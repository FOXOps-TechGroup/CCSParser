// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

#ifndef CCSPARSER_EVENT_H_
#define CCSPARSER_EVENT_H_

#include <cstddef>
#include <optional>
#include <string>

#include "ccsparser/types.h"

namespace ccsparser {

// The shape of an event's data field.
enum class EventShape {
  kSingleObject,       // id != null, data is object -> upsert.
  kDelete,             // id != null, data == null -> delete.
  kCollectionReplace,  // id == null, data is array.
  kSingletonUpdate,    // id == null, data is object (contest/state).
};

// A raw parsed event before object decoding.
struct RawEvent {
  std::string type_str;
  ObjectType object_type = ObjectType::kUnknown;
  std::optional<std::string> id;
  EventShape shape = EventShape::kSingleObject;
  std::optional<std::string> token;
  size_t line_no = 0;

  // The raw JSON of the data field (object or array).
  // Empty for delete events.
  std::string data_json;

  // The full raw line (populated if keep_raw_json is true).
  std::optional<std::string> raw_line;
};

// Cursor tracking parser position.
struct EventCursor {
  size_t line_no = 0;
  size_t event_count = 0;
  std::optional<std::string> last_token;
  std::optional<std::string> last_event_id;
  bool end_of_updates = false;
};

}  // namespace ccsparser

#endif  // CCSPARSER_EVENT_H_
