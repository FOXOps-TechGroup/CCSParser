// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

#include <gtest/gtest.h>

#include <memory>
#include <sstream>
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

TEST(EventFeedStreamTest, LastAppliedRecordIsAvailableWithoutRetainingHistory) {
  ParseOptions options;
  options.version = ApiVersion::k2026_01;
  options.strict_version = true;
  options.error_policy = ErrorPolicy::kFailFast;
  options.unknown_type_policy = UnknownTypePolicy::kPreserveRaw;
  options.keep_event_log = false;
  options.keep_raw_json = false;
  auto result = EventFeedParser::CreateStreamingSession(options);
  ASSERT_TRUE(result.ok());
  auto session = std::move(result.value());
  EXPECT_EQ(session->last_applied_event(), nullptr);
  ASSERT_TRUE(
      session
          ->ConsumeLine(
              R"({"type":"teams","id":"a","data":{"id":"a","name":"队伍","extension":{"v":1}},"token":"first"})")
          .ok());
  const auto *record = session->last_applied_event();
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(record->type_str, "teams");
  EXPECT_EQ(record->id, "a");
  EXPECT_EQ(record->token, session->cursor().last_token);
  EXPECT_NE(record->data_json.find("extension"), std::string::npos);
  EXPECT_FALSE(record->raw_line.has_value());
  EXPECT_TRUE(session->store().GetEventLog().empty());
  ASSERT_TRUE(session->ConsumeLine(" ").ok());
  EXPECT_EQ(session->last_applied_event()->token, "first");
  ASSERT_TRUE(
      session
          ->ConsumeLine(
              R"({"type":"teams","id":null,"data":[{"id":"b"}],"token":"replace"})")
          .ok());
  EXPECT_EQ(session->last_applied_event()->shape,
            EventShape::kCollectionReplace);
  EXPECT_EQ(session->last_applied_event()->data_json, R"([{"id":"b"}])");
  ASSERT_TRUE(
      session
          ->ConsumeLine(
              R"({"type":"teams","id":"b","data":null,"token":"delete"})")
          .ok());
  EXPECT_EQ(session->last_applied_event()->shape, EventShape::kDelete);
  EXPECT_TRUE(session->last_applied_event()->data_json.empty());
  ASSERT_TRUE(
      session
          ->ConsumeLine(
              R"({"type":"custom-resource","id":"x","data":{"id":"x","value":7},"token":"raw"})")
          .ok());
  EXPECT_EQ(session->last_applied_event()->object_type, ObjectType::kUnknown);
  EXPECT_NE(session->last_applied_event()->data_json.find("value"),
            std::string::npos);
  EXPECT_FALSE(session->ConsumeLine("{broken}").ok());
  EXPECT_EQ(session->last_applied_event()->token, "raw");
  EXPECT_EQ(session->cursor().last_token, "raw");
  EXPECT_TRUE(session->store().GetEventLog().empty());
}

TEST(EventFeedStreamTest,
     LastAppliedRecordAndCursorAreCommittedBeforeCallback) {
  ParseOptions options;
  options.version = ApiVersion::k2026_01;
  options.strict_version = true;
  options.keep_event_log = false;
  auto created = EventFeedParser::CreateStreamingSession(options);
  ASSERT_TRUE(created.ok());
  auto session = std::move(created.value());
  int called = 0;
  EventFeedStreamDecoder decoder(*session, 4096, [&](const auto &current) {
    ++called;
    const auto *record = current.last_applied_event();
    EXPECT_NE(record, nullptr);
    if (!record)
      return false;
    EXPECT_EQ(record->token, current.cursor().last_token);
    EXPECT_TRUE(
        current.store().GetObject(ObjectType::kTeams, *record->id).ok());
    return false;
  });
  ASSERT_TRUE(
      decoder
          .Feed(R"({"type":"teams","id":"a","data":{"id":"a"},"token":"a"})"
                "\n"
                R"({"type":"teams","id":"b","data":{"id":"b"},"token":"b"})"
                "\n")
          .ok());
  EXPECT_TRUE(decoder.stopped());
  EXPECT_EQ(called, 1);
  EXPECT_EQ(session->cursor().last_token, "a");
  EXPECT_EQ(session->last_applied_event()->id, "a");
  EXPECT_FALSE(session->store().GetObject(ObjectType::kTeams, "b").ok());
}

TEST(EventFeedStreamTest,
     RestoresACompleteStoreAndCursorThenContinuesStreaming) {
  for (const auto version : {ApiVersion::k2023_06, ApiVersion::k2026_01}) {
    ParseOptions options;
    options.version = version;
    options.strict_version = true;
    options.error_policy = ErrorPolicy::kFailFast;
    options.keep_event_log = false;
    options.unknown_type_policy = UnknownTypePolicy::kPreserveRaw;
    std::istringstream input(
        R"({"type":"teams","id":"a","data":{"id":"a","name":"Before restart"},"token":"old"})"
        "\n"
        R"({"type":"custom-resource","id":"x","data":{"id":"x","value":9},"token":"checkpoint"})"
        "\n");
    auto checkpoint = EventFeedParser::ParseStream(input, options);
    ASSERT_TRUE(checkpoint.ok());
    const auto saved_cursor = checkpoint->cursor;
    auto resume_options = options;
    resume_options.version = ApiVersion::kAuto;
    auto restored = EventFeedParser::RestoreStreamingSession(
        std::move(checkpoint.value()), resume_options);
    ASSERT_TRUE(restored.ok()) << restored.status().ToString();
    auto session = std::move(restored.value());
    EXPECT_EQ(session->resolved_version(), version);
    EXPECT_TRUE(session->enforces_declared_version());
    EXPECT_EQ(session->cursor().last_token, saved_cursor.last_token);
    EXPECT_EQ(session->cursor().event_count, saved_cursor.event_count);
    EXPECT_EQ(session->cursor().line_no, saved_cursor.line_no);
    EXPECT_EQ(session->last_applied_event(), nullptr);
    EXPECT_TRUE(session->store().GetObject(ObjectType::kTeams, "a").ok());
    EXPECT_TRUE(session->store().GetRawObject("custom-resource", "x").ok());
    EventFeedStreamDecoder decoder(*session, 4096);
    ASSERT_TRUE(
        decoder
            .Feed(
                R"({"type":"teams","id":"b","data":{"id":"b"},"token":"after"})"
                "\n")
            .ok());
    EXPECT_EQ(session->cursor().event_count, saved_cursor.event_count + 1);
    EXPECT_EQ(session->cursor().last_token, "after");
    EXPECT_TRUE(session->store().GetObject(ObjectType::kTeams, "a").ok());
    EXPECT_TRUE(session->store().GetObject(ObjectType::kTeams, "b").ok());
    EXPECT_TRUE(session->store().GetEventLog().empty());
    const std::string penalty =
        version == ApiVersion::k2023_06 ? "20" : "\"0:20:00\"";
    ASSERT_TRUE(session
                    ->ConsumeLine("{\"type\":\"contest\",\"id\":null,\"data\":{"
                                  "\"id\":\"c\",\"penalty_time\":" +
                                  penalty + "},\"token\":\"profile\"}")
                    .ok());
    const std::string other =
        version == ApiVersion::k2023_06 ? "\"0:20:00\"" : "20";
    EXPECT_EQ(session
                  ->ConsumeLine("{\"type\":\"contest\",\"id\":null,\"data\":{"
                                "\"id\":\"c\",\"penalty_time\":" +
                                other + "},\"token\":\"bad-profile\"}")
                  .code(),
              StatusCode::kParseError);
    EXPECT_EQ(session->cursor().last_token, "profile");
  }
}

TEST(EventFeedStreamTest, RejectsIncompleteOrVersionMismatchedCheckpoints) {
  for (int kind = 0; kind < 3; ++kind) {
    ParseOptions options;
    options.version = ApiVersion::k2026_01;
    options.strict_version = true;
    ParseResult checkpoint;
    checkpoint.resolved_version =
        kind == 0 ? ApiVersion::kAuto : ApiVersion::k2023_06;
    if (kind == 1) {
      checkpoint.resolved_version = ApiVersion::k2026_01;
      checkpoint.cursor.contiguous = false;
    }
    EXPECT_EQ(
        EventFeedParser::RestoreStreamingSession(std::move(checkpoint), options)
            .status()
            .code(),
        StatusCode::kInvalidArgument);
  }
}

TEST(EventFeedStreamTest, RestoresTerminalStateWithoutAcceptingFurtherRecords) {
  ParseOptions options;
  options.version = ApiVersion::k2026_01;
  options.strict_version = true;
  options.error_policy = ErrorPolicy::kFailFast;
  std::istringstream input(
      R"({"type":"state","id":null,"data":{"end_of_updates":"2026-11-07T00:00:00Z"},"token":"last"})"
      "\n");
  auto checkpoint = EventFeedParser::ParseStream(input, options);
  ASSERT_TRUE(checkpoint.ok());
  ASSERT_TRUE(checkpoint->cursor.end_of_updates);
  auto restored = EventFeedParser::RestoreStreamingSession(
      std::move(checkpoint.value()), options);
  ASSERT_TRUE(restored.ok());
  EXPECT_TRUE(restored.value()->cursor().end_of_updates);
  EXPECT_EQ(
      restored.value()
          ->ConsumeLine(
              R"({"type":"teams","id":"a","data":{"id":"a"},"token":"invalid"})")
          .code(),
      StatusCode::kCorruptedInput);
  EXPECT_EQ(restored.value()->cursor().last_token, "last");
}

} // namespace
} // namespace ccsparser
