// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

// Common decoder helpers shared between version profiles.

#ifndef CCSPARSER_INTERNAL_DECODER_COMMON_H_
#define CCSPARSER_INTERNAL_DECODER_COMMON_H_

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "ccsparser/diagnostic.h"
#include "ccsparser/objects.h"
#include "ccsparser/parse_options.h"
#include "ccsparser/status.h"
#include "ccsparser/types.h"

namespace ccsparser {
namespace internal {

using json = nlohmann::json;

// Helper to extract an optional string field.
inline std::optional<std::string> OptString(const json& obj,
                                            std::string_view key) {
  auto it = obj.find(key);
  if (it != obj.end() && it->is_string()) {
    return it->get<std::string>();
  }
  return std::nullopt;
}

// Helper to extract an optional bool field.
inline std::optional<bool> OptBool(const json& obj, std::string_view key) {
  auto it = obj.find(key);
  if (it != obj.end() && it->is_boolean()) {
    return it->get<bool>();
  }
  return std::nullopt;
}

// Helper to extract an optional int field.
inline std::optional<int> OptInt(const json& obj, std::string_view key) {
  auto it = obj.find(key);
  if (it != obj.end() && it->is_number_integer()) {
    return it->get<int>();
  }
  return std::nullopt;
}

// Helper to extract an optional double field.
inline std::optional<double> OptDouble(const json& obj,
                                       std::string_view key) {
  auto it = obj.find(key);
  if (it != obj.end() && it->is_number()) {
    return it->get<double>();
  }
  return std::nullopt;
}

// Helper to extract a string array.
inline std::vector<std::string> StringArray(const json& obj,
                                            std::string_view key) {
  std::vector<std::string> result;
  auto it = obj.find(key);
  if (it != obj.end() && it->is_array()) {
    result.reserve(it->size());
    for (const auto& elem : *it) {
      if (elem.is_string()) {
        result.push_back(elem.get<std::string>());
      }
    }
  }
  return result;
}

// Decode a FileRef from a JSON object.
FileRef DecodeFileRef(const json& obj);

// Decode an array of FileRef from a JSON array field.
std::vector<FileRef> DecodeFileRefArray(const json& parent,
                                        std::string_view key);

// Collect unknown fields from a JSON object, given a set of known keys.
UnknownFields CollectUnknownFields(
    const json& obj, const std::unordered_set<std::string_view>& known_keys);

// Parse an optional AbsoluteTime field.
std::optional<AbsoluteTime> OptAbsTime(const json& obj,
                                       const std::string& key,
                                       std::vector<Diagnostic>& diags,
                                       size_t line_no);

// Parse an optional RelativeTime from a RELTIME string field.
std::optional<RelativeTime> OptRelTime(const json& obj,
                                       const std::string& key,
                                       std::vector<Diagnostic>& diags,
                                       size_t line_no);

}  // namespace internal
}  // namespace ccsparser

#endif  // CCSPARSER_INTERNAL_DECODER_COMMON_H_
