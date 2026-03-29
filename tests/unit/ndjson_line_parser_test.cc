// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

// Tests for NDJSON line parsing.
// The ndjson_line_parser is internal, so we test its behavior through the
// StreamingParseSession public API by observing parsed events, diagnostics,
// and store state.

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "ccsparser/diagnostic.h"
#include "ccsparser/event.h"
#include "ccsparser/objects.h"
#include "ccsparser/parse_options.h"
#include "ccsparser/parser.h"
#include "ccsparser/status.h"
#include "ccsparser/types.h"

namespace ccsparser {
namespace {

std::unique_ptr<StreamingParseSession> MakeSession(
    ParseOptions opts = ParseOptions{}) {
  if (opts.version == ApiVersion::kAuto) {
    opts.version = ApiVersion::k2023_06;
  }
  auto s = EventFeedParser::CreateStreamingSession(opts);
  EXPECT_TRUE(s.ok());
  return std::move(s.value());
}

// =====================================================================
// Basic upsert event
// =====================================================================

TEST(NdjsonLineParserTest, BasicUpsertEvent) {
  auto session = MakeSession();
  auto s = session->ConsumeLine(
      R"({"type":"problems","id":"A","data":{"id":"A","label":"A","name":"Apples"}})");
  ASSERT_TRUE(s.ok()) << s.ToString();

  auto objs = session->store().ListObjects(ObjectType::kProblems);
  ASSERT_EQ(objs.size(), 1);
  const auto* prob = dynamic_cast<const Problem*>(objs[0]);
  ASSERT_NE(prob, nullptr);
  EXPECT_EQ(prob->id, "A");
  EXPECT_EQ(prob->label.value_or(""), "A");
  EXPECT_EQ(prob->name.value_or(""), "Apples");
}

// =====================================================================
// Delete event
// =====================================================================

TEST(NdjsonLineParserTest, DeleteEvent) {
  auto session = MakeSession();
  session->ConsumeLine(
      R"({"type":"problems","id":"A","data":{"id":"A","label":"A","name":"Apples"}})");
  ASSERT_EQ(session->store().ListObjects(ObjectType::kProblems).size(), 1);

  auto s = session->ConsumeLine(
      R"({"type":"problems","id":"A","data":null})");
  ASSERT_TRUE(s.ok()) << s.ToString();
  EXPECT_EQ(session->store().ListObjects(ObjectType::kProblems).size(), 0);
}

// =====================================================================
// Singleton update (contest)
// =====================================================================

TEST(NdjsonLineParserTest, SingletonUpdate) {
  auto session = MakeSession();
  auto s = session->ConsumeLine(
      R"({"type":"contest","data":{"id":"c1","name":"World Finals"}})");
  ASSERT_TRUE(s.ok()) << s.ToString();

  const Contest* c = session->store().GetContest();
  ASSERT_NE(c, nullptr);
  EXPECT_EQ(c->name.value_or(""), "World Finals");
}

// =====================================================================
// Keepalive (empty line)
// =====================================================================

TEST(NdjsonLineParserTest, EmptyLineIsKeepalive) {
  auto session = MakeSession();
  auto s = session->ConsumeLine("");
  ASSERT_TRUE(s.ok()) << s.ToString();
  EXPECT_EQ(session->store().GetEventCount(), 0);
}

TEST(NdjsonLineParserTest, WhitespaceLineIsKeepalive) {
  auto session = MakeSession();
  auto s = session->ConsumeLine("   \t  ");
  ASSERT_TRUE(s.ok()) << s.ToString();
  EXPECT_EQ(session->store().GetEventCount(), 0);
}

TEST(NdjsonLineParserTest, CarriageReturnLineIsKeepalive) {
  auto session = MakeSession();
  auto s = session->ConsumeLine("\r");
  ASSERT_TRUE(s.ok()) << s.ToString();
  EXPECT_EQ(session->store().GetEventCount(), 0);
}

// =====================================================================
// Malformed JSON
// =====================================================================

TEST(NdjsonLineParserTest, MalformedJsonContinueMode) {
  ParseOptions opts;
  opts.version = ApiVersion::k2023_06;
  opts.error_policy = ErrorPolicy::kContinue;
  auto session = MakeSession(opts);

  auto s = session->ConsumeLine("{not valid json}");
  // kContinue: returns ok but records a diagnostic.
  ASSERT_TRUE(s.ok()) << s.ToString();
  EXPECT_GE(session->diagnostics().size(), 1);
  EXPECT_EQ(session->diagnostics()[0].severity, Severity::kError);
  EXPECT_EQ(session->diagnostics()[0].code, DiagnosticCode::kMalformedJson);
}

TEST(NdjsonLineParserTest, MalformedJsonFailFastMode) {
  ParseOptions opts;
  opts.version = ApiVersion::k2023_06;
  opts.error_policy = ErrorPolicy::kFailFast;
  auto session = MakeSession(opts);

  auto s = session->ConsumeLine("{bad json!!!}");
  EXPECT_FALSE(s.ok());
  EXPECT_EQ(s.code(), StatusCode::kParseError);
}

// =====================================================================
// Non-object root
// =====================================================================

TEST(NdjsonLineParserTest, NonObjectRoot) {
  ParseOptions opts;
  opts.version = ApiVersion::k2023_06;
  opts.error_policy = ErrorPolicy::kContinue;
  auto session = MakeSession(opts);

  session->ConsumeLine("[1, 2, 3]");
  EXPECT_GE(session->diagnostics().size(), 1);
}

// =====================================================================
// Missing type field
// =====================================================================

TEST(NdjsonLineParserTest, MissingTypeField) {
  ParseOptions opts;
  opts.version = ApiVersion::k2023_06;
  opts.error_policy = ErrorPolicy::kContinue;
  auto session = MakeSession(opts);

  session->ConsumeLine(R"({"data":{"id":"x"}})");
  ASSERT_GE(session->diagnostics().size(), 1);
  EXPECT_EQ(session->diagnostics()[0].code, DiagnosticCode::kMissingType);
}

// =====================================================================
// Missing data field
// =====================================================================

TEST(NdjsonLineParserTest, MissingDataField) {
  ParseOptions opts;
  opts.version = ApiVersion::k2023_06;
  opts.error_policy = ErrorPolicy::kContinue;
  auto session = MakeSession(opts);

  session->ConsumeLine(R"({"type":"problems","id":"A"})");
  ASSERT_GE(session->diagnostics().size(), 1);
  EXPECT_EQ(session->diagnostics()[0].code, DiagnosticCode::kMissingData);
}

// =====================================================================
// Invalid id type
// =====================================================================

TEST(NdjsonLineParserTest, InvalidIdType) {
  ParseOptions opts;
  opts.version = ApiVersion::k2023_06;
  opts.error_policy = ErrorPolicy::kContinue;
  auto session = MakeSession(opts);

  session->ConsumeLine(R"({"type":"problems","id":42,"data":{"id":"A"}})");
  ASSERT_GE(session->diagnostics().size(), 1);
  EXPECT_EQ(session->diagnostics()[0].code, DiagnosticCode::kInvalidIdType);
}

// =====================================================================
// Token field
// =====================================================================

TEST(NdjsonLineParserTest, TokenTracking) {
  ParseOptions opts;
  opts.version = ApiVersion::k2023_06;
  opts.keep_event_log = true;
  auto session = MakeSession(opts);

  session->ConsumeLine(
      R"({"type":"problems","id":"A","token":"tok1","data":{"id":"A","label":"A"}})");

  EXPECT_EQ(session->cursor().last_token.value_or(""), "tok1");
}

TEST(NdjsonLineParserTest, TokenUpdatesOnSubsequentEvents) {
  ParseOptions opts;
  opts.version = ApiVersion::k2023_06;
  auto session = MakeSession(opts);

  session->ConsumeLine(
      R"({"type":"problems","id":"A","token":"tok1","data":{"id":"A","label":"A"}})");
  EXPECT_EQ(session->cursor().last_token.value_or(""), "tok1");

  session->ConsumeLine(
      R"({"type":"problems","id":"B","token":"tok2","data":{"id":"B","label":"B"}})");
  EXPECT_EQ(session->cursor().last_token.value_or(""), "tok2");
}

// =====================================================================
// Unknown event type
// =====================================================================

TEST(NdjsonLineParserTest, UnknownEventTypeWarning) {
  ParseOptions opts;
  opts.version = ApiVersion::k2023_06;
  opts.unknown_type_policy = UnknownTypePolicy::kWarnAndIgnore;
  auto session = MakeSession(opts);

  auto s = session->ConsumeLine(
      R"({"type":"future-thing","id":"x","data":{"id":"x","val":"y"}})");
  ASSERT_TRUE(s.ok()) << s.ToString();
  ASSERT_GE(session->diagnostics().size(), 1);
  EXPECT_EQ(session->diagnostics()[0].code,
            DiagnosticCode::kUnknownEventType);
}

TEST(NdjsonLineParserTest, UnknownEventTypeError) {
  ParseOptions opts;
  opts.version = ApiVersion::k2023_06;
  opts.unknown_type_policy = UnknownTypePolicy::kError;
  opts.error_policy = ErrorPolicy::kFailFast;
  auto session = MakeSession(opts);

  auto s = session->ConsumeLine(
      R"({"type":"future-thing","id":"x","data":{"id":"x"}})");
  EXPECT_FALSE(s.ok());
}

// =====================================================================
// Collection replace
// =====================================================================

TEST(NdjsonLineParserTest, CollectionReplace) {
  auto session = MakeSession();
  // First insert a problem.
  session->ConsumeLine(
      R"({"type":"problems","id":"A","data":{"id":"A","label":"A"}})");
  ASSERT_EQ(session->store().ListObjects(ObjectType::kProblems).size(), 1);

  // Replace entire collection.
  auto s = session->ConsumeLine(
      R"({"type":"problems","data":[{"id":"X","label":"X"},{"id":"Y","label":"Y"}]})");
  ASSERT_TRUE(s.ok()) << s.ToString();

  auto objs = session->store().ListObjects(ObjectType::kProblems);
  EXPECT_EQ(objs.size(), 2);
}

// =====================================================================
// Line counting
// =====================================================================

TEST(NdjsonLineParserTest, LineCounterIncrements) {
  auto session = MakeSession();
  session->ConsumeLine(
      R"({"type":"contest","data":{"id":"c1","name":"Test"}})");
  EXPECT_EQ(session->cursor().line_no, 1);

  session->ConsumeLine("");  // keepalive
  EXPECT_EQ(session->cursor().line_no, 2);

  session->ConsumeLine(
      R"({"type":"problems","id":"A","data":{"id":"A","label":"A"}})");
  EXPECT_EQ(session->cursor().line_no, 3);
}

// =====================================================================
// Event counting
// =====================================================================

TEST(NdjsonLineParserTest, EventCountExcludesKeepalives) {
  auto session = MakeSession();
  session->ConsumeLine(
      R"({"type":"contest","data":{"id":"c1"}})");
  session->ConsumeLine("");  // keepalive
  session->ConsumeLine(
      R"({"type":"problems","id":"A","data":{"id":"A","label":"A"}})");

  EXPECT_EQ(session->cursor().event_count, 2);
}

// =====================================================================
// Invalid token type
// =====================================================================

TEST(NdjsonLineParserTest, InvalidTokenType) {
  ParseOptions opts;
  opts.version = ApiVersion::k2023_06;
  opts.error_policy = ErrorPolicy::kContinue;
  auto session = MakeSession(opts);

  session->ConsumeLine(
      R"({"type":"problems","id":"A","token":42,"data":{"id":"A"}})");
  ASSERT_GE(session->diagnostics().size(), 1);
  EXPECT_EQ(session->diagnostics()[0].code,
            DiagnosticCode::kInvalidTokenType);
}

// =====================================================================
// Line too long
// =====================================================================

TEST(NdjsonLineParserTest, LineTooLong) {
  ParseOptions opts;
  opts.version = ApiVersion::k2023_06;
  opts.error_policy = ErrorPolicy::kContinue;
  opts.limits.max_line_bytes = 50;
  auto session = MakeSession(opts);

  // This line is well over 50 bytes.
  session->ConsumeLine(
      R"({"type":"problems","id":"A","data":{"id":"A","label":"A","name":"This is a very long problem name that exceeds the limit"}})");
  ASSERT_GE(session->diagnostics().size(), 1);
  EXPECT_EQ(session->diagnostics()[0].code, DiagnosticCode::kLineTooLong);
}

// =====================================================================
// Multiple events in sequence
// =====================================================================

TEST(NdjsonLineParserTest, MultipleEventsSequence) {
  auto session = MakeSession();

  session->ConsumeLine(
      R"({"type":"contest","data":{"id":"c1","name":"WF"}})");
  session->ConsumeLine(
      R"({"type":"judgement-types","id":"AC","data":{"id":"AC","name":"Accepted","penalty":false,"solved":true}})");
  session->ConsumeLine(
      R"({"type":"languages","id":"cpp","data":{"id":"cpp","name":"C++"}})");
  session->ConsumeLine(
      R"({"type":"problems","id":"A","data":{"id":"A","label":"A","name":"Apples","ordinal":1}})");

  EXPECT_NE(session->store().GetContest(), nullptr);
  EXPECT_EQ(session->store().ListObjects(ObjectType::kJudgementTypes).size(), 1);
  EXPECT_EQ(session->store().ListObjects(ObjectType::kLanguages).size(), 1);
  EXPECT_EQ(session->store().ListObjects(ObjectType::kProblems).size(), 1);
  EXPECT_EQ(session->cursor().event_count, 4);
}

// =====================================================================
// Singleton state event (no id)
// =====================================================================

TEST(NdjsonLineParserTest, SingletonStateNoId) {
  auto session = MakeSession();
  auto s = session->ConsumeLine(
      R"({"type":"state","data":{"started":"2024-04-14T13:00:00Z"}})");
  ASSERT_TRUE(s.ok()) << s.ToString();

  const State* st = session->store().GetState();
  ASSERT_NE(st, nullptr);
  ASSERT_TRUE(st->started.has_value());
}

// =====================================================================
// Null data with null id (empty singleton)
// =====================================================================

TEST(NdjsonLineParserTest, NullDataNullIdEmptySingleton) {
  auto session = MakeSession();
  // id=null, data=null -> treated as empty singleton update.
  auto s = session->ConsumeLine(
      R"({"type":"state","id":null,"data":null})");
  ASSERT_TRUE(s.ok()) << s.ToString();

  const State* st = session->store().GetState();
  // State should exist but with no fields set.
  ASSERT_NE(st, nullptr);
  EXPECT_FALSE(st->started.has_value());
}

// =====================================================================
// Keep raw JSON option
// =====================================================================

TEST(NdjsonLineParserTest, KeepRawJsonOption) {
  ParseOptions opts;
  opts.version = ApiVersion::k2023_06;
  opts.keep_raw_json = true;
  opts.keep_event_log = true;
  auto session = MakeSession(opts);

  std::string line =
      R"({"type":"problems","id":"A","data":{"id":"A","label":"A"}})";
  session->ConsumeLine(line);

  const auto& log = session->store().GetEventLog();
  ASSERT_EQ(log.size(), 1);
  EXPECT_TRUE(log[0].raw_line.has_value());
  EXPECT_EQ(log[0].raw_line.value(), line);
}

// =====================================================================
// Consecutive error limit
// =====================================================================

TEST(NdjsonLineParserTest, ConsecutiveErrorLimit) {
  ParseOptions opts;
  opts.version = ApiVersion::k2023_06;
  opts.error_policy = ErrorPolicy::kContinue;
  opts.limits.max_consecutive_errors = 3;
  auto session = MakeSession(opts);

  session->ConsumeLine("{bad1");
  session->ConsumeLine("{bad2");
  auto s = session->ConsumeLine("{bad3");
  EXPECT_FALSE(s.ok());
  EXPECT_EQ(s.code(), StatusCode::kLimitExceeded);
}

// =====================================================================
// Finish signals end
// =====================================================================

TEST(NdjsonLineParserTest, FinishPreventsFurtherConsumption) {
  auto session = MakeSession();
  session->ConsumeLine(
      R"({"type":"contest","data":{"id":"c1"}})");
  session->Finish();

  auto s = session->ConsumeLine(
      R"({"type":"problems","id":"A","data":{"id":"A"}})");
  EXPECT_FALSE(s.ok());
  EXPECT_EQ(s.code(), StatusCode::kFailedPrecondition);
}

}  // namespace
}  // namespace ccsparser
