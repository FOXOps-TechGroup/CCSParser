// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

// HTTP client for fetching CCS Contest API resources.
// Wraps libcurl to provide a simple, synchronous interface for GET requests
// with optional Basic Auth, suitable for consuming CCS REST API endpoints.

#ifndef CCSPARSER_HTTP_CLIENT_H_
#define CCSPARSER_HTTP_CLIENT_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "ccsparser/status.h"

namespace ccsparser {

// Credentials for HTTP Basic Authentication.
struct HttpBasicAuth {
  std::string username;
  std::string password;
};

// Options for HTTP requests.
struct HttpRequestOptions {
  std::optional<HttpBasicAuth> basic_auth;
  int timeout_seconds = 30;
  bool verify_ssl = true;
};

// Response from an HTTP GET request.
struct HttpResponse {
  int status_code = 0;
  std::string body;
  std::string content_type;
};

// Simple HTTP client for CCS REST API consumption.
class HttpClient {
 public:
  HttpClient();
  ~HttpClient();

  // Perform a synchronous GET request.
  StatusOr<HttpResponse> Get(const std::string& url,
                             const HttpRequestOptions& options = {});

  // Perform a streaming GET request.  The callback is invoked with each
  // chunk of data as it arrives.  Return false from the callback to abort.
  using StreamCallback = std::function<bool(const char* data, size_t size)>;
  StatusOr<int> GetStreaming(const std::string& url,
                             StreamCallback callback,
                             const HttpRequestOptions& options = {});

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace ccsparser

#endif  // CCSPARSER_HTTP_CLIENT_H_
