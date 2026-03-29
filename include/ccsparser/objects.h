// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

#ifndef CCSPARSER_OBJECTS_H_
#define CCSPARSER_OBJECTS_H_

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "ccsparser/types.h"

namespace ccsparser {

// Unknown fields preserved as JSON string values keyed by field name.
using UnknownFields = std::map<std::string, std::string>;

// Base for all contest objects.
struct ContestObject {
  ObjectType type = ObjectType::kUnknown;
  std::string id;
  UnknownFields unknown_fields;

  virtual ~ContestObject() = default;
};

// ---- Typed contest objects ----

struct Contest : public ContestObject {
  Contest() { type = ObjectType::kContest; }

  std::optional<std::string> name;
  std::optional<std::string> formal_name;
  std::optional<AbsoluteTime> start_time;
  std::optional<AbsoluteTime> end_time;
  std::optional<RelativeTime> duration;
  std::optional<RelativeTime> scoreboard_freeze_duration;
  std::optional<AbsoluteTime> scoreboard_thaw_time;
  std::optional<std::string> scoreboard_type;
  std::optional<RelativeTime> penalty_time;  // Normalized from int or RELTIME.
  std::optional<std::string> main_scoreboard_group_id;  // 2026-01 only.
  std::vector<FileRef> banner;
  std::vector<FileRef> logo;
  std::vector<FileRef> problemset;
  std::optional<std::string> shortname;
  std::optional<std::string> external_id;
  std::optional<bool> allow_submit;
  std::optional<bool> runtime_as_score_tiebreaker;
  std::optional<std::string> warning_message;
};

struct JudgementType : public ContestObject {
  JudgementType() { type = ObjectType::kJudgementTypes; }

  std::optional<std::string> name;
  std::optional<bool> penalty;
  std::optional<bool> solved;
  std::optional<std::string> simplified_judgement_type_id;  // 2026-01.
};

struct Language : public ContestObject {
  Language() { type = ObjectType::kLanguages; }

  std::optional<std::string> name;
  std::optional<bool> entry_point_required;
  std::optional<std::string> entry_point_name;
  std::vector<std::string> extensions;
  std::optional<std::string> compiler;
  std::optional<std::string> runner;
};

struct Problem : public ContestObject {
  Problem() { type = ObjectType::kProblems; }

  std::optional<int> ordinal;
  std::optional<std::string> label;
  std::optional<std::string> name;
  std::optional<std::string> rgb;
  std::optional<std::string> color;
  std::optional<int> time_limit;
  std::optional<int> test_data_count;
  std::optional<int> memory_limit;     // 2026-01.
  std::optional<int> output_limit;     // 2026-01.
  std::optional<int> code_limit;       // 2026-01.
  std::optional<std::string> uuid;
  std::optional<std::string> short_name;
  std::optional<std::string> external_id;
  std::vector<FileRef> statement;
  std::vector<FileRef> attachments;    // 2026-01.
};

struct Group : public ContestObject {
  Group() { type = ObjectType::kGroups; }

  std::optional<std::string> name;
  std::optional<std::string> icpc_id;
  std::optional<std::string> type_str;  // "type" in JSON.
  std::optional<bool> hidden;
  std::optional<int> sortorder;
  std::optional<std::string> color;
  std::vector<FileRef> logo;
};

struct Organization : public ContestObject {
  Organization() { type = ObjectType::kOrganizations; }

  std::optional<std::string> name;
  std::optional<std::string> formal_name;
  std::optional<std::string> icpc_id;
  std::optional<std::string> country;
  std::optional<std::string> country_flag;  // Backward compat.
  std::optional<std::string> url;
  std::optional<std::string> twitter_hashtag;
  std::optional<std::string> twitter_account;
  std::optional<std::string> country_subdivision;    // 2026-01.
  std::vector<FileRef> logo;
  std::vector<FileRef> country_flag_files;
  std::vector<FileRef> country_subdivision_flag;     // 2026-01.
};

struct Team : public ContestObject {
  Team() { type = ObjectType::kTeams; }

  std::optional<std::string> name;
  std::optional<std::string> display_name;
  std::optional<std::string> icpc_id;
  std::optional<std::string> organization_id;
  std::optional<std::string> group_ids_str;  // Backward compat.
  std::vector<std::string> group_ids;
  std::optional<bool> hidden;  // 2023-06 only.
  std::vector<FileRef> photo;
  std::vector<FileRef> video;
  std::vector<FileRef> backup;
  std::vector<FileRef> desktop;
  std::vector<FileRef> webcam;
  std::vector<FileRef> audio;
  std::optional<std::string> location_id;
  std::optional<std::string> external_id;
};

struct Person : public ContestObject {
  Person() { type = ObjectType::kPersons; }

  std::optional<std::string> name;
  std::optional<std::string> title;
  std::optional<std::string> email;
  std::optional<std::string> sex;
  std::optional<std::string> role;
  std::optional<std::string> team_id;
  std::optional<std::string> icpc_id;
  std::vector<FileRef> photo;
};

struct Account : public ContestObject {
  Account() { type = ObjectType::kAccounts; }

  std::optional<std::string> username;
  std::optional<std::string> password;
  std::optional<std::string> account_type;  // "type" in JSON.
  std::optional<std::string> ip;
  std::optional<std::string> team_id;
  std::optional<std::string> person_id;
};

struct State : public ContestObject {
  State() { type = ObjectType::kState; }

  std::optional<AbsoluteTime> started;
  std::optional<AbsoluteTime> ended;
  std::optional<AbsoluteTime> frozen;
  std::optional<AbsoluteTime> thawed;
  std::optional<AbsoluteTime> finalized;
  std::optional<AbsoluteTime> end_of_updates;
};

struct Submission : public ContestObject {
  Submission() { type = ObjectType::kSubmissions; }

  std::optional<std::string> language_id;
  std::optional<std::string> problem_id;
  std::optional<std::string> team_id;  // Optional in 2026-01.
  std::optional<std::string> account_id;  // 2026-01.
  std::optional<AbsoluteTime> time;
  std::optional<RelativeTime> contest_time;
  std::optional<std::string> entry_point;
  std::optional<std::string> external_id;
  std::vector<FileRef> files;
  std::vector<FileRef> reaction;
};

struct Judgement : public ContestObject {
  Judgement() { type = ObjectType::kJudgements; }

  std::optional<std::string> submission_id;
  std::optional<std::string> judgement_type_id;
  std::optional<std::string> simplified_judgement_type_id;  // 2026-01.
  std::optional<AbsoluteTime> start_time;
  std::optional<RelativeTime> start_contest_time;
  std::optional<AbsoluteTime> end_time;
  std::optional<RelativeTime> end_contest_time;
  std::optional<double> max_run_time;
  std::optional<bool> valid;
  std::optional<bool> current;  // 2026-01.
};

struct Run : public ContestObject {
  Run() { type = ObjectType::kRuns; }

  std::optional<std::string> judgement_id;
  std::optional<int> ordinal;
  std::optional<std::string> judgement_type_id;
  std::optional<AbsoluteTime> time;
  std::optional<RelativeTime> contest_time;
  std::optional<double> run_time;
};

struct Clarification : public ContestObject {
  Clarification() { type = ObjectType::kClarifications; }

  std::optional<std::string> from_team_id;
  // Normalized: 2023-06 to_team_id -> to_team_ids[0].
  std::vector<std::string> to_team_ids;
  std::vector<std::string> to_group_ids;  // 2026-01.
  std::optional<std::string> problem_id;
  std::optional<std::string> reply_to_id;
  std::optional<AbsoluteTime> time;
  std::optional<RelativeTime> contest_time;
  std::optional<std::string> text;
};

struct Award : public ContestObject {
  Award() { type = ObjectType::kAwards; }

  std::optional<std::string> citation;
  std::vector<std::string> team_ids;
  // Keep these as raw strings, no business enum.
  UnknownFields parameters;
};

struct Commentary : public ContestObject {
  Commentary() { type = ObjectType::kCommentary; }

  std::optional<AbsoluteTime> time;
  std::optional<RelativeTime> contest_time;
  std::optional<std::string> message;
  std::vector<std::string> team_ids;
  std::vector<std::string> problem_ids;
  std::vector<std::string> submission_ids;
};

}  // namespace ccsparser

#endif  // CCSPARSER_OBJECTS_H_
