// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

#include "ccsparser/visibility.h"

#include "gtest/gtest.h"

namespace ccsparser {
namespace {

TEST(PublicVisibilityTest, FreezeBoundaryIsInclusiveUntilThaw) {
  Contest contest;
  contest.duration = RelativeTime{300000};
  contest.scoreboard_freeze_duration = RelativeTime{60000};
  State state;
  Submission submission;
  submission.contest_time = RelativeTime{239999};
  EXPECT_EQ(*EvaluatePublicJudgementVisibility(contest, state, submission),
            PublicJudgementVisibility::kVisible);
  submission.contest_time = RelativeTime{240000};
  EXPECT_EQ(*EvaluatePublicJudgementVisibility(contest, state, submission),
            PublicJudgementVisibility::kFrozen);
  state.thawed = AbsoluteTime{123, 0};
  EXPECT_EQ(*EvaluatePublicJudgementVisibility(contest, state, submission),
            PublicJudgementVisibility::kVisible);
}

TEST(PublicVisibilityTest,
     RejectsMissingOrInvalidInputsAndExcludesOutsideContest) {
  Contest contest;
  State state;
  Submission submission;
  EXPECT_FALSE(
      EvaluatePublicJudgementVisibility(contest, state, submission).ok());
  contest.duration = RelativeTime{300000};
  submission.contest_time = RelativeTime{-1};
  EXPECT_EQ(*EvaluatePublicJudgementVisibility(contest, state, submission),
            PublicJudgementVisibility::kOutsideContest);
  submission.contest_time = RelativeTime{300000};
  EXPECT_EQ(*EvaluatePublicJudgementVisibility(contest, state, submission),
            PublicJudgementVisibility::kOutsideContest);
  submission.contest_time = RelativeTime{0};
  contest.scoreboard_freeze_duration = RelativeTime{300001};
  EXPECT_FALSE(
      EvaluatePublicJudgementVisibility(contest, state, submission).ok());
}

} // namespace
} // namespace ccsparser
