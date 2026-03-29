// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

// Unit tests for the ScoreboardBuilder module.

#include <string>

#include <gtest/gtest.h>

#include "ccsparser/ccsparser.h"
#include "ccsparser/scoreboard.h"
#include "ccsparser/scoreboard_builder.h"

namespace ccsparser {
namespace {

// Helper: create a streaming session, feed lines, and return ParseResult.
ParseResult Feed(const std::vector<std::string>& lines,
                 ApiVersion version = ApiVersion::k2023_06) {
  ParseOptions opts;
  opts.version = version;
  opts.error_policy = ErrorPolicy::kContinue;
  opts.keep_event_log = false;

  auto session_or = EventFeedParser::CreateStreamingSession(opts);
  EXPECT_TRUE(session_or.ok());
  auto& session = session_or.value();
  for (const auto& line : lines) {
    session->ConsumeLine(line);
  }
  session->Finish();

  ParseResult pr;
  pr.resolved_version = session->resolved_version();
  pr.store = std::move(session->mutable_store());
  pr.cursor = session->cursor();
  return pr;
}

TEST(ScoreboardBuilderTest, EmptyStoreProducesEmptyScoreboard) {
  auto pr = Feed({R"({"type":"state","data":{}})"});
  auto sb_or = BuildScoreboard(pr.store);
  ASSERT_TRUE(sb_or.ok());
  EXPECT_TRUE(sb_or->rows.empty());
}

TEST(ScoreboardBuilderTest, BasicRanking) {
  auto pr = Feed({
      R"({"type":"state","data":{}})",
      R"({"type":"contest","id":"c","data":{"id":"c","name":"Test","penalty_time":20}})",
      R"({"type":"judgement-types","id":"AC","data":{"id":"AC","name":"correct","penalty":false,"solved":true}})",
      R"({"type":"judgement-types","id":"WA","data":{"id":"WA","name":"wrong answer","penalty":true,"solved":false}})",
      R"({"type":"problems","id":"A","data":{"id":"A","ordinal":1,"label":"A","name":"PA"}})",
      R"({"type":"problems","id":"B","data":{"id":"B","ordinal":2,"label":"B","name":"PB"}})",
      R"({"type":"teams","id":"t1","data":{"id":"t1","name":"Team 1"}})",
      R"({"type":"teams","id":"t2","data":{"id":"t2","name":"Team 2"}})",
      // t1 solves A at 15 min
      R"({"type":"submissions","id":"s1","data":{"id":"s1","language_id":"cpp","problem_id":"A","team_id":"t1","contest_time":"0:15:00"}})",
      R"({"type":"judgements","id":"j1","data":{"id":"j1","submission_id":"s1","judgement_type_id":"AC","end_time":"2025-01-01T10:15:05.000+00:00"}})",
      // t2 solves A at 10 min and B at 30 min
      R"({"type":"submissions","id":"s2","data":{"id":"s2","language_id":"cpp","problem_id":"A","team_id":"t2","contest_time":"0:10:00"}})",
      R"({"type":"judgements","id":"j2","data":{"id":"j2","submission_id":"s2","judgement_type_id":"AC","end_time":"2025-01-01T10:10:05.000+00:00"}})",
      R"({"type":"submissions","id":"s3","data":{"id":"s3","language_id":"cpp","problem_id":"B","team_id":"t2","contest_time":"0:30:00"}})",
      R"({"type":"judgements","id":"j3","data":{"id":"j3","submission_id":"s3","judgement_type_id":"AC","end_time":"2025-01-01T10:30:05.000+00:00"}})",
  });

  auto sb_or = BuildScoreboard(pr.store);
  ASSERT_TRUE(sb_or.ok());
  auto& sb = sb_or.value();

  EXPECT_EQ(sb.contest_name, "Test");
  EXPECT_EQ(sb.problems.size(), 2);
  ASSERT_EQ(sb.rows.size(), 2);

  // t2 should be first (2 solved), t1 second (1 solved).
  EXPECT_EQ(sb.rows[0].team_id, "t2");
  EXPECT_EQ(sb.rows[0].solved, 2);
  EXPECT_EQ(sb.rows[0].place, 1);

  EXPECT_EQ(sb.rows[1].team_id, "t1");
  EXPECT_EQ(sb.rows[1].solved, 1);
  EXPECT_EQ(sb.rows[1].place, 2);
}

TEST(ScoreboardBuilderTest, PenaltyCalculation) {
  auto pr = Feed({
      R"({"type":"state","data":{}})",
      R"({"type":"contest","id":"c","data":{"id":"c","name":"Pen","penalty_time":20}})",
      R"({"type":"judgement-types","id":"AC","data":{"id":"AC","penalty":false,"solved":true}})",
      R"({"type":"judgement-types","id":"WA","data":{"id":"WA","penalty":true,"solved":false}})",
      R"({"type":"problems","id":"A","data":{"id":"A","ordinal":1,"label":"A"}})",
      R"({"type":"teams","id":"t1","data":{"id":"t1","name":"T1"}})",
      // WA at 10 min
      R"({"type":"submissions","id":"s1","data":{"id":"s1","problem_id":"A","team_id":"t1","contest_time":"0:10:00"}})",
      R"({"type":"judgements","id":"j1","data":{"id":"j1","submission_id":"s1","judgement_type_id":"WA","end_time":"2025-01-01T10:10:05.000+00:00"}})",
      // AC at 30 min
      R"({"type":"submissions","id":"s2","data":{"id":"s2","problem_id":"A","team_id":"t1","contest_time":"0:30:00"}})",
      R"({"type":"judgements","id":"j2","data":{"id":"j2","submission_id":"s2","judgement_type_id":"AC","end_time":"2025-01-01T10:30:05.000+00:00"}})",
  });

  auto sb_or = BuildScoreboard(pr.store);
  ASSERT_TRUE(sb_or.ok());
  ASSERT_EQ(sb_or->rows.size(), 1);
  auto& row = sb_or->rows[0];

  EXPECT_EQ(row.solved, 1);
  // penalty_ms = accept_time_ms(30*60000) + wrongs(1)*penalty_ms(20*60000)
  //            = 1800000 + 1200000 = 3000000 ms => 50 min exactly.
  EXPECT_EQ(row.penalty, 50);
  ASSERT_EQ(row.cells.size(), 1);
  EXPECT_EQ(row.cells[0].status, ProblemStatus::kSolved);
  EXPECT_EQ(row.cells[0].attempts, 2);
  EXPECT_EQ(row.cells[0].time_minutes, 30);
}

TEST(ScoreboardBuilderTest, TieBreakByPenalty) {
  auto pr = Feed({
      R"({"type":"state","data":{}})",
      R"({"type":"contest","id":"c","data":{"id":"c","penalty_time":20}})",
      R"({"type":"judgement-types","id":"AC","data":{"id":"AC","solved":true,"penalty":false}})",
      R"({"type":"problems","id":"A","data":{"id":"A","ordinal":1,"label":"A"}})",
      R"({"type":"teams","id":"t1","data":{"id":"t1","name":"T1"}})",
      R"({"type":"teams","id":"t2","data":{"id":"t2","name":"T2"}})",
      // t1 solves A at 60 min
      R"({"type":"submissions","id":"s1","data":{"id":"s1","problem_id":"A","team_id":"t1","contest_time":"1:00:00"}})",
      R"({"type":"judgements","id":"j1","data":{"id":"j1","submission_id":"s1","judgement_type_id":"AC","end_time":"2025-01-01T11:00:05.000+00:00"}})",
      // t2 solves A at 30 min
      R"({"type":"submissions","id":"s2","data":{"id":"s2","problem_id":"A","team_id":"t2","contest_time":"0:30:00"}})",
      R"({"type":"judgements","id":"j2","data":{"id":"j2","submission_id":"s2","judgement_type_id":"AC","end_time":"2025-01-01T10:30:05.000+00:00"}})",
  });

  auto sb_or = BuildScoreboard(pr.store);
  ASSERT_TRUE(sb_or.ok());
  ASSERT_EQ(sb_or->rows.size(), 2);
  // Both solved 1, t2 has lower penalty (30 < 60).
  EXPECT_EQ(sb_or->rows[0].team_id, "t2");
  EXPECT_EQ(sb_or->rows[0].penalty, 30);
  EXPECT_EQ(sb_or->rows[1].team_id, "t1");
  EXPECT_EQ(sb_or->rows[1].penalty, 60);
  // Same place because same solved; different penalty => different place.
  EXPECT_EQ(sb_or->rows[0].place, 1);
  EXPECT_EQ(sb_or->rows[1].place, 2);
}

TEST(ScoreboardBuilderTest, AwardsAssociation) {
  auto pr = Feed({
      R"({"type":"state","data":{}})",
      R"({"type":"contest","id":"c","data":{"id":"c","penalty_time":20}})",
      R"({"type":"judgement-types","id":"AC","data":{"id":"AC","solved":true,"penalty":false}})",
      R"({"type":"problems","id":"A","data":{"id":"A","ordinal":1,"label":"A"}})",
      R"({"type":"teams","id":"t1","data":{"id":"t1","name":"T1"}})",
      R"({"type":"submissions","id":"s1","data":{"id":"s1","problem_id":"A","team_id":"t1","contest_time":"0:10:00"}})",
      R"({"type":"judgements","id":"j1","data":{"id":"j1","submission_id":"s1","judgement_type_id":"AC","end_time":"2025-01-01T10:10:05.000+00:00"}})",
      R"({"type":"awards","id":"gold-medal","data":{"id":"gold-medal","citation":"Gold Medal","team_ids":["t1"]}})",
      R"({"type":"awards","id":"winner","data":{"id":"winner","citation":"Contest Winner","team_ids":["t1"]}})",
  });

  auto sb_or = BuildScoreboard(pr.store);
  ASSERT_TRUE(sb_or.ok());
  ASSERT_EQ(sb_or->rows.size(), 1);
  auto& row = sb_or->rows[0];
  ASSERT_EQ(row.awards.size(), 2);
  // Check both awards present.
  bool has_gold = false, has_winner = false;
  for (const auto& a : row.awards) {
    if (a.award_id == "gold-medal") has_gold = true;
    if (a.award_id == "winner") has_winner = true;
  }
  EXPECT_TRUE(has_gold);
  EXPECT_TRUE(has_winner);
}

TEST(ScoreboardBuilderTest, FirstToSolve) {
  auto pr = Feed({
      R"({"type":"state","data":{}})",
      R"({"type":"contest","id":"c","data":{"id":"c","penalty_time":20}})",
      R"({"type":"judgement-types","id":"AC","data":{"id":"AC","solved":true,"penalty":false}})",
      R"({"type":"problems","id":"A","data":{"id":"A","ordinal":1,"label":"A"}})",
      R"({"type":"teams","id":"t1","data":{"id":"t1","name":"T1"}})",
      R"({"type":"teams","id":"t2","data":{"id":"t2","name":"T2"}})",
      // t1 solves A at 20 min
      R"({"type":"submissions","id":"s1","data":{"id":"s1","problem_id":"A","team_id":"t1","contest_time":"0:20:00"}})",
      R"({"type":"judgements","id":"j1","data":{"id":"j1","submission_id":"s1","judgement_type_id":"AC","end_time":"2025-01-01T10:20:05.000+00:00"}})",
      // t2 solves A at 10 min (earlier)
      R"({"type":"submissions","id":"s2","data":{"id":"s2","problem_id":"A","team_id":"t2","contest_time":"0:10:00"}})",
      R"({"type":"judgements","id":"j2","data":{"id":"j2","submission_id":"s2","judgement_type_id":"AC","end_time":"2025-01-01T10:10:05.000+00:00"}})",
  });

  auto sb_or = BuildScoreboard(pr.store);
  ASSERT_TRUE(sb_or.ok());
  ASSERT_EQ(sb_or->rows.size(), 2);
  // t2 solved first.
  for (const auto& row : sb_or->rows) {
    for (const auto& cell : row.cells) {
      if (cell.problem_id == "A" && row.team_id == "t2") {
        EXPECT_TRUE(cell.is_first_to_solve);
      } else if (cell.problem_id == "A" && row.team_id == "t1") {
        EXPECT_FALSE(cell.is_first_to_solve);
      }
    }
  }
}

TEST(ScoreboardBuilderTest, FailedAndEmptyCells) {
  auto pr = Feed({
      R"({"type":"state","data":{}})",
      R"({"type":"contest","id":"c","data":{"id":"c","penalty_time":20}})",
      R"({"type":"judgement-types","id":"AC","data":{"id":"AC","solved":true,"penalty":false}})",
      R"({"type":"judgement-types","id":"WA","data":{"id":"WA","solved":false,"penalty":true}})",
      R"({"type":"problems","id":"A","data":{"id":"A","ordinal":1,"label":"A"}})",
      R"({"type":"problems","id":"B","data":{"id":"B","ordinal":2,"label":"B"}})",
      R"({"type":"teams","id":"t1","data":{"id":"t1","name":"T1"}})",
      // t1 fails A
      R"({"type":"submissions","id":"s1","data":{"id":"s1","problem_id":"A","team_id":"t1","contest_time":"0:10:00"}})",
      R"({"type":"judgements","id":"j1","data":{"id":"j1","submission_id":"s1","judgement_type_id":"WA","end_time":"2025-01-01T10:10:05.000+00:00"}})",
      // No submissions for B
  });

  auto sb_or = BuildScoreboard(pr.store);
  ASSERT_TRUE(sb_or.ok());
  ASSERT_EQ(sb_or->rows.size(), 1);
  auto& row = sb_or->rows[0];
  ASSERT_EQ(row.cells.size(), 2);
  EXPECT_EQ(row.cells[0].status, ProblemStatus::kFailed);
  EXPECT_EQ(row.cells[0].attempts, 1);
  EXPECT_EQ(row.cells[1].status, ProblemStatus::kEmpty);
}

TEST(ScoreboardBuilderTest, HiddenTeamsExcluded) {
  auto pr = Feed({
      R"({"type":"state","data":{}})",
      R"({"type":"contest","id":"c","data":{"id":"c","penalty_time":20}})",
      R"({"type":"teams","id":"t1","data":{"id":"t1","name":"T1","hidden":false}})",
      R"({"type":"teams","id":"t2","data":{"id":"t2","name":"Hidden","hidden":true}})",
  });

  auto sb_or = BuildScoreboard(pr.store);
  ASSERT_TRUE(sb_or.ok());
  EXPECT_EQ(sb_or->rows.size(), 1);
  EXPECT_EQ(sb_or->rows[0].team_id, "t1");
}

TEST(ScoreboardBuilderTest, OrganizationLookup) {
  auto pr = Feed({
      R"({"type":"state","data":{}})",
      R"({"type":"contest","id":"c","data":{"id":"c","penalty_time":20}})",
      R"({"type":"organizations","id":"org1","data":{"id":"org1","name":"University X"}})",
      R"({"type":"teams","id":"t1","data":{"id":"t1","name":"T1","organization_id":"org1"}})",
  });

  auto sb_or = BuildScoreboard(pr.store);
  ASSERT_TRUE(sb_or.ok());
  ASSERT_EQ(sb_or->rows.size(), 1);
  EXPECT_EQ(sb_or->rows[0].organization_name, "University X");
  EXPECT_EQ(sb_or->rows[0].organization_id, "org1");
}

}  // namespace
}  // namespace ccsparser
