// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

// Unit tests for the XML feed parser.

#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "ccsparser/parser.h"
#include "ccsparser/scoreboard_builder.h"

namespace ccsparser {
namespace {

TEST(XmlFeedParserTest, ParsesSimpleContest) {
  const std::string xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<contest>
  <info>
    <id>test</id>
    <name>Test Contest</name>
    <duration>5:00:00</duration>
    <penalty_time>20</penalty_time>
    <start_time>2024-01-01T10:00:00+00:00</start_time>
  </info>
  <judgement-type>
    <id>AC</id><name>Accepted</name><solved>true</solved><penalty>false</penalty>
  </judgement-type>
  <judgement-type>
    <id>WA</id><name>Wrong Answer</name><solved>false</solved><penalty>true</penalty>
  </judgement-type>
  <problem>
    <id>A</id><label>A</label><name>Problem A</name><ordinal>0</ordinal>
  </problem>
  <team>
    <id>1</id><name>Team One</name>
  </team>
  <team>
    <id>2</id><name>Team Two</name>
  </team>
  <submission>
    <id>s1</id><team_id>1</team_id><problem_id>A</problem_id>
    <language_id>cpp</language_id>
    <time>2024-01-01T10:30:00+00:00</time>
    <contest_time>0:30:00</contest_time>
  </submission>
  <judgement>
    <id>j1</id><submission_id>s1</submission_id><judgement_type_id>AC</judgement_type_id>
    <start_time>2024-01-01T10:30:01+00:00</start_time>
    <start_contest_time>0:30:01</start_contest_time>
    <end_time>2024-01-01T10:30:05+00:00</end_time>
    <end_contest_time>0:30:05</end_contest_time>
  </judgement>
</contest>
)";

  ParseOptions opts;
  opts.version = ApiVersion::k2023_06;
  opts.error_policy = ErrorPolicy::kContinue;

  std::istringstream stream(xml);
  auto result = XmlFeedParser::ParseStream(stream, opts);
  ASSERT_TRUE(result.ok()) << result.status().ToString();

  auto& pr = result.value();
  EXPECT_GT(pr.cursor.event_count, 0u);

  // Verify contest was parsed.
  const Contest* contest = pr.store.GetContest();
  ASSERT_NE(contest, nullptr);
  EXPECT_EQ(contest->name.value_or(""), "Test Contest");

  // Verify teams.
  auto teams = pr.store.ListObjects(ObjectType::kTeams);
  EXPECT_EQ(teams.size(), 2u);

  // Verify problems.
  auto problems = pr.store.ListObjects(ObjectType::kProblems);
  EXPECT_EQ(problems.size(), 1u);

  // Verify submissions.
  auto subs = pr.store.ListObjects(ObjectType::kSubmissions);
  EXPECT_EQ(subs.size(), 1u);

  // Verify judgements.
  auto judgements = pr.store.ListObjects(ObjectType::kJudgements);
  EXPECT_EQ(judgements.size(), 1u);

  // Build scoreboard and verify.
  auto sb_or = BuildScoreboard(pr.store);
  ASSERT_TRUE(sb_or.ok()) << sb_or.status().ToString();
  auto& sb = sb_or.value();
  EXPECT_EQ(sb.rows.size(), 2u);
  // Team One solved 1, Team Two solved 0.
  EXPECT_EQ(sb.rows[0].team_name, "Team One");
  EXPECT_EQ(sb.rows[0].solved, 1);
  EXPECT_EQ(sb.rows[0].penalty, 30);
  EXPECT_EQ(sb.rows[1].team_name, "Team Two");
  EXPECT_EQ(sb.rows[1].solved, 0);
}

TEST(XmlFeedParserTest, ParsesOpBasedEvents) {
  const std::string xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<events>
  <event>
    <type>team</type>
    <data>
      <id>t1</id><name>Alpha</name>
    </data>
  </event>
  <event>
    <type>team</type>
    <data>
      <id>t2</id><name>Beta</name>
    </data>
  </event>
  <event>
    <type>problem</type>
    <data>
      <id>p1</id><label>A</label><name>Prob A</name><ordinal>0</ordinal>
    </data>
  </event>
</events>
)";

  ParseOptions opts;
  opts.version = ApiVersion::k2023_06;
  opts.error_policy = ErrorPolicy::kContinue;

  std::istringstream stream(xml);
  auto result = XmlFeedParser::ParseStream(stream, opts);
  ASSERT_TRUE(result.ok()) << result.status().ToString();

  auto& pr = result.value();
  auto teams = pr.store.ListObjects(ObjectType::kTeams);
  EXPECT_EQ(teams.size(), 2u);
  auto problems = pr.store.ListObjects(ObjectType::kProblems);
  EXPECT_EQ(problems.size(), 1u);
}

TEST(XmlFeedParserTest, HandlesEmptyXml) {
  const std::string xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<contest>
</contest>
)";

  ParseOptions opts;
  opts.version = ApiVersion::k2023_06;

  std::istringstream stream(xml);
  auto result = XmlFeedParser::ParseStream(stream, opts);
  ASSERT_TRUE(result.ok()) << result.status().ToString();
  EXPECT_EQ(result.value().cursor.event_count, 0u);
}

TEST(XmlFeedParserTest, HandlesInvalidXml) {
  const std::string xml = "not xml at all <broken";

  ParseOptions opts;
  std::istringstream stream(xml);
  auto result = XmlFeedParser::ParseStream(stream, opts);
  EXPECT_FALSE(result.ok());
}

TEST(XmlFeedParserTest, NumericIdsStayStrings) {
  // Verify that numeric IDs like "1" stay as strings, not integers.
  const std::string xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<contest>
  <team>
    <id>1</id><name>Team One</name>
  </team>
  <team>
    <id>2</id><name>Team Two</name>
  </team>
</contest>
)";

  ParseOptions opts;
  opts.version = ApiVersion::k2023_06;

  std::istringstream stream(xml);
  auto result = XmlFeedParser::ParseStream(stream, opts);
  ASSERT_TRUE(result.ok()) << result.status().ToString();

  auto teams = result.value().store.ListObjects(ObjectType::kTeams);
  EXPECT_EQ(teams.size(), 2u);

  // Check that we can find team by string id "1".
  auto t1 = result.value().store.GetObject(ObjectType::kTeams, "1");
  EXPECT_TRUE(t1.ok()) << t1.status().ToString();
}

}  // namespace
}  // namespace ccsparser
