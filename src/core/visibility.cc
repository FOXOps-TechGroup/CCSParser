// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

#include "ccsparser/visibility.h"

namespace ccsparser {

StatusOr<PublicJudgementVisibility>
EvaluatePublicJudgementVisibility(const Contest &contest, const State &state,
                                  const Submission &submission) {
  if (!contest.duration || contest.duration->milliseconds <= 0 ||
      !submission.contest_time) {
    return Status(StatusCode::kFailedPrecondition,
                  "Contest duration and submission contest time are required");
  }
  const int64_t duration = contest.duration->milliseconds;
  const int64_t time = submission.contest_time->milliseconds;
  if (contest.scoreboard_freeze_duration &&
      (contest.scoreboard_freeze_duration->milliseconds < 0 ||
       contest.scoreboard_freeze_duration->milliseconds > duration)) {
    return Status(StatusCode::kInvalidArgument,
                  "Invalid scoreboard freeze duration");
  }
  if (time < 0 || time >= duration)
    return PublicJudgementVisibility::kOutsideContest;
  if (contest.scoreboard_freeze_duration && !state.thawed &&
      time >= duration - contest.scoreboard_freeze_duration->milliseconds) {
    return PublicJudgementVisibility::kFrozen;
  }
  return PublicJudgementVisibility::kVisible;
}

} // namespace ccsparser
