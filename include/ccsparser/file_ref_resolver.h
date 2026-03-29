// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

#ifndef CCSPARSER_FILE_REF_RESOLVER_H_
#define CCSPARSER_FILE_REF_RESOLVER_H_

#include <filesystem>
#include <optional>
#include <string>

#include "ccsparser/status.h"
#include "ccsparser/types.h"

namespace ccsparser {

// Resolves file references in a contest package to local paths.
class FileRefResolver {
 public:
  explicit FileRefResolver(std::filesystem::path package_root);

  // Resolve a FileRef to a local filesystem path.
  StatusOr<std::filesystem::path> ResolveToLocalPath(
      const FileRef& ref, const std::string& endpoint,
      const std::string& object_id) const;

  // Check if a file reference can be resolved.
  bool Exists(const FileRef& ref, const std::string& endpoint,
              const std::string& object_id) const;

 private:
  std::filesystem::path package_root_;
};

}  // namespace ccsparser

#endif  // CCSPARSER_FILE_REF_RESOLVER_H_
