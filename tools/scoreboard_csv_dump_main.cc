// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

// scoreboard_csv_dump — reads an event-feed file (NDJSON or XML), builds
// the scoreboard, and prints it as a CSV suitable for diffing against
// reference standings files.
//
// Output columns:
//   place,team_id,team_name,solved,penalty,<per-problem columns>
//
// Each per-problem column contains one of:
//   ""         — no submission
//   "-N"       — N attempts, all wrong
//   "+N(mm)"   — solved in N attempts at minute mm (N=1 means "+1(mm)")
//
// Usage:
//   bazel run //:scoreboard_csv_dump -- --eventfeed=case/event-feed-4.json
//   bazel run //:scoreboard_csv_dump -- --eventfeed=case/event-feed-4.json \
//       --format=csv > /tmp/out.csv

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "ccsparser/ccsparser.h"
#include "ccsparser/scoreboard.h"
#include "ccsparser/scoreboard_builder.h"

namespace {

void PrintUsage(const char* argv0) {
  std::cerr << "Usage: " << argv0
            << " --eventfeed=<path> [--output=<csv-path>]\n";
}

// Escape a CSV field (wraps in quotes if needed).
std::string CsvField(const std::string& s) {
  bool needs_quotes = s.find(',') != std::string::npos ||
                      s.find('"') != std::string::npos ||
                      s.find('\n') != std::string::npos;
  if (!needs_quotes) return s;
  std::string out = "\"";
  for (char c : s) {
    if (c == '"') out += "\"\"";
    else out += c;
  }
  out += "\"";
  return out;
}

}  // namespace

int main(int argc, char* argv[]) {
  std::string eventfeed_path;
  std::string output_path;

  for (int i = 1; i < argc; ++i) {
    std::string arg(argv[i]);
    if (arg.rfind("--eventfeed=", 0) == 0) {
      eventfeed_path = arg.substr(12);
    } else if (arg.rfind("--output=", 0) == 0) {
      output_path = arg.substr(9);
    } else {
      std::cerr << "Unknown argument: " << arg << "\n";
      PrintUsage(argv[0]);
      return 1;
    }
  }

  if (eventfeed_path.empty()) {
    PrintUsage(argv[0]);
    return 1;
  }

  // Detect format from extension.
  bool is_xml = false;
  {
    std::string ext = eventfeed_path;
    auto dot = ext.rfind('.');
    if (dot != std::string::npos) {
      ext = ext.substr(dot);
      if (ext == ".xml") is_xml = true;
    }
  }

  ccsparser::ParseOptions opts;
  opts.version = ccsparser::ApiVersion::kAuto;
  opts.error_policy = ccsparser::ErrorPolicy::kContinue;
  opts.keep_event_log = false;
  opts.keep_raw_json = false;

  ccsparser::StatusOr<ccsparser::ParseResult> result =
      is_xml ? ccsparser::XmlFeedParser::ParseFile(eventfeed_path, opts)
             : ccsparser::EventFeedParser::ParseFile(eventfeed_path, opts);

  if (!result.ok()) {
    std::cerr << "Parse error: " << result.status().ToString() << "\n";
    return 2;
  }

  auto& pr = result.value();

  if (!pr.diagnostics.empty()) {
    std::cerr << pr.diagnostics.size() << " diagnostic(s):\n";
    size_t shown = 0;
    for (const auto& d : pr.diagnostics) {
      if (shown++ >= 5) { std::cerr << "  ...\n"; break; }
      std::cerr << "  " << d.ToString() << "\n";
    }
  }

  auto sb_or = ccsparser::BuildScoreboard(pr.store);
  if (!sb_or.ok()) {
    std::cerr << "Scoreboard error: " << sb_or.status().ToString() << "\n";
    return 3;
  }

  auto& sb = sb_or.value();

  // Write CSV.
  std::ostream* out_ptr = &std::cout;
  std::ofstream file_out;
  if (!output_path.empty()) {
    file_out.open(output_path);
    if (!file_out.is_open()) {
      std::cerr << "Cannot open output: " << output_path << "\n";
      return 4;
    }
    out_ptr = &file_out;
  }

  std::ostream& out = *out_ptr;

  // Header.
  out << "place,team_id,team_name,solved,penalty";
  for (const auto& p : sb.problems) {
    out << "," << CsvField(p.label.empty() ? p.problem_id : p.label);
  }
  out << "\n";

  // Rows.
  for (const auto& row : sb.rows) {
    out << row.place << ","
        << CsvField(row.team_id) << ","
        << CsvField(row.team_name) << ","
        << row.solved << ","
        << row.penalty;

    for (const auto& cell : row.cells) {
      out << ",";
      if (cell.status == ccsparser::ProblemStatus::kSolved) {
        out << "+" << cell.attempts << "(" << cell.time_minutes << ")";
      } else if (cell.status == ccsparser::ProblemStatus::kFailed) {
        out << "-" << cell.attempts;
      }
      // kUnsolved → empty
    }
    out << "\n";
  }

  std::cerr << "Scoreboard: " << sb.rows.size() << " teams, "
            << sb.problems.size() << " problems.\n";

  return 0;
}
