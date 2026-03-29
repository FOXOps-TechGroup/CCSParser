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

}  // namespace
}  // namespace ccsparser
