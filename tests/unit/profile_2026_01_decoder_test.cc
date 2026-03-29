// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

// Tests for the 2026-01 profile decoder.
// Tests focus on 2026-01 specific features: RELTIME penalty_time, to_team_ids
// array in clarifications, new fields, and version detection from string
// penalty_time.

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "ccsparser/objects.h"
#include "ccsparser/parse_options.h"
#include "ccsparser/parser.h"
#include "ccsparser/status.h"
#include "ccsparser/types.h"

namespace ccsparser {
namespace {

std::unique_ptr<StreamingParseSession> MakeSession(ParseOptions opts = {}) {
  opts.version = ApiVersion::k2026_01;
  opts.error_policy = ErrorPolicy::kContinue;
  auto s = EventFeedParser::CreateStreamingSession(opts);
  EXPECT_TRUE(s.ok());
  return std::move(s.value());
}

// =====================================================================
// Contest decoding (2026-01 specifics)
// =====================================================================

TEST(Profile2026DecoderTest, ContestPenaltyTimeReltime) {
  auto session = MakeSession();
  session->ConsumeLine(R"({"type":"contest","data":{
    "id":"wf2026",
    "name":"ICPC World Finals 2026",
    "penalty_time":"0:20:00",
    "duration":"5:00:00",
    "main_scoreboard_group_id":"global"
  }})");

  const Contest* c = session->store().GetContest();
  ASSERT_NE(c, nullptr);
  EXPECT_EQ(c->id, "wf2026");
  ASSERT_TRUE(c->penalty_time.has_value());
  EXPECT_EQ(c->penalty_time->milliseconds, 1'200'000);
  EXPECT_EQ(c->main_scoreboard_group_id.value_or(""), "global");
}

TEST(Profile2026DecoderTest, ContestZeroPenaltyTime) {
  auto session = MakeSession();
  session->ConsumeLine(
      R"({"type":"contest","data":{"id":"c1","penalty_time":"0:00:00"}})");

  const Contest* c = session->store().GetContest();
  ASSERT_NE(c, nullptr);
  ASSERT_TRUE(c->penalty_time.has_value());
  EXPECT_EQ(c->penalty_time->milliseconds, 0);
}

TEST(Profile2026DecoderTest, ContestPenaltyTimeWithFraction) {
  auto session = MakeSession();
  session->ConsumeLine(
      R"({"type":"contest","data":{"id":"c1","penalty_time":"0:20:00.500"}})");

  const Contest* c = session->store().GetContest();
  ASSERT_NE(c, nullptr);
  ASSERT_TRUE(c->penalty_time.has_value());
  EXPECT_EQ(c->penalty_time->milliseconds, 1'200'500);
}

// =====================================================================
// Clarification decoding (2026-01 to_team_ids array)
// =====================================================================

TEST(Profile2026DecoderTest, ClarificationToTeamIdsArray) {
  auto session = MakeSession();
  session->ConsumeLine(
      R"({"type":"clarifications","id":"clar1","data":{"id":"clar1","from_team_id":"t1","to_team_ids":["t2","t3"],"problem_id":"A","text":"Question","time":"2026-01-01T10:00:00Z","contest_time":"1:00:00"}})");

  auto objs = session->store().ListObjects(ObjectType::kClarifications);
  ASSERT_EQ(objs.size(), 1);
  const auto* clar = dynamic_cast<const Clarification*>(objs[0]);
  ASSERT_NE(clar, nullptr);
  EXPECT_EQ(clar->to_team_ids.size(), 2);
  EXPECT_EQ(clar->to_team_ids[0], "t2");
  EXPECT_EQ(clar->to_team_ids[1], "t3");
}

TEST(Profile2026DecoderTest, ClarificationToGroupIds) {
  auto session = MakeSession();
  session->ConsumeLine(
      R"({"type":"clarifications","id":"clar1","data":{"id":"clar1","to_group_ids":["g1","g2"],"text":"Announcement","time":"2026-01-01T10:00:00Z","contest_time":"1:00:00"}})");

  auto objs = session->store().ListObjects(ObjectType::kClarifications);
  ASSERT_EQ(objs.size(), 1);
  const auto* clar = dynamic_cast<const Clarification*>(objs[0]);
  ASSERT_NE(clar, nullptr);
  EXPECT_EQ(clar->to_group_ids.size(), 2);
  EXPECT_EQ(clar->to_group_ids[0], "g1");
  EXPECT_EQ(clar->to_group_ids[1], "g2");
}

TEST(Profile2026DecoderTest, ClarificationBackwardCompatToTeamId) {
  auto session = MakeSession();
  // 2026-01 should also handle to_team_id for backward compat.
  session->ConsumeLine(
      R"({"type":"clarifications","id":"clar1","data":{"id":"clar1","to_team_id":"t5","text":"old format","time":"2026-01-01T10:00:00Z","contest_time":"1:00:00"}})");

  auto objs = session->store().ListObjects(ObjectType::kClarifications);
  ASSERT_EQ(objs.size(), 1);
  const auto* clar = dynamic_cast<const Clarification*>(objs[0]);
  ASSERT_NE(clar, nullptr);
  ASSERT_GE(clar->to_team_ids.size(), 1);
  EXPECT_EQ(clar->to_team_ids[0], "t5");
}

// =====================================================================
// Problem decoding (2026-01 new fields)
// =====================================================================

TEST(Profile2026DecoderTest, ProblemNewFields) {
  auto session = MakeSession();
  session->ConsumeLine(
      R"({"type":"problems","id":"A","data":{"id":"A","ordinal":1,"label":"A","name":"Apples","memory_limit":256,"output_limit":8192,"code_limit":50000}})");

  auto objs = session->store().ListObjects(ObjectType::kProblems);
  ASSERT_EQ(objs.size(), 1);
  const auto* prob = dynamic_cast<const Problem*>(objs[0]);
  ASSERT_NE(prob, nullptr);
  EXPECT_EQ(prob->memory_limit.value_or(0), 256);
  EXPECT_EQ(prob->output_limit.value_or(0), 8192);
  EXPECT_EQ(prob->code_limit.value_or(0), 50000);
}

TEST(Profile2026DecoderTest, ProblemAttachments) {
  auto session = MakeSession();
  session->ConsumeLine(
      R"({"type":"problems","id":"A","data":{"id":"A","label":"A","attachments":[{"href":"problems/A/attachment.zip","mime":"application/zip","filename":"samples.zip"}]}})");

  auto objs = session->store().ListObjects(ObjectType::kProblems);
  ASSERT_EQ(objs.size(), 1);
  const auto* prob = dynamic_cast<const Problem*>(objs[0]);
  ASSERT_NE(prob, nullptr);
  ASSERT_EQ(prob->attachments.size(), 1);
  EXPECT_EQ(prob->attachments[0].href, "problems/A/attachment.zip");
  EXPECT_EQ(prob->attachments[0].filename.value_or(""), "samples.zip");
}

// =====================================================================
// Judgement decoding (2026-01 new fields)
// =====================================================================

TEST(Profile2026DecoderTest, JudgementCurrentField) {
  auto session = MakeSession();
  session->ConsumeLine(
      R"({"type":"judgements","id":"j1","data":{"id":"j1","submission_id":"s1","judgement_type_id":"AC","current":true,"start_time":"2026-01-01T11:00:00Z","start_contest_time":"2:00:00","end_time":"2026-01-01T11:00:05Z","end_contest_time":"2:00:05"}})");

  auto objs = session->store().ListObjects(ObjectType::kJudgements);
  ASSERT_EQ(objs.size(), 1);
  const auto* j = dynamic_cast<const Judgement*>(objs[0]);
  ASSERT_NE(j, nullptr);
  EXPECT_EQ(j->current.value_or(false), true);
}

TEST(Profile2026DecoderTest, JudgementSimplifiedType) {
  auto session = MakeSession();
  session->ConsumeLine(
      R"({"type":"judgements","id":"j1","data":{"id":"j1","submission_id":"s1","judgement_type_id":"custom-ac","simplified_judgement_type_id":"AC","start_time":"2026-01-01T11:00:00Z","start_contest_time":"2:00:00"}})");

  auto objs = session->store().ListObjects(ObjectType::kJudgements);
  ASSERT_EQ(objs.size(), 1);
  const auto* j = dynamic_cast<const Judgement*>(objs[0]);
  ASSERT_NE(j, nullptr);
  EXPECT_EQ(j->simplified_judgement_type_id.value_or(""), "AC");
}

// =====================================================================
// JudgementType (2026-01 simplified_judgement_type_id)
// =====================================================================

TEST(Profile2026DecoderTest, JudgementTypeSimplified) {
  auto session = MakeSession();
  session->ConsumeLine(
      R"({"type":"judgement-types","id":"custom-ac","data":{"id":"custom-ac","name":"Custom Accepted","penalty":false,"solved":true,"simplified_judgement_type_id":"AC"}})");

  auto objs = session->store().ListObjects(ObjectType::kJudgementTypes);
  ASSERT_EQ(objs.size(), 1);
  const auto* jt = dynamic_cast<const JudgementType*>(objs[0]);
  ASSERT_NE(jt, nullptr);
  EXPECT_EQ(jt->simplified_judgement_type_id.value_or(""), "AC");
}

// =====================================================================
// Submission decoding (2026-01 account_id)
// =====================================================================

TEST(Profile2026DecoderTest, SubmissionWithAccountId) {
  auto session = MakeSession();
  session->ConsumeLine(
      R"({"type":"submissions","id":"s1","data":{"id":"s1","language_id":"cpp","problem_id":"A","account_id":"a1","time":"2026-01-01T10:30:00Z","contest_time":"1:30:00"}})");

  auto objs = session->store().ListObjects(ObjectType::kSubmissions);
  ASSERT_EQ(objs.size(), 1);
  const auto* sub = dynamic_cast<const Submission*>(objs[0]);
  ASSERT_NE(sub, nullptr);
  EXPECT_EQ(sub->account_id.value_or(""), "a1");
  // team_id may be optional in 2026-01.
  EXPECT_FALSE(sub->team_id.has_value());
}

// =====================================================================
// Organization (2026-01 country_subdivision)
// =====================================================================

TEST(Profile2026DecoderTest, OrganizationCountrySubdivision) {
  auto session = MakeSession();
  session->ConsumeLine(
      R"({"type":"organizations","id":"org1","data":{"id":"org1","name":"MIT","country":"USA","country_subdivision":"MA"}})");

  auto objs = session->store().ListObjects(ObjectType::kOrganizations);
  ASSERT_EQ(objs.size(), 1);
  const auto* org = dynamic_cast<const Organization*>(objs[0]);
  ASSERT_NE(org, nullptr);
  EXPECT_EQ(org->country_subdivision.value_or(""), "MA");
}

// =====================================================================
// Version detection from string penalty_time
// =====================================================================

TEST(Profile2026DecoderTest, VersionDetectedFromPenaltyTimeString) {
  ParseOptions opts;
  opts.version = ApiVersion::kAuto;
  auto session_or = EventFeedParser::CreateStreamingSession(opts);
  ASSERT_TRUE(session_or.ok());
  auto& session = session_or.value();

  session->ConsumeLine(
      R"({"type":"contest","data":{"id":"c1","penalty_time":"0:20:00"}})");

  EXPECT_EQ(session->resolved_version(), ApiVersion::k2026_01);
}

TEST(Profile2026DecoderTest, VersionDetectedFromToTeamIds) {
  ParseOptions opts;
  opts.version = ApiVersion::kAuto;
  auto session_or = EventFeedParser::CreateStreamingSession(opts);
  ASSERT_TRUE(session_or.ok());
  auto& session = session_or.value();

  session->ConsumeLine(
      R"({"type":"clarifications","id":"clar1","data":{"id":"clar1","to_team_ids":["t1"],"text":"hi","time":"2026-01-01T10:00:00Z","contest_time":"1:00:00"}})");

  EXPECT_EQ(session->resolved_version(), ApiVersion::k2026_01);
}

// =====================================================================
// Full event sequence (2026-01 style)
// =====================================================================

TEST(Profile2026DecoderTest, FullEventSequence) {
  auto session = MakeSession();

  // Contest.
  session->ConsumeLine(
      R"({"type":"contest","data":{"id":"wf2026","name":"WF 2026","duration":"5:00:00","penalty_time":"0:20:00"}})");
  // Judgement types (collection replace).
  session->ConsumeLine(
      R"({"type":"judgement-types","data":[{"id":"AC","name":"Accepted","penalty":false,"solved":true},{"id":"WA","name":"Wrong Answer","penalty":true,"solved":false}]})");
  // Languages.
  session->ConsumeLine(
      R"({"type":"languages","id":"cpp","data":{"id":"cpp","name":"C++","extensions":["cpp","cc"]}})");
  // Problems.
  session->ConsumeLine(
      R"({"type":"problems","id":"A","data":{"id":"A","ordinal":1,"label":"A","name":"Apples","memory_limit":256}})");
  // Team.
  session->ConsumeLine(
      R"({"type":"teams","id":"t1","data":{"id":"t1","name":"Alpha","group_ids":["g1"]}})");
  // Submission.
  session->ConsumeLine(
      R"({"type":"submissions","id":"s1","data":{"id":"s1","language_id":"cpp","problem_id":"A","team_id":"t1","time":"2026-01-01T10:30:00Z","contest_time":"1:30:00"}})");
  // Judgement.
  session->ConsumeLine(
      R"({"type":"judgements","id":"j1","data":{"id":"j1","submission_id":"s1","judgement_type_id":"AC","current":true,"start_time":"2026-01-01T10:30:05Z","start_contest_time":"1:30:05","end_time":"2026-01-01T10:30:08Z","end_contest_time":"1:30:08"}})");
  // State.
  session->ConsumeLine(
      R"({"type":"state","data":{"started":"2026-01-01T09:00:00Z"}})");

  const auto& store = session->store();
  ASSERT_NE(store.GetContest(), nullptr);
  EXPECT_EQ(store.GetContest()->name.value_or(""), "WF 2026");
  EXPECT_EQ(store.ListObjects(ObjectType::kJudgementTypes).size(), 2);
  EXPECT_EQ(store.ListObjects(ObjectType::kLanguages).size(), 1);
  EXPECT_EQ(store.ListObjects(ObjectType::kProblems).size(), 1);
  EXPECT_EQ(store.ListObjects(ObjectType::kTeams).size(), 1);
  EXPECT_EQ(store.ListObjects(ObjectType::kSubmissions).size(), 1);
  EXPECT_EQ(store.ListObjects(ObjectType::kJudgements).size(), 1);
  ASSERT_NE(store.GetState(), nullptr);
  EXPECT_TRUE(store.GetState()->started.has_value());

  // Verify the problem has the 2026-01 field.
  auto prob_objs = store.ListObjects(ObjectType::kProblems);
  const auto* prob = dynamic_cast<const Problem*>(prob_objs[0]);
  EXPECT_EQ(prob->memory_limit.value_or(0), 256);

  EXPECT_EQ(session->cursor().event_count, 8);
  EXPECT_TRUE(session->diagnostics().empty());
}

// =====================================================================
// Integer penalty_time still works in 2026 mode
// =====================================================================

TEST(Profile2026DecoderTest, IntegerPenaltyTimeAlsoAccepted) {
  auto session = MakeSession();
  // Even in 2026-01 mode, integer penalty_time should still decode.
  session->ConsumeLine(
      R"({"type":"contest","data":{"id":"c1","penalty_time":20}})");

  const Contest* c = session->store().GetContest();
  ASSERT_NE(c, nullptr);
  ASSERT_TRUE(c->penalty_time.has_value());
  EXPECT_EQ(c->penalty_time->milliseconds, 1'200'000);
}

}  // namespace
}  // namespace ccsparser
