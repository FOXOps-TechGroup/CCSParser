// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

#ifndef CCSPARSER_PARSER_H_
#define CCSPARSER_PARSER_H_

#include <filesystem>
#include <istream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ccsparser/contest_store.h"
#include "ccsparser/diagnostic.h"
#include "ccsparser/event.h"
#include "ccsparser/http_client.h"
#include "ccsparser/parse_options.h"
#include "ccsparser/status.h"
#include "ccsparser/types.h"

namespace ccsparser {

// Result of a complete parse operation.
struct ParseResult {
  ApiVersion resolved_version = ApiVersion::kAuto;
  ContestStore store;
  EventCursor cursor;
  std::vector<Diagnostic> diagnostics;
};

// Streaming parse session for incremental line-by-line consumption.
class StreamingParseSession {
 public:
  ~StreamingParseSession();

  // Consume a single line of input.
  Status ConsumeLine(std::string_view line);

  // Signal end of input.
  Status Finish();

  const EventCursor& cursor() const;
  const ContestStore& store() const;
  ContestStore& mutable_store();
  const std::vector<Diagnostic>& diagnostics() const;
  ApiVersion resolved_version() const;

 private:
  friend class EventFeedParser;
  struct Impl;
  std::unique_ptr<Impl> impl_;
  explicit StreamingParseSession(std::unique_ptr<Impl> impl);
};

// Main event-feed parser.
class EventFeedParser {
 public:
  // Parse from a local file.
  static StatusOr<ParseResult> ParseFile(const std::filesystem::path& path,
                                         const ParseOptions& options);
  // Parse from a stream.
  static StatusOr<ParseResult> ParseStream(std::istream& stream,
                                           const ParseOptions& options);
  // Create a streaming session for incremental parsing.
  static StatusOr<std::unique_ptr<StreamingParseSession>>
  CreateStreamingSession(const ParseOptions& options);
};

// Package parser for contest package directories and zip files.
class PackageParser {
 public:
  // Parse from a contest package directory.
  static StatusOr<ParseResult> ParsePackageDirectory(
      const std::filesystem::path& path, const ParseOptions& options);

  // Parse from a contest package zip file.
  static StatusOr<ParseResult> ParsePackageZip(
      const std::filesystem::path& path, const ParseOptions& options);
};

// Options for REST API consumption.
struct RestApiOptions {
  // Base URL of the CCS API (e.g. "https://www.domjudge.org/demoweb/api/v4").
  std::string base_url;
  // Contest identifier (e.g. "nwerc18").
  std::string contest_id;
  // Optional HTTP Basic Auth credentials.
  std::optional<HttpBasicAuth> auth;
  // HTTP request timeout in seconds (0 = no timeout).
  int timeout_seconds = 60;
  // Whether to verify SSL certificates.
  bool verify_ssl = true;
};

// REST API parser — fetches CCS Contest API endpoints via HTTP.
class RestApiParser {
 public:
  // Fetch all REST API endpoints and build a complete ContestStore.
  // This fetches /contests/{id}, /teams, /problems, /submissions,
  // /judgements, /judgement-types, /organizations, /groups, /state, /awards.
  static StatusOr<ParseResult> FetchAndParse(
      const RestApiOptions& rest_opts, const ParseOptions& parse_opts);

  // Fetch the event-feed endpoint via HTTP and parse as NDJSON.
  // Falls back to the streaming NDJSON parser.
  static StatusOr<ParseResult> FetchEventFeed(
      const RestApiOptions& rest_opts, const ParseOptions& parse_opts);
};

// XML feed parser — reads old-style XML event feeds.
class XmlFeedParser {
 public:
  // Parse an XML event-feed from a file.
  static StatusOr<ParseResult> ParseFile(const std::filesystem::path& path,
                                         const ParseOptions& options);
  // Parse an XML event-feed from a stream.
  static StatusOr<ParseResult> ParseStream(std::istream& stream,
                                           const ParseOptions& options);
};

}  // namespace ccsparser

#endif  // CCSPARSER_PARSER_H_
