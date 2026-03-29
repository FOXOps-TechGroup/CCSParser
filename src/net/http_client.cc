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
  bool aborted = false;
};

size_t StreamWriteCallback(void* contents, size_t size, size_t nmemb,
                           void* userp) {
  auto* data = static_cast<StreamCallbackData*>(userp);
  size_t total = size * nmemb;
  if (!(*data->cb)(static_cast<const char*>(contents), total)) {
    data->aborted = true;
    return 0;  // Abort transfer.
  }
  return total;
}

// Apply common curl options.
void ApplyOptions(CURL* curl, const HttpRequestOptions& options) {
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(options.timeout_seconds));
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);

  if (!options.verify_ssl) {
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
  }

  if (options.basic_auth.has_value()) {
    curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
    std::string userpwd =
        options.basic_auth->username + ":" + options.basic_auth->password;
    curl_easy_setopt(curl, CURLOPT_USERPWD, userpwd.c_str());
  }

  // Set a user-agent.
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "CCSParser/1.0");
}

}  // namespace

StatusOr<HttpResponse> HttpClient::Get(const std::string& url,
                                       const HttpRequestOptions& options) {
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
    return Status(StatusCode::kUnavailable,
                  "HTTP request failed: " + err + " (url: " + url + ")");
  }

  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status_code);

  char* ct = nullptr;
  curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &ct);
  if (ct) response.content_type = ct;

  curl_easy_cleanup(curl);

  if (response.status_code >= 400) {
    std::string body_excerpt = response.body.substr(0, 200);
    return Status(StatusCode::kUnavailable,
                  "HTTP " + std::to_string(response.status_code) +
                      " from " + url +
                      (body_excerpt.empty() ? "" : ": " + body_excerpt));
  }

  return response;
}

StatusOr<int> HttpClient::GetStreaming(const std::string& url,
                                      StreamCallback callback,
                                      const HttpRequestOptions& options) {
  CURL* curl = curl_easy_init();
  if (!curl) {
    return Status(StatusCode::kInternal, "Failed to initialize libcurl");
  }

  StreamCallbackData cb_data{&callback, false};
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

  CURLcode res = curl_easy_perform(curl);

  int status_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
  curl_easy_cleanup(curl);

  if (cb_data.aborted) {
    return status_code;  // User-requested abort.
  }

  if (res != CURLE_OK) {
    return Status(StatusCode::kUnavailable,
                  "HTTP streaming request failed: " +
                      std::string(curl_easy_strerror(res)));
  }

  return status_code;
}

}  // namespace ccsparser
