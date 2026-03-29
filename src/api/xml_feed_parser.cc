// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

// XML feed parser — reads old-style XML event feeds (pre-CCS-spec NDJSON
// format, as used by older ICPC contest systems) and maps them to the
// standard CCSParser object model.
//
// Supported XML structures:
//   1. DOMjudge-style XML event feed (op-based: create/update/delete)
//   2. Legacy ICPC CDS XML contest feed
//   3. Generic XML with recognized element names
//
// The parser creates synthetic NDJSON-like events and feeds them through
// the standard decoder pipeline.

#include "ccsparser/parser.h"

#include <fstream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>
#include <pugixml.hpp>

#include "ccsparser/contest_store.h"
#include "ccsparser/diagnostic.h"
#include "ccsparser/parse_options.h"
#include "ccsparser/status.h"
#include "ccsparser/types.h"
#include "src/profile/version_profile.h"

namespace ccsparser {

namespace {

using json = nlohmann::json;

// Map XML element names to CCS object types.
struct XmlTypeMapping {
  const char* xml_name;
  ObjectType type;
  bool is_singleton;
};

const XmlTypeMapping kTypeMappings[] = {
    {"contest", ObjectType::kContest, true},
    {"info", ObjectType::kContest, true},
    {"judgement-type", ObjectType::kJudgementTypes, false},
    {"judgement_type", ObjectType::kJudgementTypes, false},
    {"language", ObjectType::kLanguages, false},
    {"problem", ObjectType::kProblems, false},
    {"group", ObjectType::kGroups, false},
    {"organization", ObjectType::kOrganizations, false},
    {"team", ObjectType::kTeams, false},
    {"submission", ObjectType::kSubmissions, false},
    {"judgement", ObjectType::kJudgements, false},
    {"run", ObjectType::kRuns, false},
    {"clarification", ObjectType::kClarifications, false},
    {"award", ObjectType::kAwards, false},
    {"state", ObjectType::kState, true},
};

const XmlTypeMapping* FindMapping(const std::string& name) {
  for (const auto& m : kTypeMappings) {
    if (name == m.xml_name) return &m;
  }
  return nullptr;
}

// Convert an XML node's children to a JSON object.
// Handles simple text elements and attributes.
// Fields that represent IDs are always kept as strings (CCS spec requires
// string IDs even when they happen to look numeric).
static const std::unordered_set<std::string> kStringFields = {
    "id",          "team_id",      "problem_id",       "language_id",
    "group_id",    "organization_id", "judgement_id",  "submission_id",
    "judgement_type_id", "person_id", "account_id",   "reply_to_id",
    "from_team_id", "to_team_id",  "source_id",       "icpc_id",
    "label",       "entry_point",  "name",             "formal_name",
    "shortname",   "short_name",   "citation",         "username",
    "type",        "start_time",   "end_time",         "time",
    "contest_time","start_contest_time", "end_contest_time",
    "duration",    "scoreboard_freeze_duration", "penalty_time",
    "rgb",         "color",        "text",             "message",
    "href",        "filename",     "mime",
};

json XmlNodeToJson(const pugi::xml_node& node) {
  json obj = json::object();

  // Add attributes.
  for (const auto& attr : node.attributes()) {
    obj[attr.name()] = attr.value();
  }

  // Add child elements.
  for (const auto& child : node.children()) {
    if (child.type() != pugi::node_element) continue;

    std::string name = child.name();

    // Normalize hyphens to underscores for CCS field names.
    for (auto& c : name) {
      if (c == '-') c = '_';
    }

    // Check if child has sub-elements (nested object) or just text.
    bool has_children = false;
    for (const auto& gc : child.children()) {
      if (gc.type() == pugi::node_element) {
        has_children = true;
        break;
      }
    }

    if (has_children) {
      obj[name] = XmlNodeToJson(child);
    } else {
      std::string text = child.text().as_string();

      // Known string fields — never coerce to number.
      if (kStringFields.count(name)) {
        obj[name] = text;
        continue;
      }

      // Try to parse as number.
      if (!text.empty()) {
        char* end = nullptr;
        long lval = std::strtol(text.c_str(), &end, 10);
        if (end && *end == '\0') {
          obj[name] = lval;
          continue;
        }
        double dval = std::strtod(text.c_str(), &end);
        if (end && *end == '\0') {
          obj[name] = dval;
          continue;
        }
        // Boolean.
        if (text == "true" || text == "True") {
          obj[name] = true;
          continue;
        }
        if (text == "false" || text == "False") {
          obj[name] = false;
          continue;
        }
      }
      obj[name] = text;
    }
  }

  return obj;
}

// Handle DOMjudge-style op-based XML events.
// Format: <event><op>create</op><type>team</type><data>...</data></event>
bool IsOpBasedEvent(const pugi::xml_node& node) {
  return node.child("op") || node.child("type");
}

struct XmlEvent {
  std::string op;       // "create", "update", "delete"
  std::string type;     // e.g. "team", "submission"
  json data;
};

XmlEvent ParseOpBasedEvent(const pugi::xml_node& node) {
  XmlEvent ev;
  if (auto op_node = node.child("op")) {
    ev.op = op_node.text().as_string();
  }
  if (auto type_node = node.child("type")) {
    ev.type = type_node.text().as_string();
  }
  if (auto data_node = node.child("data")) {
    ev.data = XmlNodeToJson(data_node);
  } else {
    ev.data = XmlNodeToJson(node);
    // Remove op and type from data.
    ev.data.erase("op");
    ev.data.erase("type");
  }
  return ev;
}

// Map legacy field names to CCS standard names.
void NormalizeLegacyFields(json& obj, ObjectType type) {
  // Common normalizations.
  auto rename = [&](const char* from, const char* to) {
    if (obj.contains(from) && !obj.contains(to)) {
      obj[to] = obj[from];
      obj.erase(from);
    }
  };

  switch (type) {
    case ObjectType::kContest:
      rename("length", "duration");
      rename("title", "name");
      rename("penalty", "penalty_time");
      break;
    case ObjectType::kTeams:
      rename("university", "organization_id");
      rename("nationality", "country");
      break;
    case ObjectType::kSubmissions:
      // team_id, problem_id, language_id already use standard CCS names.
      break;
    case ObjectType::kJudgements:
      rename("submission_id", "submission_id");
      rename("outcome", "judgement_type_id");
      break;
    default:
      break;
  }
}

StatusOr<ParseResult> ParseXmlDocument(const pugi::xml_document& doc,
                                        const ParseOptions& options) {
  auto profile = internal::CreateProfile(
      options.version == ApiVersion::kAuto ? ApiVersion::k2023_06
                                          : options.version);

  ParseResult result;
  result.resolved_version =
      options.version == ApiVersion::kAuto ? ApiVersion::k2023_06
                                          : options.version;

  auto root = doc.first_child();
  if (!root) {
    return Status(StatusCode::kParseError, "Empty XML document");
  }

  size_t event_count = 0;

  // Strategy 1: Op-based events (DOMjudge style).
  // <events><event><op>create</op><type>team</type><data>...</data></event>...
  bool found_op_events = false;
  for (const auto& child : root.children()) {
    if (std::string(child.name()) == "event" && IsOpBasedEvent(child)) {
      found_op_events = true;
      auto ev = ParseOpBasedEvent(child);

      const auto* mapping = FindMapping(ev.type);
      if (!mapping) {
        Diagnostic diag;
        diag.severity = Severity::kWarning;
        diag.code = DiagnosticCode::kUnknownEventType;
        diag.message = "Unknown XML event type: " + ev.type;
        result.diagnostics.push_back(std::move(diag));
        continue;
      }

      NormalizeLegacyFields(ev.data, mapping->type);

      if (ev.op == "delete") {
        auto id_it = ev.data.find("id");
        if (id_it != ev.data.end() && id_it->is_string()) {
          result.store.ApplyDelete(mapping->type, id_it->get<std::string>());
        }
      } else {
        auto dr = profile->DecodeObject(mapping->type, ev.data, event_count,
                                        options);
        if (dr.ok() && dr.value().object) {
          for (auto& d : dr.value().diagnostics) {
            result.diagnostics.push_back(std::move(d));
          }
          if (mapping->is_singleton) {
            result.store.ApplySingletonUpdate(mapping->type,
                                              std::move(dr.value().object));
          } else {
            std::string id = dr.value().object->id;
            result.store.ApplyUpsert(mapping->type, id,
                                     std::move(dr.value().object));
          }
        }
      }
      event_count++;
    }
  }

  if (found_op_events) {
    result.cursor.event_count = event_count;
    return result;
  }

  // Strategy 2: Flat structure — root element contains typed children.
  // <contest><info>...</info><team>...</team><team>...</team>...
  for (const auto& child : root.children()) {
    if (child.type() != pugi::node_element) continue;

    std::string name = child.name();
    const auto* mapping = FindMapping(name);
    if (!mapping) {
      // Try pluralized name (e.g. "teams" → "team").
      if (!name.empty() && name.back() == 's') {
        // Could be a container element with children.
        std::string singular = name.substr(0, name.size() - 1);
        const auto* inner_mapping = FindMapping(singular);
        if (inner_mapping) {
          // Process children of this container.
          for (const auto& inner_child : child.children()) {
            if (inner_child.type() != pugi::node_element) continue;
            json obj = XmlNodeToJson(inner_child);
            NormalizeLegacyFields(obj, inner_mapping->type);

            auto dr = profile->DecodeObject(inner_mapping->type, obj,
                                            event_count, options);
            if (dr.ok() && dr.value().object) {
              for (auto& d : dr.value().diagnostics) {
                result.diagnostics.push_back(std::move(d));
              }
              std::string id = dr.value().object->id;
              result.store.ApplyUpsert(inner_mapping->type, id,
                                       std::move(dr.value().object));
            }
            event_count++;
          }
          continue;
        }
      }
      // Unknown element — skip.
      continue;
    }

    json obj = XmlNodeToJson(child);
    NormalizeLegacyFields(obj, mapping->type);

    auto dr = profile->DecodeObject(mapping->type, obj, event_count, options);
    if (dr.ok() && dr.value().object) {
      for (auto& d : dr.value().diagnostics) {
        result.diagnostics.push_back(std::move(d));
      }
      if (mapping->is_singleton) {
        result.store.ApplySingletonUpdate(mapping->type,
                                          std::move(dr.value().object));
      } else {
        std::string id = dr.value().object->id;
        result.store.ApplyUpsert(mapping->type, id,
                                 std::move(dr.value().object));
      }
    }
    event_count++;
  }

  result.cursor.event_count = event_count;
  return result;
}

}  // namespace

StatusOr<ParseResult> XmlFeedParser::ParseFile(
    const std::filesystem::path& path, const ParseOptions& options) {
  pugi::xml_document doc;
  pugi::xml_parse_result xml_result = doc.load_file(path.c_str());
  if (!xml_result) {
    return Status(StatusCode::kParseError,
                  "Failed to parse XML file: " + path.string() + " — " +
                      xml_result.description());
  }
  return ParseXmlDocument(doc, options);
}

StatusOr<ParseResult> XmlFeedParser::ParseStream(
    std::istream& stream, const ParseOptions& options) {
  std::string content((std::istreambuf_iterator<char>(stream)),
                      std::istreambuf_iterator<char>());
  pugi::xml_document doc;
  pugi::xml_parse_result xml_result =
      doc.load_buffer(content.data(), content.size());
  if (!xml_result) {
    return Status(StatusCode::kParseError,
                  std::string("Failed to parse XML: ") +
                      xml_result.description());
  }
  return ParseXmlDocument(doc, options);
}

}  // namespace ccsparser
