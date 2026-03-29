// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

#include "ndjson_line_parser.h"

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

namespace ccsparser {
namespace internal {

using json = nlohmann::json;

StatusOr<LineParseResult> ParseNdjsonLine(std::string_view line,
                                          size_t line_no,
                                          const ParseOptions& options) {
  // Trim trailing whitespace/CR.
  while (!line.empty() &&
         (line.back() == '\r' || line.back() == '\n' || line.back() == ' ' ||
          line.back() == '\t')) {
    line.remove_suffix(1);
  }

  // Empty line = keepalive.
  if (line.empty()) {
    LineParseResult result;
    result.is_keepalive = true;
    return result;
  }

  // Check line length limit.
  if (line.size() > options.limits.max_line_bytes) {
    return Status(StatusCode::kParseError,
                  "Line too long (" + std::to_string(line.size()) +
                      " bytes) at line " + std::to_string(line_no));
  }

  // Parse JSON.
  json doc;
  try {
    doc = json::parse(line);
  } catch (const json::parse_error& e) {
    return Status(StatusCode::kParseError,
                  "Malformed JSON at line " + std::to_string(line_no) + ": " +
                      e.what());
  }

  // Must be a JSON object.
  if (!doc.is_object()) {
    return Status(StatusCode::kParseError,
                  "Non-object JSON root at line " + std::to_string(line_no));
  }

  // Extract "type" field.
  auto type_it = doc.find("type");
  if (type_it == doc.end()) {
    return Status(StatusCode::kParseError,
                  "Missing 'type' field at line " + std::to_string(line_no));
  }
  if (!type_it->is_string()) {
    return Status(StatusCode::kParseError,
                  "Invalid 'type' field (not string) at line " +
                      std::to_string(line_no));
  }

  // Extract "data" field.
  auto data_it = doc.find("data");
  if (data_it == doc.end()) {
    return Status(StatusCode::kParseError,
                  "Missing 'data' field at line " + std::to_string(line_no));
  }

  // Extract "id" field (optional).
  auto id_it = doc.find("id");

  // Extract "token" field (optional).
  auto token_it = doc.find("token");

  RawEvent event;
  event.line_no = line_no;
  event.type_str = type_it->get<std::string>();
  event.object_type = ObjectTypeFromString(event.type_str);

  // Populate token.
  if (token_it != doc.end()) {
    if (token_it->is_string()) {
      event.token = token_it->get<std::string>();
    } else if (!token_it->is_null()) {
      return Status(StatusCode::kParseError,
                    "Invalid 'token' type at line " + std::to_string(line_no));
    }
  }

  // Populate raw_line if requested.
  if (options.keep_raw_json) {
    event.raw_line = std::string(line);
  }

  // Determine event shape.
  bool has_id = (id_it != doc.end() && !id_it->is_null());
  bool data_is_null = data_it->is_null();
  bool data_is_array = data_it->is_array();
  bool data_is_object = data_it->is_object();

  if (has_id) {
    // Validate id is string.
    if (!id_it->is_string()) {
      return Status(StatusCode::kParseError,
                    "Invalid 'id' type (not string) at line " +
                        std::to_string(line_no));
    }
    event.id = id_it->get<std::string>();

    if (data_is_null) {
      event.shape = EventShape::kDelete;
    } else if (data_is_object) {
      event.shape = EventShape::kSingleObject;
    } else {
      return Status(
          StatusCode::kParseError,
          "Invalid data shape for event with id at line " +
              std::to_string(line_no));
    }
  } else {
    // id is null or absent.
    if (data_is_array) {
      event.shape = EventShape::kCollectionReplace;
    } else if (data_is_object) {
      event.shape = EventShape::kSingletonUpdate;
    } else if (data_is_null) {
      // id=null, data=null: treat as empty singleton update (e.g. initial
      // state).
      event.shape = EventShape::kSingletonUpdate;
    } else {
      return Status(
          StatusCode::kParseError,
          "Invalid data shape for event without id at line " +
              std::to_string(line_no));
    }
  }

  LineParseResult result;
  // Move the pre-parsed "data" JSON into the result to avoid re-parsing
  // downstream. For delete events and null-data singletons, store an empty
  // object so decoders get a valid (albeit trivial) JSON value.
  if (data_is_null) {
    result.parsed_data = json::object();
  } else {
    result.parsed_data = std::move(*data_it);
  }
  result.event = std::move(event);
  return result;
}

}  // namespace internal
}  // namespace ccsparser
