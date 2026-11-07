// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "ccsparser/event_feed_stream.h"

namespace ccsparser {
namespace {

std::unique_ptr<StreamingParseSession> MakeSession() {
  ParseOptions options;
  options.version = ApiVersion::k2026_01;
  options.error_policy = ErrorPolicy::kContinue;
  auto result = EventFeedParser::CreateStreamingSession(options);
  EXPECT_TRUE(result.ok());
  return std::move(result.value());
}

class RawObserver : public Observer {
  public:
  explicit RawObserver(const ContestStore &store) : store_(store) {}
  void OnRawExtensionUpdated(const RawEvent &event) override {
    ++count;
    if (event.id && event.shape == EventShape::kSingleObject) {
      auto object = store_.GetRawObject(event.type_str, *event.id);
      saw_applied_object = object.ok();
    }
  }
  int count = 0;
  bool saw_applied_object = false;

  private:
  const ContestStore &store_;
};

TEST(EventFeedStreamTest, HandlesChunksKeepalivesAndUtf8) {
  auto session = MakeSession();
  int notifications = 0;
  int activity = 0;
  EventFeedStreamDecoder decoder(
      *session, 1024,
      [&](const auto &) {
        ++notifications;
        return true;
      },
      [&] { ++activity; });

  const std::string first =
      R"({"type":"teams","id":"t1","data":{"id":"t1","name":"队伍"},"token":"a"})";
  const size_t split = first.find("队") + 1;
  ASSERT_TRUE(decoder.Feed(first.substr(0, split)).ok());
  EXPECT_EQ(notifications, 0);
  ASSERT_TRUE(decoder.Feed(first.substr(split) + "\r\n\n").ok());
  EXPECT_EQ(notifications, 1);
  ASSERT_TRUE(
      decoder
          .Feed(R"({"type":"teams","id":"t2","data":{"id":"t2"},"token":"b"})"
                "\n")
          .ok());
  ASSERT_TRUE(decoder.Finish().ok());
  EXPECT_EQ(notifications, 2);
  EXPECT_EQ(activity, 3);
  ASSERT_TRUE(session->cursor().last_token.has_value());
  EXPECT_EQ(*session->cursor().last_token, "b");
  auto team = session->store().GetObject(ObjectType::kTeams, "t1");
  ASSERT_TRUE(team.ok());
  EXPECT_EQ(dynamic_cast<const Team *>(team.value())->name.value_or(""),
            "队伍");
}

TEST(EventFeedStreamTest, RejectsTruncatedAndOversizedRecords) {
  auto session = MakeSession();
  EventFeedStreamDecoder truncated(*session, 1024);
  ASSERT_TRUE(truncated.Feed("{\"type\":\"teams\"").ok());
  EXPECT_EQ(truncated.Finish().code(), StatusCode::kCorruptedInput);
  EXPECT_FALSE(session->cursor().last_token.has_value());

  EventFeedStreamDecoder oversized(*session, 5);
  ASSERT_TRUE(oversized.Feed("123").ok());
  EXPECT_EQ(oversized.Feed("456").code(), StatusCode::kLimitExceeded);
  EXPECT_FALSE(session->cursor().last_token.has_value());
}

TEST(EventFeedStreamTest, StopsBeforeRecordsAfterCallbackOrFailure) {
  auto session = MakeSession();
  EventFeedStreamDecoder decoder(*session, 1024,
                                 [](const auto &) { return false; });
  ASSERT_TRUE(
      decoder
          .Feed(R"({"type":"teams","id":"t1","data":{"id":"t1"},"token":"a"})"
                "\n"
                R"({"type":"teams","id":"t2","data":{"id":"t2"},"token":"b"})"
                "\n")
          .ok());
  EXPECT_TRUE(decoder.stopped());
  EXPECT_EQ(*session->cursor().last_token, "a");
  EXPECT_FALSE(session->store().GetObject(ObjectType::kTeams, "t2").ok());

  auto corrupt_session = MakeSession();
  EventFeedStreamDecoder corrupt(*corrupt_session, 1024);
  EXPECT_EQ(corrupt.Feed("{invalid}\n").code(), StatusCode::kCorruptedInput);
  EXPECT_FALSE(corrupt_session->cursor().contiguous);
}

TEST(EventFeedStreamTest, EndOfUpdatesStopsAndRejectsTrailingRecords) {
  const std::string end =
      R"({"type":"state","data":{"end_of_updates":"2026-01-01T00:00:00Z"},"token":"last"})"
      "\n";
  auto session = MakeSession();
  EventFeedStreamDecoder decoder(*session, 1024);
  ASSERT_TRUE(decoder.Feed(end).ok());
  EXPECT_TRUE(decoder.ended());
  EXPECT_EQ(*session->cursor().last_token, "last");
  EXPECT_FALSE(decoder.Feed("\n").ok());

  auto corrupt_session = MakeSession();
  EventFeedStreamDecoder corrupt(*corrupt_session, 1024);
  EXPECT_EQ(
      corrupt
          .Feed(end +
                R"({"type":"teams","id":"a","data":{"id":"a"},"token":"late"})"
                "\n")
          .code(),
      StatusCode::kCorruptedInput);
  EXPECT_FALSE(
      corrupt_session->store().GetObject(ObjectType::kTeams, "a").ok());
}

TEST(EventFeedStreamTest,
     PreservesRawExtensionsAndAdvancesOnlyAfterApplyingThem) {
  ParseOptions options;
  options.version = ApiVersion::k2026_01;
  options.unknown_type_policy = UnknownTypePolicy::kPreserveRaw;
  options.error_policy = ErrorPolicy::kFailFast;
  auto created = EventFeedParser::CreateStreamingSession(options);
  ASSERT_TRUE(created.ok());
  auto session = std::move(created.value());
  auto observer = std::make_shared<RawObserver>(session->store());
  session->mutable_store().AddObserver(observer);
  EventFeedStreamDecoder decoder(*session, 4096);
  ASSERT_TRUE(
      decoder
          .Feed(
              R"({"type":"start-status","id":"a","data":{"id":"a","ready":true},"token":"1"})"
              "\n"
              R"({"type":"start-status","id":null,"data":[{"id":"a","ready":false},{"id":"b"}],"token":"2"})"
              "\n"
              R"({"type":"start-status","id":"a","data":null,"token":"3"})"
              "\n"
              R"({"type":"custom-state","id":null,"data":null,"token":"4"})"
              "\n")
          .ok());
  ASSERT_TRUE(decoder.Finish().ok());
  EXPECT_TRUE(session->cursor().contiguous);
  ASSERT_TRUE(session->cursor().last_token.has_value());
  EXPECT_EQ(*session->cursor().last_token, "4");
  EXPECT_FALSE(session->store().GetRawObject("start-status", "a").ok());
  auto object = session->store().GetRawObject("start-status", "b");
  ASSERT_TRUE(object.ok());
  EXPECT_EQ(object.value(), R"({"id":"b"})");
  auto singleton = session->store().GetRawSingleton("custom-state");
  ASSERT_TRUE(singleton.ok());
  EXPECT_EQ(singleton.value(), "null");
  EXPECT_EQ(session->store().ListRawObjects("start-status").size(), 1u);
  EXPECT_EQ(session->store().GetEventLog().size(), 4u);
  EXPECT_EQ(observer->count, 4);
  EXPECT_TRUE(observer->saw_applied_object);
}

TEST(EventFeedStreamTest,
     InvalidRawCollectionDoesNotChangeStateOrAdvanceCursor) {
  ParseOptions options;
  options.version = ApiVersion::k2026_01;
  options.unknown_type_policy = UnknownTypePolicy::kPreserveRaw;
  options.error_policy = ErrorPolicy::kFailFast;
  auto created = EventFeedParser::CreateStreamingSession(options);
  ASSERT_TRUE(created.ok());
  auto session = std::move(created.value());
  ASSERT_TRUE(
      session
          ->ConsumeLine(
              R"({"type":"extra","id":"a","data":{"id":"a"},"token":"1"})")
          .ok());
  EXPECT_FALSE(
      session
          ->ConsumeLine(
              R"({"type":"extra","data":[{"id":"b"},{"id":"b"}],"token":"2"})")
          .ok());
  EXPECT_FALSE(session->cursor().contiguous);
  ASSERT_TRUE(session->cursor().last_token.has_value());
  EXPECT_EQ(*session->cursor().last_token, "1");
  EXPECT_TRUE(session->store().GetRawObject("extra", "a").ok());
  EXPECT_FALSE(session->store().GetRawObject("extra", "b").ok());
  EXPECT_EQ(session->store().GetEventLog().size(), 1u);
}

TEST(EventFeedStreamTest, StrictVersionRejectsMismatchedPenaltyEncoding) {
  for (auto version : {ApiVersion::k2023_06, ApiVersion::k2026_01}) {
    ParseOptions options;
    options.version = version;
    options.strict_version = true;
    options.error_policy = ErrorPolicy::kFailFast;
    auto result = EventFeedParser::CreateStreamingSession(options);
    ASSERT_TRUE(result.ok());
    auto session = std::move(result.value());
    const std::string record =
        version == ApiVersion::k2023_06
            ? R"({"type":"contest","data":{"id":"c","penalty_time":"0:20:00"},"token":"wrong"})"
            : R"({"type":"contest","data":{"id":"c","penalty_time":20},"token":"wrong"})";
    EXPECT_EQ(session->ConsumeLine(record).code(), StatusCode::kParseError);
    EXPECT_EQ(session->store().GetContest(), nullptr);
    EXPECT_FALSE(session->cursor().last_token.has_value());
    EXPECT_FALSE(session->cursor().contiguous);
  }
}

TEST(EventFeedStreamTest, PreservesScoreAndFractionalProblemLimits) {
  for (auto version : {ApiVersion::k2023_06, ApiVersion::k2026_01}) {
    ParseOptions options;
    options.version = version;
    options.strict_version = true;
    options.error_policy = ErrorPolicy::kFailFast;
    auto result = EventFeedParser::CreateStreamingSession(options);
    ASSERT_TRUE(result.ok());
    auto session = std::move(result.value());
    ASSERT_TRUE(
        session
            ->ConsumeLine(
                R"({"type":"problems","id":"p","data":{"id":"p","time_limit":2.75,"max_score":100.5}})")
            .ok());
    ASSERT_TRUE(
        session
            ->ConsumeLine(
                R"({"type":"judgements","id":"j","data":{"id":"j","submission_id":"s","score":42.25}})")
            .ok());
    auto problem = session->store().GetObject(ObjectType::kProblems, "p");
    auto judgement = session->store().GetObject(ObjectType::kJudgements, "j");
    ASSERT_TRUE(problem.ok());
    ASSERT_TRUE(judgement.ok());
    const auto *p = dynamic_cast<const Problem *>(problem.value());
    const auto *j = dynamic_cast<const Judgement *>(judgement.value());
    ASSERT_NE(p, nullptr);
    ASSERT_NE(j, nullptr);
    ASSERT_TRUE(p->time_limit.has_value());
    ASSERT_TRUE(p->max_score.has_value());
    ASSERT_TRUE(j->score.has_value());
    EXPECT_DOUBLE_EQ(*p->time_limit, 2.75);
    EXPECT_DOUBLE_EQ(*p->max_score, 100.5);
    EXPECT_DOUBLE_EQ(*j->score, 42.25);
  }
}

TEST(EventFeedStreamTest, StrictVersionRejectsMalformedRelativePenalty) {
  ParseOptions options;
  options.version = ApiVersion::k2026_01;
  options.strict_version = true;
  options.error_policy = ErrorPolicy::kFailFast;
  auto result = EventFeedParser::CreateStreamingSession(options);
  ASSERT_TRUE(result.ok());
  auto session = std::move(result.value());
  EXPECT_EQ(
      session
          ->ConsumeLine(
              R"({"type":"contest","data":{"id":"c","penalty_time":"invalid"},"token":"bad"})")
          .code(),
      StatusCode::kParseError);
  EXPECT_EQ(session->store().GetContest(), nullptr);
  EXPECT_FALSE(session->cursor().last_token.has_value());
}

} // namespace
} // namespace ccsparser
