// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

#ifndef CCSPARSER_INTERNAL_TIME_UTILS_H_
#define CCSPARSER_INTERNAL_TIME_UTILS_H_

#include "ccsparser/status.h"
#include "ccsparser/types.h"

#include <string_view>

namespace ccsparser {
namespace internal {

// Parse a RELTIME string: (-)?(h)*h:mm:ss(.uuu)?
StatusOr<RelativeTime> ParseRelTime(std::string_view s);

// Create RelativeTime from integer minutes (CCS 2023-06 penalty_time).
RelativeTime RelTimeFromMinutes(int minutes);

// Parse an ISO8601 absolute time: yyyy-mm-ddThh:mm:ss(.uuu)?[+-]zz(:mm)?
StatusOr<AbsoluteTime> ParseAbsTime(std::string_view s);

}  // namespace internal
}  // namespace ccsparser

#endif  // CCSPARSER_INTERNAL_TIME_UTILS_H_
