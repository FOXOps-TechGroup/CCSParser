// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

// Integration tests for CCS Spec 2023-06.
// These tests exercise the full parsing pipeline through the public API,
// feeding inline NDJSON and verifying the resulting ContestStore state.

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "ccsparser/ccsparser.h"

namespace ccsparser {
namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

class Spec202306Test : public ::testing::Test {
 protected:
  std::unique_ptr<StreamingParseSession> MakeSession(
      ParseOptions opts = {}) {
    opts.version = ApiVersion::k2023_06;
    if (opts.error_policy == ErrorPolicy::kContinue) {
      // default already
    }
    opts.keep_event_log = true;
    opts.unknown_field_policy = UnknownFieldPolicy::kPreserve;
    auto s = EventFeedParser::CreateStreamingSession(opts);
    EXPECT_TRUE(s.ok()) << s.status().ToString();
    return std::move(s.value());
  }

  // Feed multiple lines to a session. Empty strings become empty lines.
  void FeedLines(StreamingParseSession& session,
                 const std::vector<std::string>& lines) {
    for (const auto& line : lines) {
      auto s = session.ConsumeLine(line);
      EXPECT_TRUE(s.ok()) << "Line failed: " << line
                          << " — " << s.ToString();
    }
  }

  // A minimal complete 2023-06 feed covering all object types.
  std::vector<std::string> FullFeed() {
    return {
        R"({"type":"state","data":{},"token":"t0"})",
        R"({"type":"contest","id":"demo","data":{"id":"demo","name":"Demo Contest","formal_name":"Demo 2025","start_time":"2025-01-01T10:00:00.000+00:00","duration":"5:00:00.000","scoreboard_freeze_duration":"1:00:00.000","scoreboard_type":"pass-fail","penalty_time":20},"token":"t1"})",
        R"({"type":"judgement-types","id":"AC","data":{"id":"AC","name":"correct","penalty":false,"solved":true},"token":"t2"})",
        R"({"type":"judgement-types","id":"WA","data":{"id":"WA","name":"wrong answer","penalty":true,"solved":false},"token":"t3"})",
        R"({"type":"languages","id":"cpp","data":{"id":"cpp","name":"C++","extensions":["cpp","cc"]},"token":"t4"})",
        R"({"type":"problems","id":"A","data":{"id":"A","ordinal":1,"label":"A","name":"Problem A","rgb":"#ff0000","time_limit":2,"test_data_count":10},"token":"t5"})",
        R"({"type":"problems","id":"B","data":{"id":"B","ordinal":2,"label":"B","name":"Problem B","rgb":"#00ff00","time_limit":3,"test_data_count":5},"token":"t6"})",
        R"({"type":"groups","id":"participants","data":{"id":"participants","name":"Participants","hidden":false},"token":"t7"})",
        R"({"type":"organizations","id":"org1","data":{"id":"org1","name":"University A","country":"USA"},"token":"t8"})",
        R"({"type":"teams","id":"team1","data":{"id":"team1","name":"Team Alpha","organization_id":"org1","group_ids":["participants"]},"token":"t9"})",
        R"({"type":"teams","id":"team2","data":{"id":"team2","name":"Team Beta","organization_id":"org1","group_ids":["participants"]},"token":"t10"})",
        R"({"type":"persons","id":"p1","data":{"id":"p1","name":"Alice","role":"contestant","team_id":"team1"},"token":"t10a"})",
        R"({"type":"accounts","id":"a1","data":{"id":"a1","username":"team1user","type":"team","team_id":"team1"},"token":"t10b"})",
        R"({"type":"submissions","id":"s1","data":{"id":"s1","language_id":"cpp","problem_id":"A","team_id":"team1","time":"2025-01-01T10:15:00.000+00:00","contest_time":"0:15:00.000"},"token":"t11"})",
        R"({"type":"judgements","id":"j1","data":{"id":"j1","submission_id":"s1","judgement_type_id":"AC","start_time":"2025-01-01T10:15:01.000+00:00","start_contest_time":"0:15:01.000","end_time":"2025-01-01T10:15:05.000+00:00","end_contest_time":"0:15:05.000","max_run_time":0.5},"token":"t12"})",
        R"({"type":"runs","id":"r1","data":{"id":"r1","judgement_id":"j1","ordinal":1,"judgement_type_id":"AC","time":"2025-01-01T10:15:02.000+00:00","contest_time":"0:15:02.000","run_time":0.3},"token":"t12a"})",
        R"({"type":"clarifications","id":"c1","data":{"id":"c1","from_team_id":"team1","to_team_id":"team1","problem_id":"A","time":"2025-01-01T10:20:00.000+00:00","contest_time":"0:20:00.000","text":"Is the input always positive?"},"token":"t13"})",
        R"({"type":"awards","id":"gold-medal","data":{"id":"gold-medal","citation":"Gold Medal","team_ids":["team1"]},"token":"t14"})",
        R"({"type":"commentary","id":"com1","data":{"id":"com1","message":"Team Alpha solved A!","team_ids":["team1"],"problem_ids":["A"],"time":"2025-01-01T10:16:00.000+00:00","contest_time":"0:16:00.000"},"token":"t14a"})",
        R"({"type":"state","data":{"started":"2025-01-01T10:00:00.000+00:00","ended":"2025-01-01T15:00:00.000+00:00","finalized":"2025-01-01T15:01:00.000+00:00","end_of_updates":"2025-01-01T15:02:00.000+00:00"},"token":"t15"})",
    };
  }
};

// =====================================================================
// basic_object_flow
// =====================================================================

TEST_F(Spec202306Test, basic_object_flow) {
  auto session = MakeSession();
  FeedLines(*session, FullFeed());
  auto s = session->Finish();
  ASSERT_TRUE(s.ok()) << s.ToString();

  const auto& store = session->store();

  // Contest singleton
  ASSERT_NE(store.GetContest(), nullptr);
  EXPECT_EQ(store.GetContest()->name.value_or(""), "Demo Contest");

  // State singleton
  ASSERT_NE(store.GetState(), nullptr);
  EXPECT_TRUE(store.GetState()->started.has_value());
  EXPECT_TRUE(store.GetState()->end_of_updates.has_value());

  // Collection types
  EXPECT_EQ(store.ListObjects(ObjectType::kJudgementTypes).size(), 2);
  EXPECT_EQ(store.ListObjects(ObjectType::kLanguages).size(), 1);
  EXPECT_EQ(store.ListObjects(ObjectType::kProblems).size(), 2);
  EXPECT_EQ(store.ListObjects(ObjectType::kGroups).size(), 1);
  EXPECT_EQ(store.ListObjects(ObjectType::kOrganizations).size(), 1);
  EXPECT_EQ(store.ListObjects(ObjectType::kTeams).size(), 2);
  EXPECT_EQ(store.ListObjects(ObjectType::kPersons).size(), 1);
  EXPECT_EQ(store.ListObjects(ObjectType::kAccounts).size(), 1);
  EXPECT_EQ(store.ListObjects(ObjectType::kSubmissions).size(), 1);
  EXPECT_EQ(store.ListObjects(ObjectType::kJudgements).size(), 1);
  EXPECT_EQ(store.ListObjects(ObjectType::kRuns).size(), 1);
  EXPECT_EQ(store.ListObjects(ObjectType::kClarifications).size(), 1);
  EXPECT_EQ(store.ListObjects(ObjectType::kAwards).size(), 1);
  EXPECT_EQ(store.ListObjects(ObjectType::kCommentary).size(), 1);

  // Resolved version
  EXPECT_EQ(session->resolved_version(), ApiVersion::k2023_06);
}

// =====================================================================
// clarification_to_team_id
// =====================================================================

TEST_F(Spec202306Test, clarification_to_team_id) {
  auto session = MakeSession();
  session->ConsumeLine(
      R"({"type":"clarifications","id":"c1","data":{"id":"c1","from_team_id":"t1","to_team_id":"t42","problem_id":"A","text":"hello"}})");

  auto objs = session->store().ListObjects(ObjectType::kClarifications);
  ASSERT_EQ(objs.size(), 1);
  const auto* clar = dynamic_cast<const Clarification*>(objs[0]);
  ASSERT_NE(clar, nullptr);

  // 2023-06: singular to_team_id is normalized to to_team_ids vector
  ASSERT_EQ(clar->to_team_ids.size(), 1);
  EXPECT_EQ(clar->to_team_ids[0], "t42");
  EXPECT_EQ(clar->from_team_id.value_or(""), "t1");
}

// =====================================================================
// penalty_time_integer
// =====================================================================

TEST_F(Spec202306Test, penalty_time_integer) {
  auto session = MakeSession();
  session->ConsumeLine(
      R"({"type":"contest","data":{"id":"c1","penalty_time":20}})");

  const Contest* c = session->store().GetContest();
  ASSERT_NE(c, nullptr);
  ASSERT_TRUE(c->penalty_time.has_value());
  // 20 minutes = 20 * 60 * 1000 = 1,200,000 ms
  EXPECT_EQ(c->penalty_time->milliseconds, 1'200'000);
}

// =====================================================================
// token_tracking
// =====================================================================

TEST_F(Spec202306Test, token_tracking) {
  auto session = MakeSession();
  session->ConsumeLine(
      R"({"type":"contest","data":{"id":"c1","name":"Test"},"token":"tok-abc"})");
  session->ConsumeLine(
      R"({"type":"problems","id":"A","data":{"id":"A","label":"A"},"token":"tok-def"})");

  const auto& cursor = session->cursor();
  EXPECT_TRUE(cursor.last_token.has_value());
  EXPECT_EQ(cursor.last_token.value(), "tok-def");
  EXPECT_EQ(cursor.event_count, 2);
}

// =====================================================================
// keepalive_handling
// =====================================================================

TEST_F(Spec202306Test, keepalive_handling) {
  auto session = MakeSession();
  session->ConsumeLine(
      R"({"type":"contest","data":{"id":"c1","name":"Test"},"token":"t1"})");
  // Empty lines are keepalives and should be skipped.
  session->ConsumeLine("");
  session->ConsumeLine("");
  session->ConsumeLine(
      R"({"type":"problems","id":"A","data":{"id":"A","label":"A"},"token":"t2"})");

  const auto& store = session->store();
  ASSERT_NE(store.GetContest(), nullptr);
  EXPECT_EQ(store.ListObjects(ObjectType::kProblems).size(), 1);
  // Only real events should be counted; keepalives are ignored.
  EXPECT_EQ(session->cursor().event_count, 2);
}

// =====================================================================
// end_of_updates
// =====================================================================

TEST_F(Spec202306Test, end_of_updates) {
  auto session = MakeSession();
  session->ConsumeLine(R"({"type":"state","data":{}})");
  EXPECT_FALSE(session->cursor().end_of_updates);

  session->ConsumeLine(
      R"({"type":"state","data":{"started":"2025-01-01T10:00:00Z","end_of_updates":"2025-01-01T15:00:00Z"}})");

  EXPECT_TRUE(session->cursor().end_of_updates);

  const State* st = session->store().GetState();
  ASSERT_NE(st, nullptr);
  EXPECT_TRUE(st->end_of_updates.has_value());
}

// =====================================================================
// duplicate_event
// =====================================================================

TEST_F(Spec202306Test, duplicate_event) {
  auto session = MakeSession();
  const std::string line =
      R"({"type":"problems","id":"A","data":{"id":"A","label":"A","name":"Apples"},"token":"t1"})";

  auto s1 = session->ConsumeLine(line);
  ASSERT_TRUE(s1.ok());
  auto s2 = session->ConsumeLine(line);
  // Same event twice should not cause an error in Continue mode.
  EXPECT_TRUE(s2.ok());

  // The store should contain exactly one problem (upsert is idempotent).
  EXPECT_EQ(session->store().ListObjects(ObjectType::kProblems).size(), 1);
}

// =====================================================================
// unknown_field_preserve
// =====================================================================

TEST_F(Spec202306Test, unknown_field_preserve) {
  ParseOptions opts;
  opts.unknown_field_policy = UnknownFieldPolicy::kPreserve;
  auto session = MakeSession(opts);

  session->ConsumeLine(
      R"({"type":"problems","id":"A","data":{"id":"A","label":"A","custom_ext":"foobar","another":123}})");

  auto objs = session->store().ListObjects(ObjectType::kProblems);
  ASSERT_EQ(objs.size(), 1);
  const auto* prob = dynamic_cast<const Problem*>(objs[0]);
  ASSERT_NE(prob, nullptr);

  // Unknown fields should be preserved in the object.
  auto it = prob->unknown_fields.find("custom_ext");
  ASSERT_NE(it, prob->unknown_fields.end());
  EXPECT_NE(it->second.find("foobar"), std::string::npos);
}

// =====================================================================
// malformed_line_continue
// =====================================================================

TEST_F(Spec202306Test, malformed_line_continue) {
  ParseOptions opts;
  opts.error_policy = ErrorPolicy::kContinue;
  auto session = MakeSession(opts);

  session->ConsumeLine(
      R"({"type":"problems","id":"A","data":{"id":"A","label":"A","name":"Apples"}})");
  // Malformed line
  session->ConsumeLine("this is not json at all");
  // Valid line after bad one should still succeed.
  session->ConsumeLine(
      R"({"type":"problems","id":"B","data":{"id":"B","label":"B","name":"Bananas"}})");

  EXPECT_EQ(session->store().ListObjects(ObjectType::kProblems).size(), 2);
  // There should be at least one diagnostic for the bad line.
  EXPECT_GE(session->diagnostics().size(), 1);
  bool found_error = false;
  for (const auto& d : session->diagnostics()) {
    if (d.severity == Severity::kError) {
      found_error = true;
      break;
    }
  }
  EXPECT_TRUE(found_error);
}

// =====================================================================
// malformed_line_failfast
// =====================================================================

TEST_F(Spec202306Test, malformed_line_failfast) {
  ParseOptions opts;
  opts.error_policy = ErrorPolicy::kFailFast;
  auto session = MakeSession(opts);

  session->ConsumeLine(
      R"({"type":"problems","id":"A","data":{"id":"A","label":"A","name":"Apples"}})");

  auto s = session->ConsumeLine("this is not json at all");
  // FailFast should report an error on the bad line.
  EXPECT_FALSE(s.ok());

  // The first problem should still be in the store.
  EXPECT_EQ(session->store().ListObjects(ObjectType::kProblems).size(), 1);
}

// =====================================================================
// collection_replace
// =====================================================================

TEST_F(Spec202306Test, collection_replace) {
  auto session = MakeSession();

  // Add initial teams.
  session->ConsumeLine(
      R"({"type":"teams","id":"t1","data":{"id":"t1","name":"Old Team 1"}})");
  session->ConsumeLine(
      R"({"type":"teams","id":"t2","data":{"id":"t2","name":"Old Team 2"}})");
  EXPECT_EQ(session->store().ListObjects(ObjectType::kTeams).size(), 2);

  // Atomic collection replace (id is null, data is array).
  session->ConsumeLine(
      R"({"type":"teams","id":null,"data":[{"id":"t3","name":"New Team 3"},{"id":"t4","name":"New Team 4"},{"id":"t5","name":"New Team 5"}]})");

  auto teams = session->store().ListObjects(ObjectType::kTeams);
  EXPECT_EQ(teams.size(), 3);

  // Old teams should be gone.
  auto old = session->store().GetObject(ObjectType::kTeams, "t1");
  EXPECT_FALSE(old.ok());
  auto old2 = session->store().GetObject(ObjectType::kTeams, "t2");
  EXPECT_FALSE(old2.ok());

  // New teams should exist.
  auto new3 = session->store().GetObject(ObjectType::kTeams, "t3");
  ASSERT_TRUE(new3.ok());
}

// =====================================================================
// singleton_update
// =====================================================================

TEST_F(Spec202306Test, singleton_update) {
  auto session = MakeSession();

  // Initial state (empty).
  session->ConsumeLine(R"({"type":"state","data":{}})");
  const State* st = session->store().GetState();
  ASSERT_NE(st, nullptr);
  EXPECT_FALSE(st->started.has_value());

  // Update state with started time.
  session->ConsumeLine(
      R"({"type":"state","data":{"started":"2025-01-01T10:00:00.000+00:00"}})");
  st = session->store().GetState();
  ASSERT_NE(st, nullptr);
  EXPECT_TRUE(st->started.has_value());

  // Contest singleton update.
  session->ConsumeLine(
      R"({"type":"contest","data":{"id":"c1","name":"Initial"}})");
  ASSERT_NE(session->store().GetContest(), nullptr);
  EXPECT_EQ(session->store().GetContest()->name.value_or(""), "Initial");

  session->ConsumeLine(
      R"({"type":"contest","data":{"id":"c1","name":"Updated"}})");
  EXPECT_EQ(session->store().GetContest()->name.value_or(""), "Updated");
}

}  // namespace
}  // namespace ccsparser
