// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

#include "ccsparser/file_ref_resolver.h"

#include <filesystem>

namespace ccsparser {

FileRefResolver::FileRefResolver(std::filesystem::path package_root)
    : package_root_(std::move(package_root)) {}

StatusOr<std::filesystem::path> FileRefResolver::ResolveToLocalPath(
    const FileRef& ref, const std::string& endpoint,
    const std::string& object_id) const {
  // Strategy: try href-based resolution, then pattern-based.

  // 1. If href is a local relative path, resolve directly.
  if (!ref.href.empty()) {
    auto local_path = package_root_ / ref.href;
    if (std::filesystem::exists(local_path)) {
      return local_path;
    }
  }

  // 2. Try pattern: <endpoint>/<id>/<filename>
  if (ref.filename.has_value() && !ref.filename->empty()) {
    auto pattern_path =
        package_root_ / endpoint / object_id / *ref.filename;
    if (std::filesystem::exists(pattern_path)) {
      return pattern_path;
    }
  }

  // 3. Try href as filename under endpoint/id.
  if (!ref.href.empty()) {
    // Extract filename from href.
    auto slash_pos = ref.href.rfind('/');
    std::string filename =
        (slash_pos != std::string::npos) ? ref.href.substr(slash_pos + 1)
                                          : ref.href;
    auto alt_path = package_root_ / endpoint / object_id / filename;
    if (std::filesystem::exists(alt_path)) {
      return alt_path;
    }
  }

  return Status(StatusCode::kNotFound,
                "Cannot resolve file reference: " + ref.href +
                    " for " + endpoint + "/" + object_id);
}

bool FileRefResolver::Exists(const FileRef& ref, const std::string& endpoint,
                              const std::string& object_id) const {
  auto result = ResolveToLocalPath(ref, endpoint, object_id);
  return result.ok();
}

}  // namespace ccsparser
