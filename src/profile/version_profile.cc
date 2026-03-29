// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

// Profile for CCS Spec 2023-06 and 2026-01. Both profiles share common
// decoding logic; version-specific differences are handled in dedicated
// decode paths.

#include "src/profile/version_profile.h"

#include <nlohmann/json.hpp>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "ccsparser/diagnostic.h"
#include "ccsparser/objects.h"
#include "ccsparser/parse_options.h"
#include "ccsparser/status.h"
#include "ccsparser/types.h"
#include "src/core/time_utils.h"
#include "src/profile/decoders/decoder_common.h"

namespace ccsparser {
namespace internal {

using json = nlohmann::json;

namespace {

// Known keys per object type (for unknown field collection).
// Using unordered_set<string_view> for O(1) lookup instead of linear scan.
const std::unordered_set<std::string_view> kContestKeys = {
    "id",
    "name",
    "formal_name",
    "start_time",
    "end_time",
    "duration",
    "scoreboard_freeze_duration",
    "scoreboard_thaw_time",
    "scoreboard_type",
    "penalty_time",
    "main_scoreboard_group_id",
    "banner",
    "logo",
    "problemset",
    "shortname",
    "external_id",
    "allow_submit",
    "runtime_as_score_tiebreaker",
    "warning_message",
};

const std::unordered_set<std::string_view> kJudgementTypeKeys = {
    "id", "name", "penalty", "solved", "simplified_judgement_type_id",
};

const std::unordered_set<std::string_view> kLanguageKeys = {
    "id",        "name",     "entry_point_required",
    "entry_point_name", "extensions", "compiler", "runner",
};

const std::unordered_set<std::string_view> kProblemKeys = {
    "id",          "ordinal",     "label",        "name",
    "rgb",         "color",       "time_limit",   "test_data_count",
    "memory_limit", "output_limit", "code_limit",  "uuid",
    "short_name",  "external_id", "statement",    "attachments",
};

const std::unordered_set<std::string_view> kGroupKeys = {
    "id", "name", "icpc_id", "type", "hidden", "sortorder", "color", "logo",
};

const std::unordered_set<std::string_view> kOrganizationKeys = {
    "id",
    "name",
    "formal_name",
    "icpc_id",
    "country",
    "country_flag",
    "url",
    "twitter_hashtag",
    "twitter_account",
    "country_subdivision",
    "logo",
    "country_subdivision_flag",
};

const std::unordered_set<std::string_view> kTeamKeys = {
    "id",         "name",           "display_name", "icpc_id",
    "organization_id", "group_ids", "hidden",       "photo",
    "video",      "backup",         "desktop",      "webcam",
    "audio",      "location_id",    "external_id",
};

const std::unordered_set<std::string_view> kPersonKeys = {
    "id", "name", "title", "email", "sex", "role", "team_id", "icpc_id",
    "photo",
};

const std::unordered_set<std::string_view> kAccountKeys = {
    "id", "username", "password", "type", "ip", "team_id", "person_id",
};

const std::unordered_set<std::string_view> kStateKeys = {
    "started", "ended", "frozen", "thawed", "finalized", "end_of_updates",
};

const std::unordered_set<std::string_view> kSubmissionKeys = {
    "id",         "language_id", "problem_id", "team_id",
    "account_id", "time",        "contest_time", "entry_point",
    "external_id", "files",      "reaction",
};

const std::unordered_set<std::string_view> kJudgementKeys = {
    "id",
    "submission_id",
    "judgement_type_id",
    "simplified_judgement_type_id",
    "start_time",
    "start_contest_time",
    "end_time",
    "end_contest_time",
    "max_run_time",
    "valid",
    "current",
};

const std::unordered_set<std::string_view> kRunKeys = {
    "id", "judgement_id", "ordinal", "judgement_type_id",
    "time", "contest_time", "run_time",
};

const std::unordered_set<std::string_view> kClarificationKeys2023 = {
    "id", "from_team_id", "to_team_id", "problem_id", "reply_to_id",
    "time", "contest_time", "text",
};

const std::unordered_set<std::string_view> kClarificationKeys2026 = {
    "id", "from_team_id", "to_team_ids", "to_group_ids", "problem_id",
    "reply_to_id", "time", "contest_time", "text",
};

const std::unordered_set<std::string_view> kAwardKeys = {
    "id", "citation", "team_ids", "parameters",
};

const std::unordered_set<std::string_view> kCommentaryKeys = {
    "id", "time", "contest_time", "message", "team_ids",
    "problem_ids", "submission_ids",
};

// Decode helpers for each object type.

std::unique_ptr<Contest> DecodeContest(const json& data, ApiVersion version,
                                       std::vector<Diagnostic>& diags,
                                       size_t line_no) {
  auto obj = std::make_unique<Contest>();
  obj->id = OptString(data, "id").value_or("");
  obj->name = OptString(data, "name");
  obj->formal_name = OptString(data, "formal_name");
  obj->start_time = OptAbsTime(data, "start_time", diags, line_no);
  obj->end_time = OptAbsTime(data, "end_time", diags, line_no);
  obj->duration = OptRelTime(data, "duration", diags, line_no);
  obj->scoreboard_freeze_duration =
      OptRelTime(data, "scoreboard_freeze_duration", diags, line_no);
  obj->scoreboard_thaw_time =
      OptAbsTime(data, "scoreboard_thaw_time", diags, line_no);
  obj->scoreboard_type = OptString(data, "scoreboard_type");
  obj->main_scoreboard_group_id =
      OptString(data, "main_scoreboard_group_id");
  obj->shortname = OptString(data, "shortname");
  obj->external_id = OptString(data, "external_id");
  obj->allow_submit = OptBool(data, "allow_submit");
  obj->runtime_as_score_tiebreaker =
      OptBool(data, "runtime_as_score_tiebreaker");
  obj->warning_message = OptString(data, "warning_message");

  // Version-specific penalty_time.
  auto pt_it = data.find("penalty_time");
  if (pt_it != data.end() && !pt_it->is_null()) {
    if (pt_it->is_number_integer()) {
      // 2023-06: integer minutes.
      obj->penalty_time = RelTimeFromMinutes(pt_it->get<int>());
    } else if (pt_it->is_string()) {
      // 2026-01: RELTIME string.
      auto rt = ParseRelTime(pt_it->get<std::string>());
      if (rt.ok()) {
        obj->penalty_time = rt.value();
      } else {
        diags.push_back(
            Diagnostic{Severity::kWarning, DiagnosticCode::kInvalidReltime,
                       "Invalid penalty_time RELTIME: " +
                           rt.status().message(),
                       line_no});
      }
    } else {
      diags.push_back(
          Diagnostic{Severity::kWarning, DiagnosticCode::kInvalidRequiredField,
                     "penalty_time has unexpected type", line_no});
    }
  }

  obj->banner = DecodeFileRefArray(data, "banner");
  obj->logo = DecodeFileRefArray(data, "logo");
  obj->problemset = DecodeFileRefArray(data, "problemset");
  obj->unknown_fields = CollectUnknownFields(data, kContestKeys);

  return obj;
}

std::unique_ptr<JudgementType> DecodeJudgementType(
    const json& data, std::vector<Diagnostic>& diags, size_t line_no) {
  auto obj = std::make_unique<JudgementType>();
  obj->id = OptString(data, "id").value_or("");
  obj->name = OptString(data, "name");
  obj->penalty = OptBool(data, "penalty");
  obj->solved = OptBool(data, "solved");
  obj->simplified_judgement_type_id =
      OptString(data, "simplified_judgement_type_id");
  obj->unknown_fields = CollectUnknownFields(data, kJudgementTypeKeys);
  return obj;
}

std::unique_ptr<Language> DecodeLanguage(const json& data,
                                          std::vector<Diagnostic>& diags,
                                          size_t line_no) {
  auto obj = std::make_unique<Language>();
  obj->id = OptString(data, "id").value_or("");
  obj->name = OptString(data, "name");
  obj->entry_point_required = OptBool(data, "entry_point_required");
  obj->entry_point_name = OptString(data, "entry_point_name");
  obj->extensions = StringArray(data, "extensions");
  obj->compiler = OptString(data, "compiler");
  obj->runner = OptString(data, "runner");
  obj->unknown_fields = CollectUnknownFields(data, kLanguageKeys);
  return obj;
}

std::unique_ptr<Problem> DecodeProblem(const json& data,
                                        std::vector<Diagnostic>& diags,
                                        size_t line_no) {
  auto obj = std::make_unique<Problem>();
  obj->id = OptString(data, "id").value_or("");
  obj->ordinal = OptInt(data, "ordinal");
  obj->label = OptString(data, "label");
  obj->name = OptString(data, "name");
  obj->rgb = OptString(data, "rgb");
  obj->color = OptString(data, "color");
  obj->time_limit = OptInt(data, "time_limit");
  obj->test_data_count = OptInt(data, "test_data_count");
  obj->memory_limit = OptInt(data, "memory_limit");
  obj->output_limit = OptInt(data, "output_limit");
  obj->code_limit = OptInt(data, "code_limit");
  obj->uuid = OptString(data, "uuid");
  obj->short_name = OptString(data, "short_name");
  obj->external_id = OptString(data, "external_id");
  obj->statement = DecodeFileRefArray(data, "statement");
  obj->attachments = DecodeFileRefArray(data, "attachments");
  obj->unknown_fields = CollectUnknownFields(data, kProblemKeys);
  return obj;
}

std::unique_ptr<Group> DecodeGroup(const json& data,
                                    std::vector<Diagnostic>& diags,
                                    size_t line_no) {
  auto obj = std::make_unique<Group>();
  obj->id = OptString(data, "id").value_or("");
  obj->name = OptString(data, "name");
  obj->icpc_id = OptString(data, "icpc_id");
  obj->type_str = OptString(data, "type");
  obj->hidden = OptBool(data, "hidden");
  obj->sortorder = OptInt(data, "sortorder");
  obj->color = OptString(data, "color");
  obj->logo = DecodeFileRefArray(data, "logo");
  obj->unknown_fields = CollectUnknownFields(data, kGroupKeys);
  return obj;
}

std::unique_ptr<Organization> DecodeOrganization(
    const json& data, std::vector<Diagnostic>& diags, size_t line_no) {
  auto obj = std::make_unique<Organization>();
  obj->id = OptString(data, "id").value_or("");
  obj->name = OptString(data, "name");
  obj->formal_name = OptString(data, "formal_name");
  obj->icpc_id = OptString(data, "icpc_id");
  obj->country = OptString(data, "country");
  obj->country_flag = OptString(data, "country_flag");
  obj->url = OptString(data, "url");
  obj->twitter_hashtag = OptString(data, "twitter_hashtag");
  obj->twitter_account = OptString(data, "twitter_account");
  obj->country_subdivision = OptString(data, "country_subdivision");
  obj->logo = DecodeFileRefArray(data, "logo");
  obj->country_flag_files = DecodeFileRefArray(data, "country_flag");
  obj->country_subdivision_flag =
      DecodeFileRefArray(data, "country_subdivision_flag");
  obj->unknown_fields = CollectUnknownFields(data, kOrganizationKeys);
  return obj;
}

std::unique_ptr<Team> DecodeTeam(const json& data,
                                  std::vector<Diagnostic>& diags,
                                  size_t line_no) {
  auto obj = std::make_unique<Team>();
  obj->id = OptString(data, "id").value_or("");
  obj->name = OptString(data, "name");
  obj->display_name = OptString(data, "display_name");
  obj->icpc_id = OptString(data, "icpc_id");
  obj->organization_id = OptString(data, "organization_id");
  obj->group_ids = StringArray(data, "group_ids");
  obj->hidden = OptBool(data, "hidden");
  obj->photo = DecodeFileRefArray(data, "photo");
  obj->video = DecodeFileRefArray(data, "video");
  obj->backup = DecodeFileRefArray(data, "backup");
  obj->desktop = DecodeFileRefArray(data, "desktop");
  obj->webcam = DecodeFileRefArray(data, "webcam");
  obj->audio = DecodeFileRefArray(data, "audio");
  obj->location_id = OptString(data, "location_id");
  obj->external_id = OptString(data, "external_id");
  obj->unknown_fields = CollectUnknownFields(data, kTeamKeys);
  return obj;
}

std::unique_ptr<Person> DecodePerson(const json& data,
                                      std::vector<Diagnostic>& diags,
                                      size_t line_no) {
  auto obj = std::make_unique<Person>();
  obj->id = OptString(data, "id").value_or("");
  obj->name = OptString(data, "name");
  obj->title = OptString(data, "title");
  obj->email = OptString(data, "email");
  obj->sex = OptString(data, "sex");
  obj->role = OptString(data, "role");
  obj->team_id = OptString(data, "team_id");
  obj->icpc_id = OptString(data, "icpc_id");
  obj->photo = DecodeFileRefArray(data, "photo");
  obj->unknown_fields = CollectUnknownFields(data, kPersonKeys);
  return obj;
}

std::unique_ptr<Account> DecodeAccount(const json& data,
                                        std::vector<Diagnostic>& diags,
                                        size_t line_no) {
  auto obj = std::make_unique<Account>();
  obj->id = OptString(data, "id").value_or("");
  obj->username = OptString(data, "username");
  obj->password = OptString(data, "password");
  obj->account_type = OptString(data, "type");
  obj->ip = OptString(data, "ip");
  obj->team_id = OptString(data, "team_id");
  obj->person_id = OptString(data, "person_id");
  obj->unknown_fields = CollectUnknownFields(data, kAccountKeys);
  return obj;
}

std::unique_ptr<State> DecodeState(const json& data,
                                    std::vector<Diagnostic>& diags,
                                    size_t line_no) {
  auto obj = std::make_unique<State>();
  obj->started = OptAbsTime(data, "started", diags, line_no);
  obj->ended = OptAbsTime(data, "ended", diags, line_no);
  obj->frozen = OptAbsTime(data, "frozen", diags, line_no);
  obj->thawed = OptAbsTime(data, "thawed", diags, line_no);
  obj->finalized = OptAbsTime(data, "finalized", diags, line_no);
  obj->end_of_updates = OptAbsTime(data, "end_of_updates", diags, line_no);
  obj->unknown_fields = CollectUnknownFields(data, kStateKeys);
  return obj;
}

std::unique_ptr<Submission> DecodeSubmission(const json& data,
                                              std::vector<Diagnostic>& diags,
                                              size_t line_no) {
  auto obj = std::make_unique<Submission>();
  obj->id = OptString(data, "id").value_or("");
  obj->language_id = OptString(data, "language_id");
  obj->problem_id = OptString(data, "problem_id");
  obj->team_id = OptString(data, "team_id");
  obj->account_id = OptString(data, "account_id");
  obj->time = OptAbsTime(data, "time", diags, line_no);
  obj->contest_time = OptRelTime(data, "contest_time", diags, line_no);
  obj->entry_point = OptString(data, "entry_point");
  obj->external_id = OptString(data, "external_id");
  obj->files = DecodeFileRefArray(data, "files");
  obj->reaction = DecodeFileRefArray(data, "reaction");
  obj->unknown_fields = CollectUnknownFields(data, kSubmissionKeys);
  return obj;
}

std::unique_ptr<Judgement> DecodeJudgement(const json& data,
                                            std::vector<Diagnostic>& diags,
                                            size_t line_no) {
  auto obj = std::make_unique<Judgement>();
  obj->id = OptString(data, "id").value_or("");
  obj->submission_id = OptString(data, "submission_id");
  obj->judgement_type_id = OptString(data, "judgement_type_id");
  obj->simplified_judgement_type_id =
      OptString(data, "simplified_judgement_type_id");
  obj->start_time = OptAbsTime(data, "start_time", diags, line_no);
  obj->start_contest_time =
      OptRelTime(data, "start_contest_time", diags, line_no);
  obj->end_time = OptAbsTime(data, "end_time", diags, line_no);
  obj->end_contest_time =
      OptRelTime(data, "end_contest_time", diags, line_no);
  obj->max_run_time = OptDouble(data, "max_run_time");
  obj->valid = OptBool(data, "valid");
  obj->current = OptBool(data, "current");
  obj->unknown_fields = CollectUnknownFields(data, kJudgementKeys);
  return obj;
}

std::unique_ptr<Run> DecodeRun(const json& data,
                                std::vector<Diagnostic>& diags,
                                size_t line_no) {
  auto obj = std::make_unique<Run>();
  obj->id = OptString(data, "id").value_or("");
  obj->judgement_id = OptString(data, "judgement_id");
  obj->ordinal = OptInt(data, "ordinal");
  obj->judgement_type_id = OptString(data, "judgement_type_id");
  obj->time = OptAbsTime(data, "time", diags, line_no);
  obj->contest_time = OptRelTime(data, "contest_time", diags, line_no);
  auto rt = OptDouble(data, "run_time");
  if (rt.has_value()) obj->run_time = rt;
  obj->unknown_fields = CollectUnknownFields(data, kRunKeys);
  return obj;
}

std::unique_ptr<Clarification> DecodeClarification(
    const json& data, ApiVersion version, std::vector<Diagnostic>& diags,
    size_t line_no) {
  auto obj = std::make_unique<Clarification>();
  obj->id = OptString(data, "id").value_or("");
  obj->from_team_id = OptString(data, "from_team_id");
  obj->problem_id = OptString(data, "problem_id");
  obj->reply_to_id = OptString(data, "reply_to_id");
  obj->time = OptAbsTime(data, "time", diags, line_no);
  obj->contest_time = OptRelTime(data, "contest_time", diags, line_no);
  obj->text = OptString(data, "text");

  if (version == ApiVersion::k2023_06) {
    // 2023-06: to_team_id (singular).
    auto to_team_id = OptString(data, "to_team_id");
    if (to_team_id.has_value()) {
      obj->to_team_ids.push_back(*to_team_id);
    }
    // Also check for to_team_ids in case of mixed data.
    auto arr = StringArray(data, "to_team_ids");
    for (auto& s : arr) {
      obj->to_team_ids.push_back(std::move(s));
    }
    obj->unknown_fields =
        CollectUnknownFields(data, kClarificationKeys2023);
  } else {
    // 2026-01: to_team_ids, to_group_ids (arrays).
    obj->to_team_ids = StringArray(data, "to_team_ids");
    obj->to_group_ids = StringArray(data, "to_group_ids");
    // Also check to_team_id for backward compat.
    auto to_team_id = OptString(data, "to_team_id");
    if (to_team_id.has_value() && obj->to_team_ids.empty()) {
      obj->to_team_ids.push_back(*to_team_id);
    }
    obj->unknown_fields =
        CollectUnknownFields(data, kClarificationKeys2026);
  }
  return obj;
}

std::unique_ptr<Award> DecodeAward(const json& data,
                                    std::vector<Diagnostic>& diags,
                                    size_t line_no) {
  auto obj = std::make_unique<Award>();
  obj->id = OptString(data, "id").value_or("");
  obj->citation = OptString(data, "citation");
  obj->team_ids = StringArray(data, "team_ids");

  // Parse parameters as unknown fields.
  auto params_it = data.find("parameters");
  if (params_it != data.end() && params_it->is_object()) {
    for (const auto& [key, val] : params_it->items()) {
      obj->parameters[key] = val.dump();
    }
  }
  obj->unknown_fields = CollectUnknownFields(data, kAwardKeys);
  return obj;
}

std::unique_ptr<Commentary> DecodeCommentary(const json& data,
                                              std::vector<Diagnostic>& diags,
                                              size_t line_no) {
  auto obj = std::make_unique<Commentary>();
  obj->id = OptString(data, "id").value_or("");
  obj->time = OptAbsTime(data, "time", diags, line_no);
  obj->contest_time = OptRelTime(data, "contest_time", diags, line_no);
  obj->message = OptString(data, "message");
  obj->team_ids = StringArray(data, "team_ids");
  obj->problem_ids = StringArray(data, "problem_ids");
  obj->submission_ids = StringArray(data, "submission_ids");
  obj->unknown_fields = CollectUnknownFields(data, kCommentaryKeys);
  return obj;
}

// Dispatch object decoding by type.
StatusOr<DecodeResult> DecodeTypedObject(
    ObjectType obj_type, const json& data, ApiVersion version,
    size_t line_no, const ParseOptions& options) {
  DecodeResult result;

  switch (obj_type) {
    case ObjectType::kContest:
      result.object = DecodeContest(data, version, result.diagnostics, line_no);
      break;
    case ObjectType::kJudgementTypes:
      result.object =
          DecodeJudgementType(data, result.diagnostics, line_no);
      break;
    case ObjectType::kLanguages:
      result.object = DecodeLanguage(data, result.diagnostics, line_no);
      break;
    case ObjectType::kProblems:
      result.object = DecodeProblem(data, result.diagnostics, line_no);
      break;
    case ObjectType::kGroups:
      result.object = DecodeGroup(data, result.diagnostics, line_no);
      break;
    case ObjectType::kOrganizations:
      result.object =
          DecodeOrganization(data, result.diagnostics, line_no);
      break;
    case ObjectType::kTeams:
      result.object = DecodeTeam(data, result.diagnostics, line_no);
      break;
    case ObjectType::kPersons:
      result.object = DecodePerson(data, result.diagnostics, line_no);
      break;
    case ObjectType::kAccounts:
      result.object = DecodeAccount(data, result.diagnostics, line_no);
      break;
    case ObjectType::kState:
      result.object = DecodeState(data, result.diagnostics, line_no);
      break;
    case ObjectType::kSubmissions:
      result.object = DecodeSubmission(data, result.diagnostics, line_no);
      break;
    case ObjectType::kJudgements:
      result.object = DecodeJudgement(data, result.diagnostics, line_no);
      break;
    case ObjectType::kRuns:
      result.object = DecodeRun(data, result.diagnostics, line_no);
      break;
    case ObjectType::kClarifications:
      result.object =
          DecodeClarification(data, version, result.diagnostics, line_no);
      break;
    case ObjectType::kAwards:
      result.object = DecodeAward(data, result.diagnostics, line_no);
      break;
    case ObjectType::kCommentary:
      result.object = DecodeCommentary(data, result.diagnostics, line_no);
      break;
    case ObjectType::kUnknown:
      return Status(StatusCode::kParseError,
                    "Cannot decode unknown object type");
  }

  return result;
}

// Concrete profile implementation (shared logic, parameterized by version).
class ProfileImpl : public VersionProfile {
 public:
  explicit ProfileImpl(ApiVersion ver) : version_(ver) {}

  ApiVersion version() const override { return version_; }

  StatusOr<DecodeResult> DecodeObject(ObjectType type,
                                       const json& data,
                                       size_t line_no,
                                       const ParseOptions& options)
      const override {
    if (!data.is_object()) {
      return Status(StatusCode::kParseError,
                    "Data is not a JSON object at line " +
                        std::to_string(line_no));
    }

    return DecodeTypedObject(type, data, version_, line_no, options);
  }

  StatusOr<CollectionDecodeResult> DecodeCollection(
      ObjectType type, const json& data, size_t line_no,
      const ParseOptions& options) const override {
    if (!data.is_array()) {
      return Status(StatusCode::kParseError,
                    "Collection data is not a JSON array at line " +
                        std::to_string(line_no));
    }

    CollectionDecodeResult result;
    result.objects.reserve(data.size());
    for (size_t i = 0; i < data.size(); ++i) {
      const auto& elem = data[i];
      if (!elem.is_object()) {
        return Status(
            StatusCode::kParseError,
            "Collection element " + std::to_string(i) +
                " is not an object at line " + std::to_string(line_no));
      }

      // Each collection element must have an id.
      auto id_it = elem.find("id");
      if (id_it == elem.end() || !id_it->is_string()) {
        return Status(
            StatusCode::kParseError,
            "Collection element " + std::to_string(i) +
                " missing 'id' at line " + std::to_string(line_no));
      }

      auto decode_result =
          DecodeTypedObject(type, elem, version_, line_no, options);
      if (!decode_result.ok()) {
        return Status(
            StatusCode::kParseError,
            "Collection element " + std::to_string(i) + " decode failed: " +
                decode_result.status().message());
      }

      for (auto& d : decode_result.value().diagnostics) {
        result.diagnostics.push_back(std::move(d));
      }
      result.objects.push_back(std::move(decode_result.value().object));
    }

    return result;
  }

 private:
  ApiVersion version_;
};

}  // namespace

std::unique_ptr<VersionProfile> CreateProfile(ApiVersion version) {
  if (version == ApiVersion::kAuto) {
    // Default to 2023-06 for auto; version detection happens at a higher level.
    return std::make_unique<ProfileImpl>(ApiVersion::k2023_06);
  }
  return std::make_unique<ProfileImpl>(version);
}

}  // namespace internal
}  // namespace ccsparser
