// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

#ifndef CCSPARSER_PARSE_OPTIONS_H_
#define CCSPARSER_PARSE_OPTIONS_H_

#include <cstddef>

#include "ccsparser/types.h"

namespace ccsparser {

// Limits for defensive parsing.
struct ParseLimits {
  size_t max_line_bytes = 64 * 1024 * 1024;   // 64 MB
  size_t max_diagnostics = 10000;
  size_t max_consecutive_errors = 100;
};

// Options controlling parser behavior.
struct ParseOptions {
  ApiVersion version = ApiVersion::kAuto;
  ErrorPolicy error_policy = ErrorPolicy::kContinue;
  UnknownFieldPolicy unknown_field_policy = UnknownFieldPolicy::kPreserve;
  UnknownTypePolicy unknown_type_policy = UnknownTypePolicy::kWarnAndIgnore;
  bool keep_event_log = true;
  bool keep_raw_json = false;
  bool enable_validation = true;
  bool enable_checkpointing = true;
  ParseLimits limits;
};

}  // namespace ccsparser

#endif  // CCSPARSER_PARSE_OPTIONS_H_
