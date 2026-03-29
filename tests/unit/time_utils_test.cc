// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

// Tests for time parsing and normalization logic.
// Since time_utils is an internal module, we test it through the public
// StreamingParseSession API by feeding NDJSON lines that contain time values
// and observing the parsed objects.

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>

#include "ccsparser/objects.h"
#include "ccsparser/parse_options.h"
#include "ccsparser/parser.h"
#include "ccsparser/status.h"
#include "ccsparser/types.h"

namespace ccsparser {
namespace {

// Helper: create a session with a specific API version.
std::unique_ptr<StreamingParseSession> MakeSession(ApiVersion version) {
  ParseOptions opts;
  opts.version = version;
  opts.error_policy = ErrorPolicy::kContinue;
  auto session_or = EventFeedParser::CreateStreamingSession(opts);
  EXPECT_TRUE(session_or.ok()) << session_or.status().ToString();
  return std::move(session_or.value());
}

// Helper: create a default 2023-06 session.
std::unique_ptr<StreamingParseSession> MakeSession2023() {
  return MakeSession(ApiVersion::k2023_06);
}

// Helper: create a default 2026-01 session.
std::unique_ptr<StreamingParseSession> MakeSession2026() {
  return MakeSession(ApiVersion::k2026_01);
}

// =====================================================================
// penalty_time as integer (2023-06: minutes -> RelativeTime)
// =====================================================================

TEST(TimeUtilsTest, PenaltyTimeIntegerMinutes_2023_06) {
  auto session = MakeSession2023();
  auto s = session->ConsumeLine(
      R"({"type":"contest","data":{"id":"wf2024","penalty_time":20}})");
  ASSERT_TRUE(s.ok()) << s.ToString();

  const Contest* c = session->store().GetContest();
  ASSERT_NE(c, nullptr);
  ASSERT_TRUE(c->penalty_time.has_value());
  // 20 minutes = 20 * 60 * 1000 = 1,200,000 ms.
  EXPECT_EQ(c->penalty_time->milliseconds, 1'200'000);
}

TEST(TimeUtilsTest, PenaltyTimeZeroMinutes) {
  auto session = MakeSession2023();
  session->ConsumeLine(
      R"({"type":"contest","data":{"id":"c1","penalty_time":0}})");

  const Contest* c = session->store().GetContest();
  ASSERT_NE(c, nullptr);
  ASSERT_TRUE(c->penalty_time.has_value());
  EXPECT_EQ(c->penalty_time->milliseconds, 0);
}

// =====================================================================
// penalty_time as RELTIME string (2026-01)
// =====================================================================

TEST(TimeUtilsTest, PenaltyTimeReltimeString_2026_01) {
  auto session = MakeSession2026();
  session->ConsumeLine(
      R"({"type":"contest","data":{"id":"wf2026","penalty_time":"0:20:00"}})");

  const Contest* c = session->store().GetContest();
  ASSERT_NE(c, nullptr);
  ASSERT_TRUE(c->penalty_time.has_value());
  // 0:20:00 = 20 minutes = 1,200,000 ms.
  EXPECT_EQ(c->penalty_time->milliseconds, 1'200'000);
}

TEST(TimeUtilsTest, PenaltyTimeReltimeWithFraction) {
  auto session = MakeSession2026();
  session->ConsumeLine(
      R"({"type":"contest","data":{"id":"c1","penalty_time":"0:20:00.500"}})");

  const Contest* c = session->store().GetContest();
  ASSERT_NE(c, nullptr);
  ASSERT_TRUE(c->penalty_time.has_value());
  // 0:20:00.500 = 20m + 0.5s = 1,200,500 ms.
  EXPECT_EQ(c->penalty_time->milliseconds, 1'200'500);
}

// =====================================================================
// Duration as RELTIME string
// =====================================================================

TEST(TimeUtilsTest, ContestDurationReltime) {
  auto session = MakeSession2023();
  session->ConsumeLine(
      R"({"type":"contest","data":{"id":"c1","duration":"5:00:00"}})");

  const Contest* c = session->store().GetContest();
  ASSERT_NE(c, nullptr);
  ASSERT_TRUE(c->duration.has_value());
  // 5:00:00 = 5 hours = 18,000,000 ms.
  EXPECT_EQ(c->duration->milliseconds, 18'000'000);
}

TEST(TimeUtilsTest, ContestDurationWithMilliseconds) {
  auto session = MakeSession2023();
  session->ConsumeLine(
      R"({"type":"contest","data":{"id":"c1","duration":"5:00:00.123"}})");

  const Contest* c = session->store().GetContest();
  ASSERT_NE(c, nullptr);
  ASSERT_TRUE(c->duration.has_value());
  EXPECT_EQ(c->duration->milliseconds, 18'000'123);
}

TEST(TimeUtilsTest, ScoreboardFreezeDuration) {
  auto session = MakeSession2023();
  session->ConsumeLine(
      R"({"type":"contest","data":{"id":"c1","scoreboard_freeze_duration":"1:00:00"}})");

  const Contest* c = session->store().GetContest();
  ASSERT_NE(c, nullptr);
  ASSERT_TRUE(c->scoreboard_freeze_duration.has_value());
  // 1:00:00 = 1 hour = 3,600,000 ms.
  EXPECT_EQ(c->scoreboard_freeze_duration->milliseconds, 3'600'000);
}

// =====================================================================
// AbsoluteTime parsing through start_time / end_time
// =====================================================================

TEST(TimeUtilsTest, AbsoluteTimeUtcZ) {
  auto session = MakeSession2023();
  session->ConsumeLine(
      R"({"type":"contest","data":{"id":"c1","start_time":"2024-04-14T13:00:00Z"}})");

  const Contest* c = session->store().GetContest();
  ASSERT_NE(c, nullptr);
  ASSERT_TRUE(c->start_time.has_value());
  EXPECT_EQ(c->start_time->tz_offset_minutes, 0);
  // 2024-04-14T13:00:00Z epoch ms: verify via cross-check.
  // Known: 2024-04-14T13:00:00Z = 1713099600 seconds since epoch.
  EXPECT_EQ(c->start_time->epoch_ms, 1713099600LL * 1000);
}

TEST(TimeUtilsTest, AbsoluteTimePositiveOffset) {
  auto session = MakeSession2023();
  session->ConsumeLine(
      R"({"type":"contest","data":{"id":"c1","start_time":"2024-04-14T22:00:00+09:00"}})");

  const Contest* c = session->store().GetContest();
  ASSERT_NE(c, nullptr);
  ASSERT_TRUE(c->start_time.has_value());
  EXPECT_EQ(c->start_time->tz_offset_minutes, 540);  // +09:00 = 540 min.
  // 2024-04-14T22:00:00+09:00 = 2024-04-14T13:00:00Z = 1713099600 seconds.
  EXPECT_EQ(c->start_time->epoch_ms, 1713099600LL * 1000);
}

TEST(TimeUtilsTest, AbsoluteTimeNegativeOffset) {
  auto session = MakeSession2023();
  session->ConsumeLine(
      R"({"type":"contest","data":{"id":"c1","start_time":"2024-04-14T08:00:00-05:00"}})");

  const Contest* c = session->store().GetContest();
  ASSERT_NE(c, nullptr);
  ASSERT_TRUE(c->start_time.has_value());
  EXPECT_EQ(c->start_time->tz_offset_minutes, -300);  // -05:00 = -300 min.
  // 2024-04-14T08:00:00-05:00 = 2024-04-14T13:00:00Z = 1713099600 seconds.
  EXPECT_EQ(c->start_time->epoch_ms, 1713099600LL * 1000);
}

TEST(TimeUtilsTest, AbsoluteTimeWithMilliseconds) {
  auto session = MakeSession2023();
  session->ConsumeLine(
      R"({"type":"contest","data":{"id":"c1","start_time":"2024-04-14T13:00:00.456Z"}})");

  const Contest* c = session->store().GetContest();
  ASSERT_NE(c, nullptr);
  ASSERT_TRUE(c->start_time.has_value());
  EXPECT_EQ(c->start_time->epoch_ms, 1713099600LL * 1000 + 456);
}

// =====================================================================
// contest_time as RELTIME on submissions
// =====================================================================

TEST(TimeUtilsTest, SubmissionContestTime) {
  auto session = MakeSession2023();
  session->ConsumeLine(
      R"({"type":"submissions","id":"s1","data":{"id":"s1","language_id":"cpp","problem_id":"A","team_id":"t1","time":"2024-04-14T14:23:45Z","contest_time":"1:23:45"}})");

  auto objs = session->store().ListObjects(ObjectType::kSubmissions);
  ASSERT_EQ(objs.size(), 1);
  const auto* sub = dynamic_cast<const Submission*>(objs[0]);
  ASSERT_NE(sub, nullptr);
  ASSERT_TRUE(sub->contest_time.has_value());
  // 1:23:45 = 1*3600 + 23*60 + 45 = 5025 seconds = 5025000 ms.
  EXPECT_EQ(sub->contest_time->milliseconds, 5'025'000);
}

TEST(TimeUtilsTest, SubmissionContestTimeWithFraction) {
  auto session = MakeSession2023();
  session->ConsumeLine(
      R"({"type":"submissions","id":"s2","data":{"id":"s2","language_id":"cpp","problem_id":"A","team_id":"t1","time":"2024-04-14T14:23:45Z","contest_time":"1:23:45.678"}})");

  auto objs = session->store().ListObjects(ObjectType::kSubmissions);
  ASSERT_EQ(objs.size(), 1);
  const auto* sub = dynamic_cast<const Submission*>(objs[0]);
  ASSERT_NE(sub, nullptr);
  ASSERT_TRUE(sub->contest_time.has_value());
  EXPECT_EQ(sub->contest_time->milliseconds, 5'025'678);
}

// =====================================================================
// Large hours value
// =====================================================================

TEST(TimeUtilsTest, LargeHoursValue) {
  auto session = MakeSession2023();
  session->ConsumeLine(
      R"({"type":"contest","data":{"id":"c1","duration":"100:00:00"}})");

  const Contest* c = session->store().GetContest();
  ASSERT_NE(c, nullptr);
  ASSERT_TRUE(c->duration.has_value());
  // 100 hours = 360,000,000 ms.
  EXPECT_EQ(c->duration->milliseconds, 360'000'000);
}

// =====================================================================
// Null / missing time fields
// =====================================================================

TEST(TimeUtilsTest, NullStartTime) {
  auto session = MakeSession2023();
  session->ConsumeLine(
      R"({"type":"contest","data":{"id":"c1","start_time":null}})");

  const Contest* c = session->store().GetContest();
  ASSERT_NE(c, nullptr);
  EXPECT_FALSE(c->start_time.has_value());
}

TEST(TimeUtilsTest, MissingDuration) {
  auto session = MakeSession2023();
  session->ConsumeLine(
      R"({"type":"contest","data":{"id":"c1","name":"Test"}})");

  const Contest* c = session->store().GetContest();
  ASSERT_NE(c, nullptr);
  EXPECT_FALSE(c->duration.has_value());
}

// =====================================================================
// RelativeTime / AbsoluteTime struct behavior
// =====================================================================

TEST(TimeUtilsTest, RelativeTimeToString) {
  RelativeTime rt{5'025'678};  // 1:23:45.678
  EXPECT_EQ(rt.ToString(), "1:23:45.678");
}

TEST(TimeUtilsTest, RelativeTimeToStringNoFraction) {
  RelativeTime rt{5'025'000};  // 1:23:45
  EXPECT_EQ(rt.ToString(), "1:23:45");
}

TEST(TimeUtilsTest, RelativeTimeToStringZero) {
  RelativeTime rt{0};
  EXPECT_EQ(rt.ToString(), "0:00:00");
}

TEST(TimeUtilsTest, RelativeTimeNegative) {
  RelativeTime rt{-5'025'678};
  EXPECT_TRUE(rt.is_negative());
  EXPECT_EQ(rt.ToString(), "-1:23:45.678");
}

TEST(TimeUtilsTest, RelativeTimeComparison) {
  RelativeTime a{1000};
  RelativeTime b{2000};
  RelativeTime c{1000};

  EXPECT_TRUE(a == c);
  EXPECT_TRUE(a != b);
  EXPECT_TRUE(a < b);
  EXPECT_FALSE(b < a);
}

TEST(TimeUtilsTest, AbsoluteTimeEquality) {
  AbsoluteTime a{1713099600000LL, 0};
  AbsoluteTime b{1713099600000LL, 540};
  // Equality is based on epoch_ms only.
  EXPECT_TRUE(a == b);
  EXPECT_FALSE(a != b);
}

// =====================================================================
// Invalid time values produce diagnostics
// =====================================================================

TEST(TimeUtilsTest, InvalidReltimeProducesDiagnostic) {
  auto session = MakeSession2026();
  session->ConsumeLine(
      R"({"type":"contest","data":{"id":"c1","penalty_time":"not-a-time"}})");

  const Contest* c = session->store().GetContest();
  ASSERT_NE(c, nullptr);
  // Invalid reltime should not be set.
  EXPECT_FALSE(c->penalty_time.has_value());
  // Should produce at least one diagnostic.
  EXPECT_FALSE(session->diagnostics().empty());
}

TEST(TimeUtilsTest, InvalidAbsoluteTimeProducesDiagnostic) {
  auto session = MakeSession2023();
  session->ConsumeLine(
      R"({"type":"contest","data":{"id":"c1","start_time":"bad-time"}})");

  const Contest* c = session->store().GetContest();
  ASSERT_NE(c, nullptr);
  EXPECT_FALSE(c->start_time.has_value());
  EXPECT_FALSE(session->diagnostics().empty());
}

// =====================================================================
// State time fields
// =====================================================================

TEST(TimeUtilsTest, StateTimestamps) {
  auto session = MakeSession2023();
  session->ConsumeLine(
      R"({"type":"state","data":{"started":"2024-04-14T13:00:00Z","frozen":"2024-04-14T17:00:00Z"}})");

  const State* st = session->store().GetState();
  ASSERT_NE(st, nullptr);
  ASSERT_TRUE(st->started.has_value());
  ASSERT_TRUE(st->frozen.has_value());
  EXPECT_FALSE(st->ended.has_value());
  EXPECT_EQ(st->started->epoch_ms, 1713099600LL * 1000);
  // 4 hours later.
  EXPECT_EQ(st->frozen->epoch_ms, (1713099600LL + 14400) * 1000);
}

}  // namespace
}  // namespace ccsparser
