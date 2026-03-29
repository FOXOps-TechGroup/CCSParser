// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

// REST API parser — fetches CCS Contest API endpoints via HTTP and builds
// a ContestStore.  This is the HTTP Client / REST API consumption feature.
//
// Usage:
//   ccsparser::RestApiOptions rest_opts;
//   rest_opts.base_url = "https://www.domjudge.org/demoweb/api/v4";
//   rest_opts.contest_id = "nwerc18";
//   rest_opts.auth = HttpBasicAuth{"admin", "admin"};
//   auto result = RestApiParser::FetchAndParse(rest_opts, parse_opts);

#include "ccsparser/parser.h"

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "ccsparser/contest_store.h"
#include "ccsparser/diagnostic.h"
#include "ccsparser/http_client.h"
#include "ccsparser/objects.h"
#include "ccsparser/parse_options.h"
#include "ccsparser/status.h"
#include "ccsparser/types.h"
#include "src/profile/version_profile.h"

namespace ccsparser {

namespace {

// Fetch a JSON array or object from a REST API endpoint.
StatusOr<nlohmann::json> FetchJsonEndpoint(HttpClient& client,
                                           const std::string& url,
                                           const HttpRequestOptions& http_opts) {
  auto resp_or = client.Get(url, http_opts);
  if (!resp_or.ok()) return resp_or.status();

  auto& resp = resp_or.value();
  if (resp.body.empty()) {
    return nlohmann::json(nullptr);
  }

  try {
    return nlohmann::json::parse(resp.body);
  } catch (const nlohmann::json::parse_error& e) {
    return Status(StatusCode::kParseError,
                  "Failed to parse JSON from " + url + ": " + e.what());
  }
}

// Apply a JSON array of objects to the store using the profile decoder.
Status ApplyCollection(ContestStore& store,
                       internal::VersionProfile& profile,
                       ObjectType type,
                       const nlohmann::json& data,
                       const ParseOptions& opts,
                       std::vector<Diagnostic>& diags) {
  if (data.is_null()) return Status::Ok();

  if (data.is_array()) {
    auto cr = profile.DecodeCollection(type, data, 0, opts);
    if (!cr.ok()) return cr.status();
    for (auto& d : cr.value().diagnostics) {
      if (diags.size() < opts.limits.max_diagnostics) {
        diags.push_back(std::move(d));
      }
    }
    return store.ApplyCollectionReplace(type, std::move(cr.value().objects));
  }

  if (data.is_object()) {
    auto dr = profile.DecodeObject(type, data, 0, opts);
    if (!dr.ok()) return dr.status();
    for (auto& d : dr.value().diagnostics) {
      if (diags.size() < opts.limits.max_diagnostics) {
        diags.push_back(std::move(d));
      }
    }
    if (dr.value().object) {
      if (IsSingleton(type)) {
        return store.ApplySingletonUpdate(type, std::move(dr.value().object));
      } else {
        std::string id = dr.value().object->id;
        return store.ApplyUpsert(type, id, std::move(dr.value().object));
      }
    }
  }

  return Status::Ok();
}

}  // namespace

StatusOr<ParseResult> RestApiParser::FetchAndParse(
    const RestApiOptions& rest_opts, const ParseOptions& parse_opts) {
  HttpClient client;

  HttpRequestOptions http_opts;
  http_opts.timeout_seconds = rest_opts.timeout_seconds;
  http_opts.verify_ssl = rest_opts.verify_ssl;
  if (rest_opts.auth.has_value()) {
    http_opts.basic_auth = rest_opts.auth;
  }

  // Determine profile version.
  ApiVersion version = parse_opts.version;
  auto profile = internal::CreateProfile(
      version == ApiVersion::kAuto ? ApiVersion::k2023_06 : version);

  ParseResult result;
  result.resolved_version =
      version == ApiVersion::kAuto ? ApiVersion::k2023_06 : version;

  std::string base =
      rest_opts.base_url + "/contests/" + rest_opts.contest_id;

  // Endpoints to fetch, in dependency order.
  struct EndpointSpec {
    ObjectType type;
    std::string path;
    bool is_singleton;
    bool required;
  };

  std::vector<EndpointSpec> endpoints = {
      {ObjectType::kContest, "", true, true},
      {ObjectType::kState, "/state", true, false},
      {ObjectType::kJudgementTypes, "/judgement-types", false, true},
      {ObjectType::kLanguages, "/languages", false, false},
      {ObjectType::kProblems, "/problems", false, true},
      {ObjectType::kGroups, "/groups", false, false},
      {ObjectType::kOrganizations, "/organizations", false, false},
      {ObjectType::kTeams, "/teams", false, true},
      {ObjectType::kSubmissions, "/submissions", false, true},
      {ObjectType::kJudgements, "/judgements", false, true},
      {ObjectType::kRuns, "/runs", false, false},
      {ObjectType::kAwards, "/awards", false, false},
  };

  size_t event_count = 0;
  for (const auto& ep : endpoints) {
    std::string url = base + ep.path;

    auto json_or = FetchJsonEndpoint(client, url, http_opts);
    if (!json_or.ok()) {
      if (ep.required) return json_or.status();
      // Optional endpoint failed — skip silently (server may not support it).
      Diagnostic diag;
      diag.severity = Severity::kWarning;
      diag.code = DiagnosticCode::kMissingData;
      diag.message = "Failed to fetch optional endpoint: " + url +
                     " (" + json_or.status().message() + ")";
      result.diagnostics.push_back(std::move(diag));
      continue;
    }

    auto& json = json_or.value();
    if (json.is_null()) continue;

    // Auto-detect version from contest endpoint.
    if (ep.type == ObjectType::kContest && json.is_object() &&
        result.resolved_version == ApiVersion::k2023_06 &&
        parse_opts.version == ApiVersion::kAuto) {
      auto pt_it = json.find("penalty_time");
      if (pt_it != json.end() && pt_it->is_string()) {
        result.resolved_version = ApiVersion::k2026_01;
        profile = internal::CreateProfile(ApiVersion::k2026_01);
      }
    }

    // For contest endpoint, data is a single object or an array.
    // DOMjudge /contests/{id} returns a single object.
    auto status = ApplyCollection(result.store, *profile, ep.type, json,
                                  parse_opts, result.diagnostics);
    if (!status.ok() && ep.required) {
      return status;
    }

    event_count += json.is_array() ? json.size() : 1;
  }

  result.cursor.event_count = event_count;
  return result;
}

// ---- Streaming event-feed from HTTP ----

StatusOr<ParseResult> RestApiParser::FetchEventFeed(
    const RestApiOptions& rest_opts, const ParseOptions& parse_opts) {
  HttpClient client;

  HttpRequestOptions http_opts;
  http_opts.timeout_seconds = rest_opts.timeout_seconds;
  http_opts.verify_ssl = rest_opts.verify_ssl;
  if (rest_opts.auth.has_value()) {
    http_opts.basic_auth = rest_opts.auth;
  }

  std::string url =
      rest_opts.base_url + "/contests/" + rest_opts.contest_id + "/event-feed";

  // Download the full event-feed body (not truly streaming for simplicity).
  auto resp_or = client.Get(url, http_opts);
  if (!resp_or.ok()) return resp_or.status();

  // Parse the NDJSON body as a stream.
  std::istringstream stream(resp_or.value().body);
  return EventFeedParser::ParseStream(stream, parse_opts);
}

}  // namespace ccsparser
