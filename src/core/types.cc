// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

#include "ccsparser/types.h"

#include <array>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

namespace ccsparser {

namespace {

struct TypeMapping {
  std::string_view str;
  ObjectType type;
};

constexpr std::array<TypeMapping, 16> kTypeMappings = {{
    {"contest", ObjectType::kContest},
    {"judgement-types", ObjectType::kJudgementTypes},
    {"languages", ObjectType::kLanguages},
    {"problems", ObjectType::kProblems},
    {"groups", ObjectType::kGroups},
    {"organizations", ObjectType::kOrganizations},
    {"teams", ObjectType::kTeams},
    {"persons", ObjectType::kPersons},
    {"accounts", ObjectType::kAccounts},
    {"state", ObjectType::kState},
    {"submissions", ObjectType::kSubmissions},
    {"judgements", ObjectType::kJudgements},
    {"runs", ObjectType::kRuns},
    {"clarifications", ObjectType::kClarifications},
    {"awards", ObjectType::kAwards},
    {"commentary", ObjectType::kCommentary},
}};

}  // namespace

ObjectType ObjectTypeFromString(std::string_view type_str) {
  for (const auto& m : kTypeMappings) {
    if (m.str == type_str) return m.type;
  }
  return ObjectType::kUnknown;
}

std::string_view ObjectTypeToString(ObjectType type) {
  for (const auto& m : kTypeMappings) {
    if (m.type == type) return m.str;
  }
  return "unknown";
}

bool IsSingleton(ObjectType type) {
  return type == ObjectType::kContest || type == ObjectType::kState;
}

// --- RelativeTime ---

std::string RelativeTime::ToString() const {
  int64_t ms = milliseconds;
  bool neg = ms < 0;
  if (neg) ms = -ms;

  int64_t total_seconds = ms / 1000;
  int64_t frac_ms = ms % 1000;
  int64_t hours = total_seconds / 3600;
  int64_t minutes = (total_seconds % 3600) / 60;
  int64_t seconds = total_seconds % 60;

  char buf[64];
  if (frac_ms > 0) {
    std::snprintf(buf, sizeof(buf), "%s%" PRId64 ":%02" PRId64 ":%02" PRId64 ".%03" PRId64,
                  neg ? "-" : "", hours, minutes, seconds, frac_ms);
  } else {
    std::snprintf(buf, sizeof(buf), "%s%" PRId64 ":%02" PRId64 ":%02" PRId64,
                  neg ? "-" : "", hours, minutes, seconds);
  }
  return std::string(buf);
}

// --- AbsoluteTime ---

std::string AbsoluteTime::ToString() const {
  // Convert epoch_ms to broken-down time.
  int64_t adjusted_ms = epoch_ms + static_cast<int64_t>(tz_offset_minutes) * 60 * 1000;
  int64_t epoch_sec = adjusted_ms / 1000;
  int64_t frac_ms = adjusted_ms % 1000;
  if (frac_ms < 0) {
    frac_ms += 1000;
    epoch_sec -= 1;
  }

  time_t t = static_cast<time_t>(epoch_sec);
  struct tm tm_buf;
  gmtime_r(&t, &tm_buf);

  char buf[64];
  int tz_h = tz_offset_minutes / 60;
  int tz_m = std::abs(tz_offset_minutes % 60);

  if (frac_ms > 0) {
    std::snprintf(buf, sizeof(buf),
                  "%04d-%02d-%02dT%02d:%02d:%02d.%03d%+03d:%02d",
                  tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
                  tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec,
                  static_cast<int>(frac_ms), tz_h, tz_m);
  } else {
    std::snprintf(buf, sizeof(buf),
                  "%04d-%02d-%02dT%02d:%02d:%02d%+03d:%02d",
                  tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
                  tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec, tz_h, tz_m);
  }
  return std::string(buf);
}

}  // namespace ccsparser
