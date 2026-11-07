// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

#ifndef CCSPARSER_VISIBILITY_H_
#define CCSPARSER_VISIBILITY_H_

#include "ccsparser/objects.h"
#include "ccsparser/status.h"

namespace ccsparser {

enum class PublicJudgementVisibility { kVisible, kFrozen, kOutsideContest };

// Evaluates the freeze boundary for one submission. This is one policy input,
// not a complete public-data projection or authorization decision.
StatusOr<PublicJudgementVisibility>
EvaluatePublicJudgementVisibility(const Contest &contest, const State &state,
                                  const Submission &submission);

} // namespace ccsparser

#endif // CCSPARSER_VISIBILITY_H_
