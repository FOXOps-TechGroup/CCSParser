// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

#ifndef CCSPARSER_INTERNAL_VERSION_PROFILE_H_
#define CCSPARSER_INTERNAL_VERSION_PROFILE_H_

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "ccsparser/diagnostic.h"
#include "ccsparser/objects.h"
#include "ccsparser/parse_options.h"
#include "ccsparser/status.h"
#include "ccsparser/types.h"

namespace ccsparser {
namespace internal {

// Decode result for a single object.
struct DecodeResult {
  std::unique_ptr<ContestObject> object;
  std::vector<Diagnostic> diagnostics;
};

// Decode result for a collection (array of objects).
struct CollectionDecodeResult {
  std::vector<std::unique_ptr<ContestObject>> objects;
  std::vector<Diagnostic> diagnostics;
};

// Abstract version profile for decoding objects per CCS version.
class VersionProfile {
 public:
  virtual ~VersionProfile() = default;

  virtual ApiVersion version() const = 0;

  // Decode a single object from a pre-parsed JSON value.
  virtual StatusOr<DecodeResult> DecodeObject(
      ObjectType type, const nlohmann::json& data, size_t line_no,
      const ParseOptions& options) const = 0;

  // Decode a collection (array) from a pre-parsed JSON value.
  virtual StatusOr<CollectionDecodeResult> DecodeCollection(
      ObjectType type, const nlohmann::json& data, size_t line_no,
      const ParseOptions& options) const = 0;
};

// Create a profile for the given version.
std::unique_ptr<VersionProfile> CreateProfile(ApiVersion version);

}  // namespace internal
}  // namespace ccsparser

#endif  // CCSPARSER_INTERNAL_VERSION_PROFILE_H_
