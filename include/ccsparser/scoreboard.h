// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

// Scoreboard data model — presentation-agnostic structures for contest
// standings.  Designed for reuse by resolvers, HTML exporters, and other
// downstream consumers.

#ifndef CCSPARSER_SCOREBOARD_H_
#define CCSPARSER_SCOREBOARD_H_

#include <cstdint>
#include <string>
#include <vector>

namespace ccsparser {

// Status of a single problem cell on the scoreboard.
enum class ProblemStatus {
  kSolved,
  kFailed,
  kEmpty,
};

// A single team-award association.
struct TeamAward {
  std::string award_id;
  std::string citation;
};

// Result cell for one problem on one team's row.
struct ProblemResultCell {
  std::string problem_id;
  std::string label;
  ProblemStatus status = ProblemStatus::kEmpty;
  int attempts = 0;
  // Contest-time in minutes at which the problem was accepted (0 if unsolved).
  int64_t time_minutes = 0;
  bool is_first_to_solve = false;
};

// One row on the scoreboard.
struct ScoreboardRow {
  int place = 0;
  std::string team_id;
  std::string team_name;
  std::string organization_id;
  std::string organization_name;
  int solved = 0;
  int64_t penalty = 0;
  std::vector<ProblemResultCell> cells;
  std::vector<TeamAward> awards;
};

// Complete scoreboard snapshot.
struct ScoreboardSnapshot {
  std::string contest_name;
  std::string contest_formal_name;
  int64_t penalty_time_minutes = 20;

  // Ordered list of problem labels/ids for column headers.
  struct ProblemColumn {
    std::string problem_id;
    std::string label;
    std::string name;
    std::string rgb;  // e.g. "#ff0000".
  };
  std::vector<ProblemColumn> problems;

  // Rows sorted by rank (best first).
  std::vector<ScoreboardRow> rows;
};

}  // namespace ccsparser

#endif  // CCSPARSER_SCOREBOARD_H_
