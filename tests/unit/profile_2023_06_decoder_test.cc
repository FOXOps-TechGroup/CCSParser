// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

// Tests for the 2023-06 profile decoder.
// We test through StreamingParseSession with version=k2023_06 and verify
// that JSON payloads are correctly decoded into typed objects.

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
  opts.version = ApiVersion::k2023_06;
  opts.error_policy = ErrorPolicy::kContinue;
  auto s = EventFeedParser::CreateStreamingSession(opts);
  EXPECT_TRUE(s.ok());
  return std::move(s.value());
}

// =====================================================================
// Contest decoding (2023-06)
// =====================================================================

TEST(Profile2023DecoderTest, ContestFullFields) {
  auto session = MakeSession();
  session->ConsumeLine(R"({"type":"contest","data":{
    "id":"wf2024",
    "name":"ICPC World Finals 2024",
    "formal_name":"The 48th Annual ICPC World Finals",
    "start_time":"2024-04-14T13:00:00+00:00",
    "duration":"5:00:00",
    "scoreboard_freeze_duration":"1:00:00",
    "scoreboard_type":"pass-fail",
    "penalty_time":20,
    "shortname":"wf2024",
    "allow_submit":true
  }})");

  const Contest* c = session->store().GetContest();
  ASSERT_NE(c, nullptr);
  EXPECT_EQ(c->id, "wf2024");
  EXPECT_EQ(c->name.value_or(""), "ICPC World Finals 2024");
  EXPECT_EQ(c->formal_name.value_or(""),
            "The 48th Annual ICPC World Finals");
  EXPECT_TRUE(c->start_time.has_value());
  ASSERT_TRUE(c->duration.has_value());
  EXPECT_EQ(c->duration->milliseconds, 18'000'000);
  ASSERT_TRUE(c->scoreboard_freeze_duration.has_value());
  EXPECT_EQ(c->scoreboard_freeze_duration->milliseconds, 3'600'000);
  EXPECT_EQ(c->scoreboard_type.value_or(""), "pass-fail");
  ASSERT_TRUE(c->penalty_time.has_value());
  EXPECT_EQ(c->penalty_time->milliseconds, 1'200'000);
  EXPECT_EQ(c->shortname.value_or(""), "wf2024");
  EXPECT_EQ(c->allow_submit.value_or(false), true);
}

TEST(Profile2023DecoderTest, ContestMinimalFields) {
  auto session = MakeSession();
  session->ConsumeLine(R"({"type":"contest","data":{"id":"c1"}})");

  const Contest* c = session->store().GetContest();
  ASSERT_NE(c, nullptr);
  EXPECT_EQ(c->id, "c1");
  EXPECT_FALSE(c->name.has_value());
  EXPECT_FALSE(c->duration.has_value());
  EXPECT_FALSE(c->penalty_time.has_value());
}

// =====================================================================
// JudgementType decoding
// =====================================================================

TEST(Profile2023DecoderTest, JudgementType) {
  auto session = MakeSession();
  session->ConsumeLine(
      R"({"type":"judgement-types","id":"AC","data":{"id":"AC","name":"Accepted","penalty":false,"solved":true}})");

  auto objs = session->store().ListObjects(ObjectType::kJudgementTypes);
  ASSERT_EQ(objs.size(), 1);
  const auto* jt = dynamic_cast<const JudgementType*>(objs[0]);
  ASSERT_NE(jt, nullptr);
  EXPECT_EQ(jt->id, "AC");
  EXPECT_EQ(jt->name.value_or(""), "Accepted");
  EXPECT_EQ(jt->penalty.value_or(true), false);
  EXPECT_EQ(jt->solved.value_or(false), true);
}

// =====================================================================
// Language decoding
// =====================================================================

TEST(Profile2023DecoderTest, Language) {
  auto session = MakeSession();
  session->ConsumeLine(
      R"({"type":"languages","id":"cpp","data":{"id":"cpp","name":"C++","extensions":["cpp","cc","cxx"],"entry_point_required":false}})");

  auto objs = session->store().ListObjects(ObjectType::kLanguages);
  ASSERT_EQ(objs.size(), 1);
  const auto* lang = dynamic_cast<const Language*>(objs[0]);
  ASSERT_NE(lang, nullptr);
  EXPECT_EQ(lang->id, "cpp");
  EXPECT_EQ(lang->name.value_or(""), "C++");
  EXPECT_EQ(lang->extensions.size(), 3);
  EXPECT_EQ(lang->extensions[0], "cpp");
  EXPECT_EQ(lang->entry_point_required.value_or(true), false);
}

// =====================================================================
// Problem decoding
// =====================================================================

TEST(Profile2023DecoderTest, Problem) {
  auto session = MakeSession();
  session->ConsumeLine(
      R"({"type":"problems","id":"A","data":{"id":"A","ordinal":1,"label":"A","name":"Apples","rgb":"#FF0000","time_limit":10,"test_data_count":25}})");

  auto objs = session->store().ListObjects(ObjectType::kProblems);
  ASSERT_EQ(objs.size(), 1);
  const auto* prob = dynamic_cast<const Problem*>(objs[0]);
  ASSERT_NE(prob, nullptr);
  EXPECT_EQ(prob->id, "A");
  EXPECT_EQ(prob->ordinal.value_or(0), 1);
  EXPECT_EQ(prob->label.value_or(""), "A");
  EXPECT_EQ(prob->name.value_or(""), "Apples");
  EXPECT_EQ(prob->rgb.value_or(""), "#FF0000");
  EXPECT_EQ(prob->time_limit.value_or(0), 10);
  EXPECT_EQ(prob->test_data_count.value_or(0), 25);
}

// =====================================================================
// Group decoding
// =====================================================================

TEST(Profile2023DecoderTest, Group) {
  auto session = MakeSession();
  session->ConsumeLine(
      R"({"type":"groups","id":"g1","data":{"id":"g1","name":"Asia","type":"region","hidden":false,"sortorder":1}})");

  auto objs = session->store().ListObjects(ObjectType::kGroups);
  ASSERT_EQ(objs.size(), 1);
  const auto* grp = dynamic_cast<const Group*>(objs[0]);
  ASSERT_NE(grp, nullptr);
  EXPECT_EQ(grp->id, "g1");
  EXPECT_EQ(grp->name.value_or(""), "Asia");
  EXPECT_EQ(grp->type_str.value_or(""), "region");
  EXPECT_EQ(grp->hidden.value_or(true), false);
  EXPECT_EQ(grp->sortorder.value_or(0), 1);
}

// =====================================================================
// Organization decoding
// =====================================================================

TEST(Profile2023DecoderTest, Organization) {
  auto session = MakeSession();
  session->ConsumeLine(
      R"({"type":"organizations","id":"org1","data":{"id":"org1","name":"MIT","formal_name":"Massachusetts Institute of Technology","country":"USA","icpc_id":"1234"}})");

  auto objs = session->store().ListObjects(ObjectType::kOrganizations);
  ASSERT_EQ(objs.size(), 1);
  const auto* org = dynamic_cast<const Organization*>(objs[0]);
  ASSERT_NE(org, nullptr);
  EXPECT_EQ(org->id, "org1");
  EXPECT_EQ(org->name.value_or(""), "MIT");
  EXPECT_EQ(org->formal_name.value_or(""),
            "Massachusetts Institute of Technology");
  EXPECT_EQ(org->country.value_or(""), "USA");
}

// =====================================================================
// Team decoding
// =====================================================================

TEST(Profile2023DecoderTest, Team) {
  auto session = MakeSession();
  session->ConsumeLine(
      R"({"type":"teams","id":"t42","data":{"id":"t42","name":"MIT Red","icpc_id":"12345","organization_id":"org1","group_ids":["g1","g2"],"hidden":false}})");

  auto objs = session->store().ListObjects(ObjectType::kTeams);
  ASSERT_EQ(objs.size(), 1);
  const auto* team = dynamic_cast<const Team*>(objs[0]);
  ASSERT_NE(team, nullptr);
  EXPECT_EQ(team->id, "t42");
  EXPECT_EQ(team->name.value_or(""), "MIT Red");
  EXPECT_EQ(team->icpc_id.value_or(""), "12345");
  EXPECT_EQ(team->organization_id.value_or(""), "org1");
  EXPECT_EQ(team->group_ids.size(), 2);
  EXPECT_EQ(team->group_ids[0], "g1");
  EXPECT_EQ(team->group_ids[1], "g2");
  EXPECT_EQ(team->hidden.value_or(true), false);
}

// =====================================================================
// Person decoding
// =====================================================================

TEST(Profile2023DecoderTest, Person) {
  auto session = MakeSession();
  session->ConsumeLine(
      R"({"type":"persons","id":"p1","data":{"id":"p1","name":"Alice","role":"contestant","team_id":"t42","sex":"female"}})");

  auto objs = session->store().ListObjects(ObjectType::kPersons);
  ASSERT_EQ(objs.size(), 1);
  const auto* person = dynamic_cast<const Person*>(objs[0]);
  ASSERT_NE(person, nullptr);
  EXPECT_EQ(person->id, "p1");
  EXPECT_EQ(person->name.value_or(""), "Alice");
  EXPECT_EQ(person->role.value_or(""), "contestant");
  EXPECT_EQ(person->team_id.value_or(""), "t42");
}

// =====================================================================
// Account decoding
// =====================================================================

TEST(Profile2023DecoderTest, Account) {
  auto session = MakeSession();
  session->ConsumeLine(
      R"({"type":"accounts","id":"a1","data":{"id":"a1","username":"team42","type":"team","team_id":"t42"}})");

  auto objs = session->store().ListObjects(ObjectType::kAccounts);
  ASSERT_EQ(objs.size(), 1);
  const auto* acct = dynamic_cast<const Account*>(objs[0]);
  ASSERT_NE(acct, nullptr);
  EXPECT_EQ(acct->id, "a1");
  EXPECT_EQ(acct->username.value_or(""), "team42");
  EXPECT_EQ(acct->account_type.value_or(""), "team");
  EXPECT_EQ(acct->team_id.value_or(""), "t42");
}

// =====================================================================
// State decoding
// =====================================================================

TEST(Profile2023DecoderTest, State) {
  auto session = MakeSession();
  session->ConsumeLine(
      R"({"type":"state","data":{"started":"2024-04-14T13:00:00Z","frozen":"2024-04-14T17:00:00Z","finalized":"2024-04-14T18:30:00Z"}})");

  const State* st = session->store().GetState();
  ASSERT_NE(st, nullptr);
  ASSERT_TRUE(st->started.has_value());
  ASSERT_TRUE(st->frozen.has_value());
  ASSERT_TRUE(st->finalized.has_value());
  EXPECT_FALSE(st->ended.has_value());
  EXPECT_FALSE(st->thawed.has_value());
  EXPECT_FALSE(st->end_of_updates.has_value());
}

TEST(Profile2023DecoderTest, StateEndOfUpdates) {
  auto session = MakeSession();
  session->ConsumeLine(
      R"({"type":"state","data":{"started":"2024-04-14T13:00:00Z","end_of_updates":"2024-04-14T20:00:00Z"}})");

  EXPECT_TRUE(session->cursor().end_of_updates);
}

// =====================================================================
// Submission decoding
// =====================================================================

TEST(Profile2023DecoderTest, Submission) {
  auto session = MakeSession();
  session->ConsumeLine(
      R"({"type":"submissions","id":"s1","data":{"id":"s1","language_id":"cpp","problem_id":"A","team_id":"t42","time":"2024-04-14T14:30:00Z","contest_time":"1:30:00"}})");

  auto objs = session->store().ListObjects(ObjectType::kSubmissions);
  ASSERT_EQ(objs.size(), 1);
  const auto* sub = dynamic_cast<const Submission*>(objs[0]);
  ASSERT_NE(sub, nullptr);
  EXPECT_EQ(sub->id, "s1");
  EXPECT_EQ(sub->language_id.value_or(""), "cpp");
  EXPECT_EQ(sub->problem_id.value_or(""), "A");
  EXPECT_EQ(sub->team_id.value_or(""), "t42");
  ASSERT_TRUE(sub->time.has_value());
  ASSERT_TRUE(sub->contest_time.has_value());
  EXPECT_EQ(sub->contest_time->milliseconds, 5'400'000);
}

// =====================================================================
// Judgement decoding
// =====================================================================

TEST(Profile2023DecoderTest, Judgement) {
  auto session = MakeSession();
  session->ConsumeLine(
      R"({"type":"judgements","id":"j1","data":{"id":"j1","submission_id":"s1","judgement_type_id":"AC","start_time":"2024-04-14T14:30:10Z","start_contest_time":"1:30:10","end_time":"2024-04-14T14:30:15Z","end_contest_time":"1:30:15","max_run_time":0.456,"valid":true}})");

  auto objs = session->store().ListObjects(ObjectType::kJudgements);
  ASSERT_EQ(objs.size(), 1);
  const auto* j = dynamic_cast<const Judgement*>(objs[0]);
  ASSERT_NE(j, nullptr);
  EXPECT_EQ(j->id, "j1");
  EXPECT_EQ(j->submission_id.value_or(""), "s1");
  EXPECT_EQ(j->judgement_type_id.value_or(""), "AC");
  ASSERT_TRUE(j->start_contest_time.has_value());
  ASSERT_TRUE(j->end_contest_time.has_value());
  EXPECT_NEAR(j->max_run_time.value_or(0.0), 0.456, 0.001);
  EXPECT_EQ(j->valid.value_or(false), true);
}

// =====================================================================
// Run decoding
// =====================================================================

TEST(Profile2023DecoderTest, RunObject) {
  auto session = MakeSession();
  session->ConsumeLine(
      R"({"type":"runs","id":"r1","data":{"id":"r1","judgement_id":"j1","ordinal":1,"judgement_type_id":"AC","time":"2024-04-14T14:30:12Z","contest_time":"1:30:12","run_time":0.234}})");

  auto objs = session->store().ListObjects(ObjectType::kRuns);
  ASSERT_EQ(objs.size(), 1);
  const auto* r = dynamic_cast<const ccsparser::Run*>(objs[0]);
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->id, "r1");
  EXPECT_EQ(r->judgement_id.value_or(""), "j1");
  EXPECT_EQ(r->ordinal.value_or(0), 1);
  EXPECT_NEAR(r->run_time.value_or(0.0), 0.234, 0.001);
}

// =====================================================================
// Clarification decoding (2023-06 to_team_id singular)
// =====================================================================

TEST(Profile2023DecoderTest, ClarificationToTeamIdSingular) {
  auto session = MakeSession();
  session->ConsumeLine(
      R"({"type":"clarifications","id":"clar1","data":{"id":"clar1","from_team_id":"t42","to_team_id":"t43","problem_id":"A","text":"Is X allowed?","time":"2024-04-14T14:00:00Z","contest_time":"1:00:00"}})");

  auto objs = session->store().ListObjects(ObjectType::kClarifications);
  ASSERT_EQ(objs.size(), 1);
  const auto* clar = dynamic_cast<const Clarification*>(objs[0]);
  ASSERT_NE(clar, nullptr);
  EXPECT_EQ(clar->from_team_id.value_or(""), "t42");
  // 2023-06: to_team_id is normalized to to_team_ids.
  ASSERT_GE(clar->to_team_ids.size(), 1);
  EXPECT_EQ(clar->to_team_ids[0], "t43");
  EXPECT_EQ(clar->text.value_or(""), "Is X allowed?");
}

// =====================================================================
// Award decoding
// =====================================================================

TEST(Profile2023DecoderTest, Award) {
  auto session = MakeSession();
  session->ConsumeLine(
      R"({"type":"awards","id":"gold-medal","data":{"id":"gold-medal","citation":"Gold Medal","team_ids":["t1","t2","t3"]}})");

  auto objs = session->store().ListObjects(ObjectType::kAwards);
  ASSERT_EQ(objs.size(), 1);
  const auto* award = dynamic_cast<const Award*>(objs[0]);
  ASSERT_NE(award, nullptr);
  EXPECT_EQ(award->id, "gold-medal");
  EXPECT_EQ(award->citation.value_or(""), "Gold Medal");
  EXPECT_EQ(award->team_ids.size(), 3);
}

// =====================================================================
// Commentary decoding
// =====================================================================

TEST(Profile2023DecoderTest, Commentary) {
  auto session = MakeSession();
  session->ConsumeLine(
      R"({"type":"commentary","id":"c1","data":{"id":"c1","message":"Team 42 just solved A!","team_ids":["t42"],"problem_ids":["A"],"time":"2024-04-14T14:30:00Z","contest_time":"1:30:00"}})");

  auto objs = session->store().ListObjects(ObjectType::kCommentary);
  ASSERT_EQ(objs.size(), 1);
  const auto* comm = dynamic_cast<const Commentary*>(objs[0]);
  ASSERT_NE(comm, nullptr);
  EXPECT_EQ(comm->message.value_or(""), "Team 42 just solved A!");
  EXPECT_EQ(comm->team_ids.size(), 1);
  EXPECT_EQ(comm->problem_ids.size(), 1);
}

// =====================================================================
// Unknown fields preservation (2023-06)
// =====================================================================

TEST(Profile2023DecoderTest, UnknownFieldsPreserved) {
  ParseOptions opts;
  opts.version = ApiVersion::k2023_06;
  opts.unknown_field_policy = UnknownFieldPolicy::kPreserve;
  auto session = MakeSession(opts);

  session->ConsumeLine(
      R"({"type":"problems","id":"A","data":{"id":"A","label":"A","name":"Apples","custom_field":"custom_value"}})");

  auto objs = session->store().ListObjects(ObjectType::kProblems);
  ASSERT_EQ(objs.size(), 1);
  const auto* prob = dynamic_cast<const Problem*>(objs[0]);
  ASSERT_NE(prob, nullptr);
  // Unknown field should be preserved.
  auto it = prob->unknown_fields.find("custom_field");
  ASSERT_NE(it, prob->unknown_fields.end());
  // Preserved as JSON string value.
  EXPECT_NE(it->second.find("custom_value"), std::string::npos);
}

// =====================================================================
// Collection replace with 2023-06 decoding
// =====================================================================

TEST(Profile2023DecoderTest, CollectionReplace) {
  auto session = MakeSession();
  session->ConsumeLine(
      R"({"type":"judgement-types","data":[{"id":"AC","name":"Accepted","penalty":false,"solved":true},{"id":"WA","name":"Wrong Answer","penalty":true,"solved":false},{"id":"TLE","name":"Time Limit Exceeded","penalty":true,"solved":false}]})");

  auto objs = session->store().ListObjects(ObjectType::kJudgementTypes);
  EXPECT_EQ(objs.size(), 3);
}

// =====================================================================
// FileRef decoding
// =====================================================================

TEST(Profile2023DecoderTest, FileRefInProblemStatement) {
  auto session = MakeSession();
  session->ConsumeLine(
      R"({"type":"problems","id":"A","data":{"id":"A","label":"A","statement":[{"href":"https://example.com/A.pdf","mime":"application/pdf","filename":"A.pdf"}]}})");

  auto objs = session->store().ListObjects(ObjectType::kProblems);
  ASSERT_EQ(objs.size(), 1);
  const auto* prob = dynamic_cast<const Problem*>(objs[0]);
  ASSERT_NE(prob, nullptr);
  ASSERT_EQ(prob->statement.size(), 1);
  EXPECT_EQ(prob->statement[0].href, "https://example.com/A.pdf");
  EXPECT_EQ(prob->statement[0].mime.value_or(""), "application/pdf");
  EXPECT_EQ(prob->statement[0].filename.value_or(""), "A.pdf");
}

TEST(Profile2023DecoderTest, TeamPhotoFileRef) {
  auto session = MakeSession();
  session->ConsumeLine(
      R"({"type":"teams","id":"t1","data":{"id":"t1","name":"Alpha","photo":[{"href":"teams/t1/photo.jpg","mime":"image/jpeg","width":640,"height":480}]}})");

  auto objs = session->store().ListObjects(ObjectType::kTeams);
  ASSERT_EQ(objs.size(), 1);
  const auto* team = dynamic_cast<const Team*>(objs[0]);
  ASSERT_NE(team, nullptr);
  ASSERT_EQ(team->photo.size(), 1);
  EXPECT_EQ(team->photo[0].href, "teams/t1/photo.jpg");
  EXPECT_EQ(team->photo[0].width.value_or(0), 640);
  EXPECT_EQ(team->photo[0].height.value_or(0), 480);
}

// =====================================================================
// Version detection (auto mode)
// =====================================================================

TEST(Profile2023DecoderTest, VersionDetectedFromPenaltyTimeInt) {
  ParseOptions opts;
  opts.version = ApiVersion::kAuto;
  auto session_or = EventFeedParser::CreateStreamingSession(opts);
  ASSERT_TRUE(session_or.ok());
  auto& session = session_or.value();

  session->ConsumeLine(
      R"({"type":"contest","data":{"id":"c1","penalty_time":20}})");

  EXPECT_EQ(session->resolved_version(), ApiVersion::k2023_06);
}

}  // namespace
}  // namespace ccsparser
