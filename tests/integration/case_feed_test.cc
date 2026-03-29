// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

// Integration tests using real event-feed case files.

#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "ccsparser/ccsparser.h"

namespace ccsparser {
namespace {

namespace fs = std::filesystem;

// Helper to find case files. Returns empty path if not found.
fs::path FindCaseFile(const std::string& filename) {
  // Try workspace-relative paths.
  std::vector<fs::path> candidates = {
      fs::path("case") / filename,
      fs::path("ccsparser/case") / filename,
  };

  // Also try TEST_SRCDIR.
  const char* srcdir = std::getenv("TEST_SRCDIR");
  const char* workspace = std::getenv("TEST_WORKSPACE");
  if (srcdir && workspace) {
    candidates.push_back(fs::path(srcdir) / workspace / "case" / filename);
  }

  for (const auto& p : candidates) {
    if (fs::exists(p)) return p;
  }
  return {};
}

// Test parsing the first case file: event-feed.ndjson
TEST(CaseFeedTest, ParseEventFeedNdjson) {
  auto path = FindCaseFile("event-feed.ndjson");
  if (path.empty()) {
    GTEST_SKIP() << "Case file event-feed.ndjson not found";
  }

  ParseOptions opts;
  opts.version = ApiVersion::kAuto;
  opts.error_policy = ErrorPolicy::kContinue;
  opts.keep_event_log = false;  // Save memory for large files.
  opts.keep_raw_json = false;

  auto result = EventFeedParser::ParseFile(path, opts);
  ASSERT_TRUE(result.ok()) << result.status().ToString();

  auto& pr = result.value();
  auto* contest = pr.store.GetContest();
  ASSERT_NE(contest, nullptr);
  EXPECT_FALSE(contest->name.value_or("").empty());
  EXPECT_TRUE(contest->penalty_time.has_value());
  EXPECT_EQ(contest->penalty_time->milliseconds, 20 * 60 * 1000);

  // Should have teams, submissions, etc.
  auto teams = pr.store.ListObjects(ObjectType::kTeams);
  EXPECT_GT(teams.size(), 0);

  auto submissions = pr.store.ListObjects(ObjectType::kSubmissions);
  EXPECT_GT(submissions.size(), 0);

  auto judgements = pr.store.ListObjects(ObjectType::kJudgements);
  EXPECT_GT(judgements.size(), 0);

  // State should have end_of_updates.
  auto* state = pr.store.GetState();
  ASSERT_NE(state, nullptr);
  EXPECT_TRUE(state->end_of_updates.has_value());
  EXPECT_TRUE(pr.cursor.end_of_updates);

  // Should have detected version.
  EXPECT_NE(pr.resolved_version, ApiVersion::kAuto);
}

// Test parsing the second case file: event-feed(1).json
TEST(CaseFeedTest, ParseEventFeed1Json) {
  auto path = FindCaseFile("event-feed(1).json");
  if (path.empty()) {
    GTEST_SKIP() << "Case file event-feed(1).json not found";
  }

  ParseOptions opts;
  opts.version = ApiVersion::kAuto;
  opts.error_policy = ErrorPolicy::kContinue;
  opts.keep_event_log = false;
  opts.keep_raw_json = false;

  auto result = EventFeedParser::ParseFile(path, opts);
  ASSERT_TRUE(result.ok()) << result.status().ToString();

  auto& pr = result.value();
  auto* contest = pr.store.GetContest();
  ASSERT_NE(contest, nullptr);
  EXPECT_FALSE(contest->name.value_or("").empty());

  auto teams = pr.store.ListObjects(ObjectType::kTeams);
  EXPECT_GT(teams.size(), 0);

  auto* state = pr.store.GetState();
  ASSERT_NE(state, nullptr);
}

// Test parsing the third case file: event-feed(2).ndjson
TEST(CaseFeedTest, ParseEventFeed2Ndjson) {
  auto path = FindCaseFile("event-feed(2).ndjson");
  if (path.empty()) {
    GTEST_SKIP() << "Case file event-feed(2).ndjson not found";
  }

  ParseOptions opts;
  opts.version = ApiVersion::kAuto;
  opts.error_policy = ErrorPolicy::kContinue;
  opts.keep_event_log = false;
  opts.keep_raw_json = false;

  auto result = EventFeedParser::ParseFile(path, opts);
  ASSERT_TRUE(result.ok()) << result.status().ToString();

  auto& pr = result.value();
  auto* contest = pr.store.GetContest();
  ASSERT_NE(contest, nullptr);

  auto teams = pr.store.ListObjects(ObjectType::kTeams);
  EXPECT_GT(teams.size(), 0);

  auto submissions = pr.store.ListObjects(ObjectType::kSubmissions);
  EXPECT_GT(submissions.size(), 0);
}

// Test that all case files parse consistently (awards, problems, etc).
TEST(CaseFeedTest, CaseFilesHaveConsistentData) {
  auto path = FindCaseFile("event-feed.ndjson");
  if (path.empty()) {
    GTEST_SKIP() << "Case file event-feed.ndjson not found";
  }

  ParseOptions opts;
  opts.version = ApiVersion::k2023_06;
  opts.error_policy = ErrorPolicy::kContinue;
  opts.keep_event_log = false;

  auto result = EventFeedParser::ParseFile(path, opts);
  ASSERT_TRUE(result.ok()) << result.status().ToString();

  auto& pr = result.value();

  // Check problems exist.
  auto problems = pr.store.ListObjects(ObjectType::kProblems);
  EXPECT_GT(problems.size(), 0);
  for (const auto* obj : problems) {
    auto* prob = dynamic_cast<const Problem*>(obj);
    ASSERT_NE(prob, nullptr);
    EXPECT_FALSE(prob->id.empty());
  }

  // Check judgement types exist.
  auto jt = pr.store.ListObjects(ObjectType::kJudgementTypes);
  EXPECT_GT(jt.size(), 0);

  // Check awards if present.
  auto awards = pr.store.ListObjects(ObjectType::kAwards);
  for (const auto* obj : awards) {
    auto* award = dynamic_cast<const Award*>(obj);
    ASSERT_NE(award, nullptr);
    EXPECT_FALSE(award->id.empty());
  }
}

}  // namespace
}  // namespace ccsparser
