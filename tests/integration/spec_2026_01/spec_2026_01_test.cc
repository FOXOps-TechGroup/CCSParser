// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

// Integration tests for CCS Spec 2026-01.
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

class Spec202601Test : public ::testing::Test {
 protected:
  std::unique_ptr<StreamingParseSession> MakeSession(
      ParseOptions opts = {}) {
    opts.version = ApiVersion::k2026_01;
    if (opts.error_policy == ErrorPolicy::kContinue) {
      // default already
    }
    opts.keep_event_log = true;
    opts.unknown_field_policy = UnknownFieldPolicy::kPreserve;
    auto s = EventFeedParser::CreateStreamingSession(opts);
    EXPECT_TRUE(s.ok()) << s.status().ToString();
    return std::move(s.value());
  }

  void FeedLines(StreamingParseSession& session,
                 const std::vector<std::string>& lines) {
    for (const auto& line : lines) {
      auto s = session.ConsumeLine(line);
      EXPECT_TRUE(s.ok()) << "Line failed: " << line
                          << " — " << s.ToString();
    }
  }

  // A complete 2026-01 feed covering the main object types.
  std::vector<std::string> FullFeed() {
    return {
        R"({"type":"state","data":{},"token":"u0"})",
        R"({"type":"contest","id":"demo26","data":{"id":"demo26","name":"Demo Contest 2026","formal_name":"Demo 2026-01","start_time":"2026-01-15T09:00:00.000+00:00","duration":"5:00:00.000","scoreboard_freeze_duration":"1:00:00.000","scoreboard_type":"pass-fail","penalty_time":"0:20:00"},"token":"u1"})",
        R"({"type":"judgement-types","id":"AC","data":{"id":"AC","name":"correct","penalty":false,"solved":true},"token":"u2"})",
        R"({"type":"judgement-types","id":"WA","data":{"id":"WA","name":"wrong answer","penalty":true,"solved":false,"simplified_judgement_type_id":"WA"},"token":"u3"})",
        R"({"type":"languages","id":"python","data":{"id":"python","name":"Python 3","extensions":["py"]},"token":"u4"})",
        R"({"type":"problems","id":"X","data":{"id":"X","ordinal":1,"label":"X","name":"Problem X","rgb":"#0000ff","time_limit":5,"memory_limit":256,"output_limit":64,"code_limit":128},"token":"u5"})",
        R"({"type":"groups","id":"participants","data":{"id":"participants","name":"Participants"},"token":"u6"})",
        R"({"type":"organizations","id":"org2","data":{"id":"org2","name":"University B","country":"GBR","country_subdivision":"ENG"},"token":"u7"})",
        R"({"type":"teams","id":"team3","data":{"id":"team3","name":"Team Gamma","organization_id":"org2","group_ids":["participants"]},"token":"u8"})",
        R"({"type":"submissions","id":"s2","data":{"id":"s2","language_id":"python","problem_id":"X","team_id":"team3","account_id":"user3","time":"2026-01-15T09:30:00.000+00:00","contest_time":"0:30:00.000"},"token":"u9"})",
        R"({"type":"judgements","id":"j2","data":{"id":"j2","submission_id":"s2","judgement_type_id":"AC","simplified_judgement_type_id":"AC","start_time":"2026-01-15T09:30:01.000+00:00","start_contest_time":"0:30:01.000","end_time":"2026-01-15T09:30:03.000+00:00","end_contest_time":"0:30:03.000","current":true},"token":"u10"})",
        R"({"type":"clarifications","id":"c2","data":{"id":"c2","from_team_id":"team3","to_team_ids":["team3"],"to_group_ids":["participants"],"problem_id":"X","time":"2026-01-15T09:35:00.000+00:00","contest_time":"0:35:00.000","text":"Clarification question"},"token":"u11"})",
        R"({"type":"awards","id":"honors","data":{"id":"honors","citation":"Honors","team_ids":["team3"]},"token":"u12"})",
        R"({"type":"awards","id":"high-honors","data":{"id":"high-honors","citation":"High Honors","team_ids":["team3"]},"token":"u13"})",
        R"({"type":"awards","id":"highest-honors","data":{"id":"highest-honors","citation":"Highest Honors","team_ids":["team3"]},"token":"u14"})",
        R"({"type":"commentary","id":"com1","data":{"id":"com1","time":"2026-01-15T09:31:00.000+00:00","contest_time":"0:31:00.000","message":"Team Gamma solved X!","team_ids":["team3"],"problem_ids":["X"]},"token":"u15"})",
        R"({"type":"state","data":{"started":"2026-01-15T09:00:00.000+00:00","ended":"2026-01-15T14:00:00.000+00:00","finalized":"2026-01-15T14:01:00.000+00:00","end_of_updates":"2026-01-15T14:02:00.000+00:00"},"token":"u16"})",
    };
  }
};

// =====================================================================
// clarification_to_team_ids (array form)
// =====================================================================

TEST_F(Spec202601Test, clarification_to_team_ids) {
  auto session = MakeSession();
  session->ConsumeLine(
      R"({"type":"clarifications","id":"c1","data":{"id":"c1","from_team_id":"t1","to_team_ids":["t2","t3"],"to_group_ids":["participants","spectators"],"text":"hello"}})");

  auto objs = session->store().ListObjects(ObjectType::kClarifications);
  ASSERT_EQ(objs.size(), 1);
  const auto* clar = dynamic_cast<const Clarification*>(objs[0]);
  ASSERT_NE(clar, nullptr);

  // 2026-01: to_team_ids is a native array field.
  ASSERT_EQ(clar->to_team_ids.size(), 2);
  EXPECT_EQ(clar->to_team_ids[0], "t2");
  EXPECT_EQ(clar->to_team_ids[1], "t3");

  // 2026-01: to_group_ids is a new array field.
  ASSERT_EQ(clar->to_group_ids.size(), 2);
  EXPECT_EQ(clar->to_group_ids[0], "participants");
  EXPECT_EQ(clar->to_group_ids[1], "spectators");
}

// =====================================================================
// penalty_time_reltime
// =====================================================================

TEST_F(Spec202601Test, penalty_time_reltime) {
  auto session = MakeSession();
  session->ConsumeLine(
      R"({"type":"contest","data":{"id":"c1","penalty_time":"0:20:00"}})");

  const Contest* c = session->store().GetContest();
  ASSERT_NE(c, nullptr);
  ASSERT_TRUE(c->penalty_time.has_value());
  // "0:20:00" = 20 min = 1,200,000 ms
  EXPECT_EQ(c->penalty_time->milliseconds, 1'200'000);
}

// =====================================================================
// honors_awards
// =====================================================================

TEST_F(Spec202601Test, honors_awards) {
  auto session = MakeSession();
  session->ConsumeLine(
      R"({"type":"awards","id":"honors","data":{"id":"honors","citation":"Honors","team_ids":["t1"]}})");
  session->ConsumeLine(
      R"({"type":"awards","id":"high-honors","data":{"id":"high-honors","citation":"High Honors","team_ids":["t1","t2"]}})");
  session->ConsumeLine(
      R"({"type":"awards","id":"highest-honors","data":{"id":"highest-honors","citation":"Highest Honors","team_ids":["t1"]}})");

  auto awards = session->store().ListObjects(ObjectType::kAwards);
  EXPECT_EQ(awards.size(), 3);

  // Verify each award by lookup.
  auto honors = session->store().GetObject(ObjectType::kAwards, "honors");
  ASSERT_TRUE(honors.ok());
  EXPECT_EQ(dynamic_cast<const Award*>(honors.value())->citation.value_or(""),
            "Honors");

  auto high = session->store().GetObject(ObjectType::kAwards, "high-honors");
  ASSERT_TRUE(high.ok());
  EXPECT_EQ(dynamic_cast<const Award*>(high.value())->team_ids.size(), 2);

  auto highest =
      session->store().GetObject(ObjectType::kAwards, "highest-honors");
  ASSERT_TRUE(highest.ok());
  EXPECT_EQ(
      dynamic_cast<const Award*>(highest.value())->citation.value_or(""),
      "Highest Honors");
}

// =====================================================================
// commentary_compat
// =====================================================================

TEST_F(Spec202601Test, commentary_compat) {
  auto session = MakeSession();
  session->ConsumeLine(
      R"({"type":"commentary","id":"com1","data":{"id":"com1","time":"2026-01-15T09:31:00Z","contest_time":"0:31:00.000","message":"Great solve!","team_ids":["t1"],"problem_ids":["X"],"submission_ids":["s2"]}})");

  auto objs = session->store().ListObjects(ObjectType::kCommentary);
  ASSERT_EQ(objs.size(), 1);
  const auto* comm = dynamic_cast<const Commentary*>(objs[0]);
  ASSERT_NE(comm, nullptr);
  EXPECT_EQ(comm->message.value_or(""), "Great solve!");
  EXPECT_EQ(comm->team_ids.size(), 1);
  EXPECT_EQ(comm->problem_ids.size(), 1);
  EXPECT_EQ(comm->submission_ids.size(), 1);
  EXPECT_EQ(comm->submission_ids[0], "s2");
}

// =====================================================================
// token_tracking
// =====================================================================

TEST_F(Spec202601Test, token_tracking) {
  auto session = MakeSession();
  session->ConsumeLine(
      R"({"type":"contest","data":{"id":"c1"},"token":"tok-001"})");
  session->ConsumeLine(
      R"({"type":"problems","id":"X","data":{"id":"X","label":"X"},"token":"tok-002"})");

  const auto& cursor = session->cursor();
  EXPECT_TRUE(cursor.last_token.has_value());
  EXPECT_EQ(cursor.last_token.value(), "tok-002");
  EXPECT_EQ(cursor.event_count, 2);
}

// =====================================================================
// keepalive_handling
// =====================================================================

TEST_F(Spec202601Test, keepalive_handling) {
  auto session = MakeSession();
  session->ConsumeLine(
      R"({"type":"contest","data":{"id":"c1"},"token":"u1"})");
  session->ConsumeLine("");
  session->ConsumeLine("");
  session->ConsumeLine(
      R"({"type":"problems","id":"X","data":{"id":"X","label":"X"},"token":"u2"})");

  EXPECT_EQ(session->cursor().event_count, 2);
  EXPECT_EQ(session->store().ListObjects(ObjectType::kProblems).size(), 1);
}

// =====================================================================
// end_of_updates
// =====================================================================

TEST_F(Spec202601Test, end_of_updates) {
  auto session = MakeSession();
  session->ConsumeLine(R"({"type":"state","data":{}})");
  EXPECT_FALSE(session->cursor().end_of_updates);

  session->ConsumeLine(
      R"({"type":"state","data":{"started":"2026-01-15T09:00:00Z","end_of_updates":"2026-01-15T14:00:00Z"}})");
  EXPECT_TRUE(session->cursor().end_of_updates);

  const State* st = session->store().GetState();
  ASSERT_NE(st, nullptr);
  EXPECT_TRUE(st->end_of_updates.has_value());
}

// =====================================================================
// duplicate_event
// =====================================================================

TEST_F(Spec202601Test, duplicate_event) {
  auto session = MakeSession();
  const std::string line =
      R"({"type":"problems","id":"X","data":{"id":"X","label":"X","name":"Problem X"},"token":"u1"})";

  EXPECT_TRUE(session->ConsumeLine(line).ok());
  EXPECT_TRUE(session->ConsumeLine(line).ok());

  EXPECT_EQ(session->store().ListObjects(ObjectType::kProblems).size(), 1);
}

// =====================================================================
// unknown_field_preserve
// =====================================================================

TEST_F(Spec202601Test, unknown_field_preserve) {
  ParseOptions opts;
  opts.unknown_field_policy = UnknownFieldPolicy::kPreserve;
  auto session = MakeSession(opts);

  session->ConsumeLine(
      R"({"type":"problems","id":"X","data":{"id":"X","label":"X","future_feature":"value42"}})");

  auto objs = session->store().ListObjects(ObjectType::kProblems);
  ASSERT_EQ(objs.size(), 1);
  const auto* prob = dynamic_cast<const Problem*>(objs[0]);
  ASSERT_NE(prob, nullptr);
  auto it = prob->unknown_fields.find("future_feature");
  ASSERT_NE(it, prob->unknown_fields.end());
  EXPECT_NE(it->second.find("value42"), std::string::npos);
}

// =====================================================================
// malformed_line_continue
// =====================================================================

TEST_F(Spec202601Test, malformed_line_continue) {
  ParseOptions opts;
  opts.error_policy = ErrorPolicy::kContinue;
  auto session = MakeSession(opts);

  session->ConsumeLine(
      R"({"type":"problems","id":"X","data":{"id":"X","label":"X"}})");
  session->ConsumeLine("NOT JSON");
  session->ConsumeLine(
      R"({"type":"problems","id":"Y","data":{"id":"Y","label":"Y"}})");

  EXPECT_EQ(session->store().ListObjects(ObjectType::kProblems).size(), 2);
  EXPECT_GE(session->diagnostics().size(), 1);
}

// =====================================================================
// malformed_line_failfast
// =====================================================================

TEST_F(Spec202601Test, malformed_line_failfast) {
  ParseOptions opts;
  opts.error_policy = ErrorPolicy::kFailFast;
  auto session = MakeSession(opts);

  session->ConsumeLine(
      R"({"type":"problems","id":"X","data":{"id":"X","label":"X"}})");
  auto s = session->ConsumeLine("NOT JSON");
  EXPECT_FALSE(s.ok());

  EXPECT_EQ(session->store().ListObjects(ObjectType::kProblems).size(), 1);
}

// =====================================================================
// collection_replace
// =====================================================================

TEST_F(Spec202601Test, collection_replace) {
  auto session = MakeSession();

  session->ConsumeLine(
      R"({"type":"teams","id":"t1","data":{"id":"t1","name":"Old"}})");
  EXPECT_EQ(session->store().ListObjects(ObjectType::kTeams).size(), 1);

  session->ConsumeLine(
      R"({"type":"teams","id":null,"data":[{"id":"t2","name":"A"},{"id":"t3","name":"B"}]})");

  EXPECT_EQ(session->store().ListObjects(ObjectType::kTeams).size(), 2);
  EXPECT_FALSE(session->store().GetObject(ObjectType::kTeams, "t1").ok());
  EXPECT_TRUE(session->store().GetObject(ObjectType::kTeams, "t2").ok());
}

// =====================================================================
// singleton_update
// =====================================================================

TEST_F(Spec202601Test, singleton_update) {
  auto session = MakeSession();
  session->ConsumeLine(R"({"type":"state","data":{}})");
  EXPECT_FALSE(session->store().GetState()->started.has_value());

  session->ConsumeLine(
      R"({"type":"state","data":{"started":"2026-01-15T09:00:00Z"}})");
  EXPECT_TRUE(session->store().GetState()->started.has_value());

  session->ConsumeLine(
      R"({"type":"contest","data":{"id":"c1","name":"V1"}})");
  EXPECT_EQ(session->store().GetContest()->name.value_or(""), "V1");

  session->ConsumeLine(
      R"({"type":"contest","data":{"id":"c1","name":"V2"}})");
  EXPECT_EQ(session->store().GetContest()->name.value_or(""), "V2");
}

// =====================================================================
// Full feed (basic_object_flow equivalent for 2026-01)
// =====================================================================

TEST_F(Spec202601Test, full_feed_2026_01) {
  auto session = MakeSession();
  FeedLines(*session, FullFeed());
  auto s = session->Finish();
  ASSERT_TRUE(s.ok()) << s.ToString();

  const auto& store = session->store();
  ASSERT_NE(store.GetContest(), nullptr);
  EXPECT_EQ(store.GetContest()->name.value_or(""), "Demo Contest 2026");
  ASSERT_NE(store.GetState(), nullptr);
  EXPECT_TRUE(store.GetState()->end_of_updates.has_value());

  EXPECT_EQ(store.ListObjects(ObjectType::kJudgementTypes).size(), 2);
  EXPECT_EQ(store.ListObjects(ObjectType::kLanguages).size(), 1);
  EXPECT_EQ(store.ListObjects(ObjectType::kProblems).size(), 1);
  EXPECT_EQ(store.ListObjects(ObjectType::kGroups).size(), 1);
  EXPECT_EQ(store.ListObjects(ObjectType::kOrganizations).size(), 1);
  EXPECT_EQ(store.ListObjects(ObjectType::kTeams).size(), 1);
  EXPECT_EQ(store.ListObjects(ObjectType::kSubmissions).size(), 1);
  EXPECT_EQ(store.ListObjects(ObjectType::kJudgements).size(), 1);
  EXPECT_EQ(store.ListObjects(ObjectType::kClarifications).size(), 1);
  EXPECT_EQ(store.ListObjects(ObjectType::kAwards).size(), 3);
  EXPECT_EQ(store.ListObjects(ObjectType::kCommentary).size(), 1);

  EXPECT_EQ(session->resolved_version(), ApiVersion::k2026_01);
  EXPECT_TRUE(session->cursor().end_of_updates);
}

// =====================================================================
// 2026-01 specific fields
// =====================================================================

TEST_F(Spec202601Test, problem_2026_fields) {
  auto session = MakeSession();
  session->ConsumeLine(
      R"({"type":"problems","id":"X","data":{"id":"X","ordinal":1,"label":"X","name":"Problem X","memory_limit":256,"output_limit":64,"code_limit":128}})");

  auto objs = session->store().ListObjects(ObjectType::kProblems);
  ASSERT_EQ(objs.size(), 1);
  const auto* prob = dynamic_cast<const Problem*>(objs[0]);
  ASSERT_NE(prob, nullptr);
  EXPECT_EQ(prob->memory_limit.value_or(0), 256);
  EXPECT_EQ(prob->output_limit.value_or(0), 64);
  EXPECT_EQ(prob->code_limit.value_or(0), 128);
}

TEST_F(Spec202601Test, judgement_type_simplified_id) {
  auto session = MakeSession();
  session->ConsumeLine(
      R"({"type":"judgement-types","id":"WA","data":{"id":"WA","name":"wrong answer","penalty":true,"solved":false,"simplified_judgement_type_id":"WA"}})");

  auto objs = session->store().ListObjects(ObjectType::kJudgementTypes);
  ASSERT_EQ(objs.size(), 1);
  const auto* jt = dynamic_cast<const JudgementType*>(objs[0]);
  ASSERT_NE(jt, nullptr);
  EXPECT_EQ(jt->simplified_judgement_type_id.value_or(""), "WA");
}

TEST_F(Spec202601Test, judgement_current_field) {
  auto session = MakeSession();
  session->ConsumeLine(
      R"({"type":"judgements","id":"j1","data":{"id":"j1","submission_id":"s1","judgement_type_id":"AC","current":true}})");

  auto objs = session->store().ListObjects(ObjectType::kJudgements);
  ASSERT_EQ(objs.size(), 1);
  const auto* j = dynamic_cast<const Judgement*>(objs[0]);
  ASSERT_NE(j, nullptr);
  EXPECT_EQ(j->current.value_or(false), true);
}

TEST_F(Spec202601Test, organization_country_subdivision) {
  auto session = MakeSession();
  session->ConsumeLine(
      R"({"type":"organizations","id":"org1","data":{"id":"org1","name":"U of Oxford","country":"GBR","country_subdivision":"ENG"}})");

  auto objs = session->store().ListObjects(ObjectType::kOrganizations);
  ASSERT_EQ(objs.size(), 1);
  const auto* org = dynamic_cast<const Organization*>(objs[0]);
  ASSERT_NE(org, nullptr);
  EXPECT_EQ(org->country_subdivision.value_or(""), "ENG");
}

TEST_F(Spec202601Test, submission_account_id) {
  auto session = MakeSession();
  session->ConsumeLine(
      R"({"type":"submissions","id":"s1","data":{"id":"s1","language_id":"python","problem_id":"X","team_id":"t1","account_id":"user1","time":"2026-01-15T09:30:00Z","contest_time":"0:30:00"}})");

  auto objs = session->store().ListObjects(ObjectType::kSubmissions);
  ASSERT_EQ(objs.size(), 1);
  const auto* sub = dynamic_cast<const Submission*>(objs[0]);
  ASSERT_NE(sub, nullptr);
  EXPECT_EQ(sub->account_id.value_or(""), "user1");
}

}  // namespace
}  // namespace ccsparser
