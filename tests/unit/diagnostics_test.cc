// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

// Tests for the Diagnostic system: construction, ToString formatting,
// and diagnostic collection behavior during parsing.

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "ccsparser/diagnostic.h"
#include "ccsparser/parse_options.h"
#include "ccsparser/parser.h"
#include "ccsparser/status.h"
#include "ccsparser/types.h"

namespace ccsparser {
namespace {

// =====================================================================
// Diagnostic struct
// =====================================================================

TEST(DiagnosticsTest, DefaultValues) {
  Diagnostic diag;
  EXPECT_EQ(diag.severity, Severity::kInfo);
  EXPECT_EQ(diag.code, DiagnosticCode::kMalformedJson);
  EXPECT_TRUE(diag.message.empty());
  EXPECT_EQ(diag.line_no, 0);
  EXPECT_FALSE(diag.raw_line.has_value());
  EXPECT_FALSE(diag.object_type.has_value());
  EXPECT_FALSE(diag.object_id.has_value());
}

// =====================================================================
// ToString formatting
// =====================================================================

TEST(DiagnosticsTest, ToStringInfo) {
  Diagnostic diag;
  diag.severity = Severity::kInfo;
  diag.line_no = 5;
  diag.message = "informational";

  std::string s = diag.ToString();
  EXPECT_NE(s.find("[INFO]"), std::string::npos);
  EXPECT_NE(s.find("line 5"), std::string::npos);
  EXPECT_NE(s.find("informational"), std::string::npos);
}

TEST(DiagnosticsTest, ToStringWarning) {
  Diagnostic diag;
  diag.severity = Severity::kWarning;
  diag.line_no = 10;
  diag.message = "something suspicious";

  std::string s = diag.ToString();
  EXPECT_NE(s.find("[WARNING]"), std::string::npos);
  EXPECT_NE(s.find("line 10"), std::string::npos);
}

TEST(DiagnosticsTest, ToStringError) {
  Diagnostic diag;
  diag.severity = Severity::kError;
  diag.line_no = 42;
  diag.message = "bad stuff";

  std::string s = diag.ToString();
  EXPECT_NE(s.find("[ERROR]"), std::string::npos);
  EXPECT_NE(s.find("line 42"), std::string::npos);
  EXPECT_NE(s.find("bad stuff"), std::string::npos);
}

TEST(DiagnosticsTest, ToStringWithObjectType) {
  Diagnostic diag;
  diag.severity = Severity::kWarning;
  diag.line_no = 7;
  diag.message = "unknown field";
  diag.object_type = "problems";

  std::string s = diag.ToString();
  EXPECT_NE(s.find("type=problems"), std::string::npos);
}

TEST(DiagnosticsTest, ToStringWithObjectTypeAndId) {
  Diagnostic diag;
  diag.severity = Severity::kError;
  diag.line_no = 3;
  diag.message = "decode failure";
  diag.object_type = "teams";
  diag.object_id = "t42";

  std::string s = diag.ToString();
  EXPECT_NE(s.find("type=teams"), std::string::npos);
  EXPECT_NE(s.find("id=t42"), std::string::npos);
}

TEST(DiagnosticsTest, ToStringNoObjectType) {
  Diagnostic diag;
  diag.severity = Severity::kError;
  diag.line_no = 1;
  diag.message = "general error";

  std::string s = diag.ToString();
  // Should not contain "type=" when object_type is not set.
  EXPECT_EQ(s.find("type="), std::string::npos);
}

// =====================================================================
// Diagnostics collected during parsing
// =====================================================================

std::unique_ptr<StreamingParseSession> MakeSession(ParseOptions opts = {}) {
  if (opts.version == ApiVersion::kAuto) {
    opts.version = ApiVersion::k2023_06;
  }
  auto s = EventFeedParser::CreateStreamingSession(opts);
  EXPECT_TRUE(s.ok());
  return std::move(s.value());
}

TEST(DiagnosticsTest, MalformedJsonDiagnostic) {
  ParseOptions opts;
  opts.version = ApiVersion::k2023_06;
  opts.error_policy = ErrorPolicy::kContinue;
  auto session = MakeSession(opts);

  session->ConsumeLine("not json at all");

  ASSERT_GE(session->diagnostics().size(), 1);
  const auto& d = session->diagnostics()[0];
  EXPECT_EQ(d.severity, Severity::kError);
  EXPECT_EQ(d.code, DiagnosticCode::kMalformedJson);
  EXPECT_EQ(d.line_no, 1);
}

TEST(DiagnosticsTest, MultipleDiagnosticsAccumulate) {
  ParseOptions opts;
  opts.version = ApiVersion::k2023_06;
  opts.error_policy = ErrorPolicy::kContinue;
  auto session = MakeSession(opts);

  session->ConsumeLine("{bad1");
  session->ConsumeLine("{bad2");

  EXPECT_GE(session->diagnostics().size(), 2);
}

TEST(DiagnosticsTest, MaxDiagnosticsLimit) {
  ParseOptions opts;
  opts.version = ApiVersion::k2023_06;
  opts.error_policy = ErrorPolicy::kContinue;
  opts.limits.max_diagnostics = 3;
  opts.limits.max_consecutive_errors = 1000;
  auto session = MakeSession(opts);

  for (int i = 0; i < 10; ++i) {
    // Alternate between valid (resetting consecutive error count) and invalid.
    session->ConsumeLine("{bad json");
    session->ConsumeLine(
        R"({"type":"contest","data":{"id":"c1","name":"test"}})");
  }

  // Should be capped at max_diagnostics.
  EXPECT_LE(session->diagnostics().size(), 3);
}

TEST(DiagnosticsTest, UnknownEventTypeDiagnostic) {
  ParseOptions opts;
  opts.version = ApiVersion::k2023_06;
  opts.unknown_type_policy = UnknownTypePolicy::kWarnAndIgnore;
  auto session = MakeSession(opts);

  session->ConsumeLine(
      R"({"type":"new-future-type","id":"x","data":{"id":"x"}})");

  ASSERT_GE(session->diagnostics().size(), 1);
  EXPECT_EQ(session->diagnostics()[0].code,
            DiagnosticCode::kUnknownEventType);
  EXPECT_EQ(session->diagnostics()[0].severity, Severity::kWarning);
}

TEST(DiagnosticsTest, DeleteUnknownObjectDiagnostic) {
  auto session = MakeSession();

  session->ConsumeLine(R"({"type":"problems","id":"A","data":null})");

  // Deleting non-existent should produce a warning.
  ASSERT_GE(session->diagnostics().size(), 1);
  EXPECT_EQ(session->diagnostics()[0].code,
            DiagnosticCode::kDeleteUnknownObject);
  EXPECT_EQ(session->diagnostics()[0].severity, Severity::kWarning);
}

TEST(DiagnosticsTest, DiagnosticLineNumberCorrectness) {
  ParseOptions opts;
  opts.version = ApiVersion::k2023_06;
  opts.error_policy = ErrorPolicy::kContinue;
  auto session = MakeSession(opts);

  session->ConsumeLine(
      R"({"type":"contest","data":{"id":"c1"}})");  // line 1, ok
  session->ConsumeLine("");                          // line 2, keepalive
  session->ConsumeLine("{bad json");                 // line 3, error

  ASSERT_GE(session->diagnostics().size(), 1);
  EXPECT_EQ(session->diagnostics()[0].line_no, 3);
}

TEST(DiagnosticsTest, RawLineInDiagnosticWhenEnabled) {
  ParseOptions opts;
  opts.version = ApiVersion::k2023_06;
  opts.error_policy = ErrorPolicy::kContinue;
  opts.keep_raw_json = true;
  auto session = MakeSession(opts);

  session->ConsumeLine("{bad json line}");

  ASSERT_GE(session->diagnostics().size(), 1);
  ASSERT_TRUE(session->diagnostics()[0].raw_line.has_value());
  EXPECT_EQ(session->diagnostics()[0].raw_line.value(), "{bad json line}");
}

TEST(DiagnosticsTest, NoRawLineWhenDisabled) {
  ParseOptions opts;
  opts.version = ApiVersion::k2023_06;
  opts.error_policy = ErrorPolicy::kContinue;
  opts.keep_raw_json = false;
  auto session = MakeSession(opts);

  session->ConsumeLine("{bad json line}");

  ASSERT_GE(session->diagnostics().size(), 1);
  EXPECT_FALSE(session->diagnostics()[0].raw_line.has_value());
}

// =====================================================================
// Status class basics (tested here since they're fundamental)
// =====================================================================

TEST(DiagnosticsTest, StatusOk) {
  Status s = Status::Ok();
  EXPECT_TRUE(s.ok());
  EXPECT_EQ(s.code(), StatusCode::kOk);
  EXPECT_EQ(s.ToString(), "OK");
}

TEST(DiagnosticsTest, StatusError) {
  Status s(StatusCode::kParseError, "something broke");
  EXPECT_FALSE(s.ok());
  EXPECT_EQ(s.code(), StatusCode::kParseError);
  EXPECT_NE(s.ToString().find("PARSE_ERROR"), std::string::npos);
  EXPECT_NE(s.ToString().find("something broke"), std::string::npos);
}

TEST(DiagnosticsTest, StatusOrValue) {
  StatusOr<int> result(42);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result.value(), 42);
  EXPECT_EQ(*result, 42);
}

TEST(DiagnosticsTest, StatusOrError) {
  StatusOr<int> result(Status(StatusCode::kNotFound, "nope"));
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), StatusCode::kNotFound);
}

}  // namespace
}  // namespace ccsparser
