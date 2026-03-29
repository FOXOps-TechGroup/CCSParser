// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

#include "decoder_common.h"

#include <string>
#include <unordered_set>
#include <vector>

#include "src/core/time_utils.h"

namespace ccsparser {
namespace internal {

FileRef DecodeFileRef(const json& obj) {
  FileRef ref;
  ref.href = OptString(obj, "href").value_or("");
  ref.filename = OptString(obj, "filename");
  ref.mime = OptString(obj, "mime");
  ref.width = OptInt(obj, "width");
  ref.height = OptInt(obj, "height");
  ref.tags = StringArray(obj, "tag");

  // Collect unknown fields.
  static const std::unordered_set<std::string_view> kKnownFileRefKeys = {
      "href", "filename", "mime", "width", "height", "tag"};
  ref.unknown_fields.clear();
  for (const auto& [key, val] : obj.items()) {
    if (kKnownFileRefKeys.find(key) == kKnownFileRefKeys.end()) {
      ref.unknown_fields.push_back({key, val.dump()});
    }
  }
  return ref;
}

std::vector<FileRef> DecodeFileRefArray(const json& parent,
                                        std::string_view key) {
  std::vector<FileRef> result;
  auto it = parent.find(key);
  if (it != parent.end() && it->is_array()) {
    result.reserve(it->size());
    for (const auto& elem : *it) {
      if (elem.is_object()) {
        result.push_back(DecodeFileRef(elem));
      }
    }
  }
  return result;
}

UnknownFields CollectUnknownFields(
    const json& obj,
    const std::unordered_set<std::string_view>& known_keys) {
  UnknownFields unknown;
  for (const auto& [key, val] : obj.items()) {
    if (known_keys.find(key) == known_keys.end()) {
      unknown[key] = val.dump();
    }
  }
  return unknown;
}

std::optional<AbsoluteTime> OptAbsTime(const json& obj,
                                        const std::string& key,
                                        std::vector<Diagnostic>& diags,
                                        size_t line_no) {
  auto it = obj.find(key);
  if (it == obj.end() || it->is_null()) return std::nullopt;
  if (!it->is_string()) {
    diags.push_back(
        Diagnostic{Severity::kWarning, DiagnosticCode::kInvalidTime,
                   "Field '" + key + "' is not a string", line_no});
    return std::nullopt;
  }
  auto result = ParseAbsTime(it->get<std::string>());
  if (!result.ok()) {
    diags.push_back(
        Diagnostic{Severity::kWarning, DiagnosticCode::kInvalidTime,
                   "Invalid TIME in '" + key + "': " + result.status().message(),
                   line_no});
    return std::nullopt;
  }
  return result.value();
}

std::optional<RelativeTime> OptRelTime(const json& obj,
                                       const std::string& key,
                                       std::vector<Diagnostic>& diags,
                                       size_t line_no) {
  auto it = obj.find(key);
  if (it == obj.end() || it->is_null()) return std::nullopt;
  if (!it->is_string()) {
    diags.push_back(
        Diagnostic{Severity::kWarning, DiagnosticCode::kInvalidReltime,
                   "Field '" + key + "' is not a string", line_no});
    return std::nullopt;
  }
  auto result = ParseRelTime(it->get<std::string>());
  if (!result.ok()) {
    diags.push_back(
        Diagnostic{Severity::kWarning, DiagnosticCode::kInvalidReltime,
                   "Invalid RELTIME in '" + key + "': " +
                       result.status().message(),
                   line_no});
    return std::nullopt;
  }
  return result.value();
}

}  // namespace internal
}  // namespace ccsparser
