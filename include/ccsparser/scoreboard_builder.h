// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

// ScoreboardBuilder — constructs a final ICPC-style scoreboard from a
// populated ContestStore.  The builder is stateless; all inputs come from
// the store and all outputs go to a ScoreboardSnapshot.

#ifndef CCSPARSER_SCOREBOARD_BUILDER_H_
#define CCSPARSER_SCOREBOARD_BUILDER_H_

#include "ccsparser/contest_store.h"
#include "ccsparser/scoreboard.h"
#include "ccsparser/status.h"

namespace ccsparser {

// Builds a ScoreboardSnapshot from the current state of a ContestStore.
//
// The returned snapshot represents the **final** standings (not a
// freeze-time or resolver-animation view).  Ranking follows standard
// ICPC rules:
//   1. solved (descending)
//   2. penalty (ascending)
//   3. team_id (ascending, stable tie-break)
StatusOr<ScoreboardSnapshot> BuildScoreboard(const ContestStore& store);

}  // namespace ccsparser

#endif  // CCSPARSER_SCOREBOARD_BUILDER_H_
