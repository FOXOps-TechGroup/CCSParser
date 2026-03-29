// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

#ifndef CCSPARSER_INTERNAL_NDJSON_LINE_PARSER_H_
#define CCSPARSER_INTERNAL_NDJSON_LINE_PARSER_H_

#include <nlohmann/json.hpp>
#include <string_view>
#include <variant>
#include <vector>

#include "ccsparser/diagnostic.h"
#include "ccsparser/event.h"
#include "ccsparser/parse_options.h"
#include "ccsparser/status.h"

namespace ccsparser {
namespace internal {

// Result of parsing a single NDJSON line.
struct LineParseResult {
  bool is_keepalive = false;
  RawEvent event;
  // Pre-parsed JSON of the "data" field. Passed downstream to decoders
  // so that JSON is parsed exactly once per line.
  nlohmann::json parsed_data;
};

// Parse a single line of NDJSON event-feed.
// Returns a RawEvent on success, or a diagnostic on failure.
// For empty/whitespace lines (keepalive), returns a result with is_keepalive =
// true.
StatusOr<LineParseResult> ParseNdjsonLine(std::string_view line,
                                          size_t line_no,
                                          const ParseOptions& options);

}  // namespace internal
}  // namespace ccsparser

#endif  // CCSPARSER_INTERNAL_NDJSON_LINE_PARSER_H_
