// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

// Integration test: fetches from DOMjudge demo REST API and validates
// scoreboard against the API's own /scoreboard endpoint.
//
// This test requires network access and will be skipped if the DOMjudge
// demo server is unreachable.

#include <iostream>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "ccsparser/http_client.h"
#include "ccsparser/parser.h"
#include "ccsparser/scoreboard_builder.h"

namespace ccsparser {
namespace {

using json = nlohmann::json;

static const char* kBaseUrl =
    "https://www.domjudge.org/demoweb/api/v4";
static const char* kContestId = "nwerc18";
static const char* kUsername = "admin";
static const char* kPassword = "admin";

class DomjudgeRestApiTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Quick connectivity check.
    HttpClient client;
    HttpRequestOptions opts;
    opts.timeout_seconds = 10;
    opts.basic_auth = HttpBasicAuth{kUsername, kPassword};

    auto resp = client.Get(
        std::string(kBaseUrl) + "/contests/" + kContestId, opts);
    if (!resp.ok()) {
      GTEST_SKIP() << "DOMjudge demo unreachable: " << resp.status().message();
    }
  }
};

TEST_F(DomjudgeRestApiTest, FetchAndBuildScoreboard) {
  RestApiOptions rest_opts;
  rest_opts.base_url = kBaseUrl;
  rest_opts.contest_id = kContestId;
  rest_opts.auth = HttpBasicAuth{kUsername, kPassword};
  rest_opts.timeout_seconds = 60;

  ParseOptions parse_opts;
  parse_opts.version = ApiVersion::kAuto;
  parse_opts.error_policy = ErrorPolicy::kContinue;
  parse_opts.keep_event_log = false;

  auto result = RestApiParser::FetchAndParse(rest_opts, parse_opts);
  ASSERT_TRUE(result.ok()) << result.status().ToString();

  auto& pr = result.value();
  std::cerr << "Parsed " << pr.cursor.event_count << " objects, "
            << pr.diagnostics.size() << " diagnostics\n";

  // Should have contest, teams, problems, submissions, judgements.
  EXPECT_NE(pr.store.GetContest(), nullptr);
  EXPECT_GT(pr.store.ListObjects(ObjectType::kTeams).size(), 100u);
  EXPECT_GT(pr.store.ListObjects(ObjectType::kProblems).size(), 5u);
  EXPECT_GT(pr.store.ListObjects(ObjectType::kSubmissions).size(), 1000u);
  EXPECT_GT(pr.store.ListObjects(ObjectType::kJudgements).size(), 1000u);

  // Build scoreboard.
  auto sb_or = BuildScoreboard(pr.store);
  ASSERT_TRUE(sb_or.ok()) << sb_or.status().ToString();
  auto& sb = sb_or.value();

  std::cerr << "Scoreboard: " << sb.rows.size() << " teams, "
            << sb.problems.size() << " problems\n";
  EXPECT_EQ(sb.rows.size(), 122u);
  EXPECT_EQ(sb.problems.size(), 11u);

  // Fetch reference scoreboard from API.
  HttpClient client;
  HttpRequestOptions http_opts;
  http_opts.timeout_seconds = 30;
  http_opts.basic_auth = HttpBasicAuth{kUsername, kPassword};

  auto ref_resp = client.Get(
      std::string(kBaseUrl) + "/contests/" + kContestId + "/scoreboard",
      http_opts);
  ASSERT_TRUE(ref_resp.ok()) << ref_resp.status().ToString();

  auto ref_sb = json::parse(ref_resp.value().body);
  auto& ref_rows = ref_sb["rows"];
  ASSERT_EQ(ref_rows.size(), 122u);

  // Fetch teams for name lookup.
  auto teams_resp = client.Get(
      std::string(kBaseUrl) + "/contests/" + kContestId + "/teams",
      http_opts);
  ASSERT_TRUE(teams_resp.ok());
  auto teams_json = json::parse(teams_resp.value().body);
  std::unordered_map<std::string, std::string> team_names;
  for (const auto& t : teams_json) {
    std::string tid = t["id"].get<std::string>();
    std::string name = t.contains("display_name") && !t["display_name"].is_null()
                           ? t["display_name"].get<std::string>()
                           : t["name"].get<std::string>();
    // Trim trailing whitespace.
    while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) {
      name.pop_back();
    }
    team_names[tid] = name;
  }

  // Build lookup from our scoreboard (trimmed names for comparison).
  auto trim = [](std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
    return s;
  };
  std::unordered_map<std::string, const ScoreboardRow*> our_by_name;
  for (const auto& row : sb.rows) {
    our_by_name[trim(row.team_name)] = &row;
  }

  // Compare.
  int mismatches = 0;
  for (const auto& ref_row : ref_rows) {
    std::string tid = ref_row["team_id"].get<std::string>();
    int ref_solved = ref_row["score"]["num_solved"].get<int>();
    int ref_penalty = ref_row["score"]["total_time"].get<int>();

    auto name_it = team_names.find(tid);
    ASSERT_NE(name_it, team_names.end()) << "Missing team: " << tid;

    auto our_it = our_by_name.find(name_it->second);
    if (our_it == our_by_name.end()) {
      std::cerr << "NOT FOUND: " << name_it->second << " (team_id=" << tid
                << ")\n";
      mismatches++;
      continue;
    }

    const auto* our_row = our_it->second;
    if (our_row->solved != ref_solved || our_row->penalty != ref_penalty) {
      std::cerr << "MISMATCH " << name_it->second
                << ": us=(" << our_row->solved << "," << our_row->penalty
                << ") ref=(" << ref_solved << "," << ref_penalty << ")\n";
      mismatches++;
    }
  }

  EXPECT_EQ(mismatches, 0) << mismatches << " teams have incorrect results";
  std::cerr << "Validated " << ref_rows.size() << " teams, "
            << mismatches << " mismatches\n";
}

}  // namespace
}  // namespace ccsparser
