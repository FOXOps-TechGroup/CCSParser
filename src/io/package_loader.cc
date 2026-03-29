// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

#include "ccsparser/parser.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "ccsparser/diagnostic.h"
#include "ccsparser/status.h"

namespace ccsparser {

StatusOr<ParseResult> PackageParser::ParsePackageDirectory(
    const std::filesystem::path& path, const ParseOptions& options) {
  if (!std::filesystem::is_directory(path)) {
    return Status(StatusCode::kInvalidArgument,
                  "Not a directory: " + path.string());
  }

  // Look for event-feed.ndjson first.
  auto event_feed_path = path / "event-feed.ndjson";
  if (std::filesystem::exists(event_feed_path)) {
    return EventFeedParser::ParseFile(event_feed_path, options);
  }

  // Fallback: look for contest.json and build from individual endpoint files.
  auto contest_json_path = path / "contest.json";
  if (!std::filesystem::exists(contest_json_path)) {
    return Status(StatusCode::kNotFound,
                  "No event-feed.ndjson or contest.json in package: " +
                      path.string());
  }

  // Build a synthetic event-feed from endpoint files.
  std::ostringstream synthetic_feed;

  // State event (empty).
  synthetic_feed << R"({"type":"state","data":{}})" << "\n";

  // Contest.
  {
    std::ifstream f(contest_json_path);
    if (f.is_open()) {
      std::string content((std::istreambuf_iterator<char>(f)),
                          std::istreambuf_iterator<char>());
      synthetic_feed << R"({"type":"contest","id":null,"data":)" << content
                     << "}\n";
    }
  }

  // Load other endpoint files.
  auto try_endpoint = [&](const std::string& name) {
    auto json_path = path / (name + ".json");
    if (std::filesystem::exists(json_path)) {
      std::ifstream f(json_path);
      if (f.is_open()) {
        std::string content((std::istreambuf_iterator<char>(f)),
                            std::istreambuf_iterator<char>());
        // Endpoint files contain arrays (collection replace).
        synthetic_feed << R"({"type":")" << name
                       << R"(","id":null,"data":)" << content << "}\n";
      }
    }
  };

  try_endpoint("judgement-types");
  try_endpoint("languages");
  try_endpoint("problems");
  try_endpoint("groups");
  try_endpoint("organizations");
  try_endpoint("teams");
  try_endpoint("persons");
  try_endpoint("accounts");
  try_endpoint("submissions");
  try_endpoint("judgements");
  try_endpoint("runs");
  try_endpoint("clarifications");
  try_endpoint("awards");
  try_endpoint("commentary");

  std::istringstream stream(synthetic_feed.str());
  return EventFeedParser::ParseStream(stream, options);
}

StatusOr<ParseResult> PackageParser::ParsePackageZip(
    const std::filesystem::path& path, const ParseOptions& options) {
  // Minimal zip reading: look for event-feed.ndjson in the zip.
  // We use a basic approach: extract using standard library + system tools
  // or a minimal zip reader.

  if (!std::filesystem::exists(path)) {
    return Status(StatusCode::kNotFound,
                  "Zip file not found: " + path.string());
  }

  // Create a temporary directory for extraction.
  auto temp_dir = std::filesystem::temp_directory_path() / "ccsparser_zip";
  std::filesystem::create_directories(temp_dir);

  // Use system unzip if available (simple, portable approach).
  std::string cmd =
      "unzip -o -q " + path.string() + " -d " + temp_dir.string();
  int ret = std::system(cmd.c_str());
  if (ret != 0) {
    std::filesystem::remove_all(temp_dir);
    return Status(StatusCode::kInternal,
                  "Failed to extract zip file: " + path.string());
  }

  auto result = ParsePackageDirectory(temp_dir, options);

  // Clean up.
  std::filesystem::remove_all(temp_dir);

  return result;
}

}  // namespace ccsparser
