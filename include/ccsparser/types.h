// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

#ifndef CCSPARSER_TYPES_H_
#define CCSPARSER_TYPES_H_

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ccsparser {

// CCS API version.
enum class ApiVersion {
  kAuto,
  k2023_06,
  k2026_01,
};

// Error handling policy.
enum class ErrorPolicy {
  kFailFast,
  kContinue,
};

// Policy for unknown JSON fields in objects.
enum class UnknownFieldPolicy {
  kPreserve,
  kDrop,
  kError,
};

// Policy for unknown event types.
enum class UnknownTypePolicy {
  kWarnAndIgnore,
  kPreserveRaw,
  kError,
};

// Object types in the CCS specification.
enum class ObjectType {
  kContest,
  kJudgementTypes,
  kLanguages,
  kProblems,
  kGroups,
  kOrganizations,
  kTeams,
  kPersons,
  kAccounts,
  kState,
  kSubmissions,
  kJudgements,
  kRuns,
  kClarifications,
  kAwards,
  kCommentary,
  kUnknown,
};

// Convert type string to ObjectType enum.
ObjectType ObjectTypeFromString(std::string_view type_str);

// Convert ObjectType enum to type string.
std::string_view ObjectTypeToString(ObjectType type);

// Whether the object type is a singleton (no id).
bool IsSingleton(ObjectType type);

// Relative time representation (internally stored as milliseconds).
struct RelativeTime {
  int64_t milliseconds = 0;

  bool is_negative() const { return milliseconds < 0; }
  std::string ToString() const;

  bool operator==(const RelativeTime& other) const {
    return milliseconds == other.milliseconds;
  }
  bool operator!=(const RelativeTime& other) const {
    return !(*this == other);
  }
  bool operator<(const RelativeTime& other) const {
    return milliseconds < other.milliseconds;
  }
};

// Absolute time representation (ISO8601).
struct AbsoluteTime {
  // Stored as milliseconds since Unix epoch.
  int64_t epoch_ms = 0;
  // Original timezone offset in minutes for faithful round-trip.
  int tz_offset_minutes = 0;

  std::string ToString() const;

  bool operator==(const AbsoluteTime& other) const {
    return epoch_ms == other.epoch_ms;
  }
  bool operator!=(const AbsoluteTime& other) const {
    return !(*this == other);
  }
};

// File reference in a contest object.
struct FileRef {
  std::string href;
  std::optional<std::string> filename;
  std::optional<std::string> mime;
  std::optional<int> width;
  std::optional<int> height;
  std::vector<std::string> tags;
  std::optional<std::string> package_relative_path;
  // Unknown fields preserved as key-value pairs.
  std::vector<std::pair<std::string, std::string>> unknown_fields;
};

}  // namespace ccsparser

#endif  // CCSPARSER_TYPES_H_
