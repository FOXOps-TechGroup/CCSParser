// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

// Unit test for the HTTP client.  This only tests local/safe operations
// since real network access may not be available in CI.

#include <gtest/gtest.h>

#include "ccsparser/http_client.h"

namespace ccsparser {
namespace {

TEST(HttpClientTest, ConstructDestruct) {
  // Just verify we can create and destroy without crashing.
  HttpClient client;
}

TEST(HttpClientTest, InvalidUrl) {
  HttpClient client;
  HttpRequestOptions opts;
  opts.timeout_seconds = 5;

  auto result = client.Get("http://localhost:1/nonexistent", opts);
  // Should fail with a network error.
  EXPECT_FALSE(result.ok());
}

TEST(HttpClientTest, BasicAuthOptions) {
  // Verify that options struct works correctly.
  HttpRequestOptions opts;
  opts.basic_auth = HttpBasicAuth{"user", "pass"};
  opts.timeout_seconds = 10;
  opts.verify_ssl = false;

  EXPECT_EQ(opts.basic_auth->username, "user");
  EXPECT_EQ(opts.basic_auth->password, "pass");
  EXPECT_EQ(opts.timeout_seconds, 10);
  EXPECT_FALSE(opts.verify_ssl);
}

TEST(HttpClientTest, RejectsConflictingAuthentication) {
  HttpClient client;
  HttpRequestOptions options;
  options.basic_auth = HttpBasicAuth{"user", "pass"};
  options.bearer_token = "token";
  auto response = client.GetStreaming(
      "http://127.0.0.1:1/", [](const char *, size_t) { return true; },
      options);
  ASSERT_FALSE(response.ok());
  EXPECT_EQ(response.status().code(), StatusCode::kInvalidArgument);
}

TEST(HttpClientTest, VerifiedHttpsModeRejectsInsecureInputsBeforeConnecting) {
  HttpClient client;
  HttpRequestOptions options;
  options.require_https = true;
  options.bearer_token = "token";
  auto plain = client.GetStreaming("http://127.0.0.1:1/", [](const char*, size_t) { return true; }, options);
  ASSERT_FALSE(plain.ok());
  EXPECT_EQ(plain.status().code(), StatusCode::kInvalidArgument);
  auto embedded = client.Get("https://user:password@localhost/", options);
  ASSERT_FALSE(embedded.ok());
  EXPECT_EQ(embedded.status().code(), StatusCode::kInvalidArgument);
  auto valid_but_unreachable = client.Get("https://127.0.0.1:1/", options);
  ASSERT_FALSE(valid_but_unreachable.ok());
  EXPECT_EQ(valid_but_unreachable.status().code(), StatusCode::kUnavailable);
  options.verify_ssl = false;
  auto unverified = client.Get("https://localhost/", options);
  ASSERT_FALSE(unverified.ok());
  EXPECT_EQ(unverified.status().code(), StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace ccsparser
