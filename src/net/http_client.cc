// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

#include "ccsparser/http_client.h"

#include <curl/curl.h>

#include <cstring>
#include <mutex>
#include <string>

namespace ccsparser {

namespace {

// curl_global_init/cleanup must be called exactly once per process and must
// not run concurrently with any other libcurl call.  We use a once-flag to
// guarantee a single global initialisation regardless of how many HttpClient
// instances are created.
std::once_flag g_curl_init_flag;

void EnsureCurlGlobalInit() {
  std::call_once(g_curl_init_flag, []() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
  });
}

}  // namespace

struct HttpClient::Impl {
  Impl() {
    EnsureCurlGlobalInit();
  }
};

HttpClient::HttpClient() : impl_(std::make_unique<Impl>()) {}
HttpClient::~HttpClient() = default;

namespace {

size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
  auto* body = static_cast<std::string*>(userp);
  size_t total = size * nmemb;
  body->append(static_cast<const char*>(contents), total);
  return total;
}

struct StreamCallbackData {
  HttpClient::StreamCallback* cb;
  const HttpRequestOptions *options;
  CURL *curl;
  bool aborted = false;
  bool cancelled = false;
  bool callback_failed = false;
};

size_t StreamWriteCallback(void* contents, size_t size, size_t nmemb,
                           void* userp) {
  auto* data = static_cast<StreamCallbackData*>(userp);
  size_t total = size * nmemb;
  long status_code = 0;
  curl_easy_getinfo(data->curl, CURLINFO_RESPONSE_CODE, &status_code);
  if (status_code != 0 && (status_code < 200 || status_code >= 300)) {
    return total;
  }
  try {
    if (!(*data->cb)(static_cast<const char *>(contents), total)) {
      data->aborted = true;
      return 0;
    }
  } catch (...) {
    data->callback_failed = true;
    return 0;
  }
  return total;
}

int StreamProgressCallback(void *userp, curl_off_t, curl_off_t, curl_off_t,
                           curl_off_t) {
  auto *data = static_cast<StreamCallbackData *>(userp);
  try {
    if (data->options->should_cancel && data->options->should_cancel()) {
      data->cancelled = true;
      return 1;
    }
  } catch (...) {
    data->callback_failed = true;
    return 1;
  }
  return 0;
}

Status ValidateOptions(const HttpRequestOptions &options) {
  if (options.basic_auth && options.bearer_token) {
    return Status(StatusCode::kInvalidArgument,
                  "Basic and bearer authentication cannot both be set");
  }
  if (options.bearer_token && options.bearer_token->empty()) {
    return Status(StatusCode::kInvalidArgument, "Bearer token is empty");
  }
  if (options.require_https && !options.verify_ssl) {
    return Status(StatusCode::kInvalidArgument,
                  "Verified HTTPS cannot disable certificate verification");
  }
  return Status::Ok();
}

Status ValidateUrl(const std::string& url, const HttpRequestOptions& options) {
  if (!options.require_https) return Status::Ok();
  CURLU* parsed = curl_url();
  if (!parsed) return Status(StatusCode::kInternal, "Could not initialize URL parser");
  const CURLUcode parsed_status = curl_url_set(parsed, CURLUPART_URL, url.c_str(), 0);
  char* scheme = nullptr;
  char* user = nullptr;
  char* password = nullptr;
  const bool https = parsed_status == CURLUE_OK &&
      curl_url_get(parsed, CURLUPART_SCHEME, &scheme, 0) == CURLUE_OK &&
      std::strcmp(scheme, "https") == 0;
  const bool has_user = curl_url_get(parsed, CURLUPART_USER, &user, 0) == CURLUE_OK;
  const bool has_password = curl_url_get(parsed, CURLUPART_PASSWORD, &password, 0) == CURLUE_OK;
  curl_free(scheme);
  curl_free(user);
  curl_free(password);
  curl_url_cleanup(parsed);
  if (!https || has_user || has_password) {
    return Status(StatusCode::kInvalidArgument,
                  "A credential-free HTTPS URL is required");
  }
  return Status::Ok();
}

// Apply common curl options.
void ApplyOptions(CURL* curl, const HttpRequestOptions& options) {
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(options.timeout_seconds));
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, options.require_https ? 0L : 1L);
  curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
  if (options.require_https) {
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
#else
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
#endif
  }

  if (!options.verify_ssl) {
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
  }
  if (options.ca_bundle_path) {
    curl_easy_setopt(curl, CURLOPT_CAINFO, options.ca_bundle_path->c_str());
  }

  if (options.basic_auth.has_value()) {
    curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
    std::string userpwd =
        options.basic_auth->username + ":" + options.basic_auth->password;
    curl_easy_setopt(curl, CURLOPT_USERPWD, userpwd.c_str());
  }
  if (options.bearer_token) {
    curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BEARER);
    curl_easy_setopt(curl, CURLOPT_XOAUTH2_BEARER,
                     options.bearer_token->c_str());
  }

  // Set a user-agent.
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "CCSParser/1.0");
}

}  // namespace

StatusOr<HttpResponse> HttpClient::Get(const std::string& url,
                                       const HttpRequestOptions& options) {
  auto valid = ValidateOptions(options);
  if (!valid.ok())
    return valid;
  valid = ValidateUrl(url, options);
  if (!valid.ok()) return valid;
  CURL* curl = curl_easy_init();
  if (!curl) {
    return Status(StatusCode::kInternal, "Failed to initialize libcurl");
  }

  HttpResponse response;
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
  ApplyOptions(curl, options);

  CURLcode res = curl_easy_perform(curl);
  if (res != CURLE_OK) {
    std::string err = curl_easy_strerror(res);
    curl_easy_cleanup(curl);
    return Status(StatusCode::kUnavailable, "HTTP request failed: " + err);
  }

  long status_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
  response.status_code = static_cast<int>(status_code);

  char* ct = nullptr;
  curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &ct);
  if (ct) response.content_type = ct;

  curl_easy_cleanup(curl);

  if (response.status_code >= 400) {
    return Status(StatusCode::kUnavailable,
                  "HTTP " + std::to_string(response.status_code));
  }

  return response;
}

StatusOr<int> HttpClient::GetStreaming(const std::string& url,
                                      StreamCallback callback,
                                      const HttpRequestOptions& options) {
  auto valid = ValidateOptions(options);
  if (!valid.ok())
    return valid;
  valid = ValidateUrl(url, options);
  if (!valid.ok()) return valid;
  if (!callback) {
    return Status(StatusCode::kInvalidArgument, "Stream callback is required");
  }
  CURL* curl = curl_easy_init();
  if (!curl) {
    return Status(StatusCode::kInternal, "Failed to initialize libcurl");
  }

  StreamCallbackData cb_data{&callback, &options, curl};
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, StreamWriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &cb_data);

  // For streaming, disable timeout (could run indefinitely).
  HttpRequestOptions stream_opts = options;
  if (stream_opts.timeout_seconds == 0) {
    stream_opts.timeout_seconds = 0;  // No timeout.
  }
  ApplyOptions(curl, stream_opts);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);  // Override: no timeout.
  curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, StreamProgressCallback);
  curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &cb_data);

  CURLcode res = curl_easy_perform(curl);

  long status_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
  curl_easy_cleanup(curl);

  if (cb_data.callback_failed) {
    return Status(StatusCode::kInternal, "HTTP stream callback failed");
  }
  if (cb_data.cancelled) {
    return Status(StatusCode::kCancelled, "HTTP streaming request cancelled");
  }
  if (cb_data.aborted) {
    return static_cast<int>(status_code);
  }
  if (res == CURLE_HTTP_RETURNED_ERROR && status_code >= 400) {
    return static_cast<int>(status_code);
  }

  if (res != CURLE_OK) {
    return Status(StatusCode::kUnavailable,
                  "HTTP streaming request failed: " +
                      std::string(curl_easy_strerror(res)));
  }

  return static_cast<int>(status_code);
}

}  // namespace ccsparser
