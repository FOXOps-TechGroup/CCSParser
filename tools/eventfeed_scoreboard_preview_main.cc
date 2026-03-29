// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

// eventfeed_scoreboard_preview — standalone tool that parses an NDJSON
// event-feed (from file or HTTP), or fetches CCS REST API endpoints,
// rebuilds the final ICPC-style scoreboard, and writes a self-contained
// HTML file for human review.
//
// Usage:
//   # From local NDJSON file:
//   bazel run //:eventfeed_scoreboard_preview -- \
//       --eventfeed=/path/to/event-feed.ndjson \
//       --output=/path/to/scoreboard.html
//
//   # From CCS REST API:
//   bazel run //:eventfeed_scoreboard_preview -- \
//       --url=https://www.domjudge.org/demoweb/api/v4 \
//       --contest=nwerc18 \
//       --auth=admin:admin \
//       --output=scoreboard.html
//
//   # From local XML file:
//   bazel run //:eventfeed_scoreboard_preview -- \
//       --eventfeed=/path/to/event-feed.xml \
//       --format=xml \
//       --output=scoreboard.html
//
// The tool delegates entirely to the CCSParser library.

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "ccsparser/ccsparser.h"
#include "ccsparser/http_client.h"
#include "ccsparser/scoreboard.h"
#include "ccsparser/scoreboard_builder.h"
#include "html_scoreboard_renderer.h"

namespace {

// Input format.
enum class InputFormat {
  kAuto,      // Auto-detect from extension or content.
  kNdjson,    // NDJSON event-feed.
  kXml,       // XML event-feed.
  kRestApi,   // CCS REST API endpoints (requires --url).
};

struct CliArgs {
  std::string eventfeed;   // Local file path (NDJSON or XML).
  std::string url;         // CCS REST API base URL.
  std::string contest_id;  // Contest ID for REST API.
  std::string auth;        // "user:pass" for HTTP Basic Auth.
  std::string output = "scoreboard_preview.html";
  std::string title;
  ccsparser::ApiVersion version = ccsparser::ApiVersion::kAuto;
  InputFormat format = InputFormat::kAuto;
};

void PrintUsage(const char* argv0) {
  std::cerr
      << "Usage: " << argv0 << " [options]\n"
      << "\n"
      << "Input (one of these is required):\n"
      << "  --eventfeed=<path>   Path to event-feed file (NDJSON or XML)\n"
      << "  --url=<url>          CCS REST API base URL\n"
      << "\n"
      << "Optional:\n"
      << "  --contest=<id>       Contest ID for REST API (default: auto-detect)\n"
      << "  --auth=<user:pass>   HTTP Basic Auth credentials\n"
      << "  --format=<fmt>       Input format: auto, ndjson, xml, rest (default: auto)\n"
      << "  --output=<path>      Output HTML file (default: scoreboard_preview.html)\n"
      << "  --title=<string>     Custom page title\n"
      << "  --version=<ver>      CCS version: auto, 2023-06, 2026-01 (default: auto)\n"
      << "  --help               Show this help message\n";
}

bool ParseArgs(int argc, char* argv[], CliArgs& args) {
  for (int i = 1; i < argc; ++i) {
    std::string arg(argv[i]);
    if (arg.rfind("--eventfeed=", 0) == 0) {
      args.eventfeed = arg.substr(12);
    } else if (arg.rfind("--url=", 0) == 0) {
      args.url = arg.substr(6);
    } else if (arg.rfind("--contest=", 0) == 0) {
      args.contest_id = arg.substr(10);
    } else if (arg.rfind("--auth=", 0) == 0) {
      args.auth = arg.substr(7);
    } else if (arg.rfind("--format=", 0) == 0) {
      std::string f = arg.substr(9);
      if (f == "auto") {
        args.format = InputFormat::kAuto;
      } else if (f == "ndjson") {
        args.format = InputFormat::kNdjson;
      } else if (f == "xml") {
        args.format = InputFormat::kXml;
      } else if (f == "rest") {
        args.format = InputFormat::kRestApi;
      } else {
        std::cerr << "Error: unknown format '" << f << "'\n";
        return false;
      }
    } else if (arg.rfind("--output=", 0) == 0) {
      args.output = arg.substr(9);
    } else if (arg.rfind("--title=", 0) == 0) {
      args.title = arg.substr(8);
    } else if (arg.rfind("--version=", 0) == 0) {
      std::string v = arg.substr(10);
      if (v == "auto") {
        args.version = ccsparser::ApiVersion::kAuto;
      } else if (v == "2023-06") {
        args.version = ccsparser::ApiVersion::k2023_06;
      } else if (v == "2026-01") {
        args.version = ccsparser::ApiVersion::k2026_01;
      } else {
        std::cerr << "Error: unknown version '" << v << "'\n";
        return false;
      }
    } else if (arg == "--help" || arg == "-h") {
      return false;
    } else {
      std::cerr << "Error: unknown argument '" << arg << "'\n";
      return false;
    }
  }
  if (args.eventfeed.empty() && args.url.empty()) {
    std::cerr << "Error: --eventfeed or --url is required\n";
    return false;
  }
  return true;
}

// Returns an ISO 8601 local-time string.
std::string NowIso8601() {
  auto now = std::chrono::system_clock::now();
  auto tt = std::chrono::system_clock::to_time_t(now);
  struct tm tm_buf;
  localtime_r(&tt, &tm_buf);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm_buf);
  return std::string(buf);
}

}  // namespace

int main(int argc, char* argv[]) {
  CliArgs args;
  if (!ParseArgs(argc, argv, args)) {
    PrintUsage(argv[0]);
    return 1;
  }

  using Clock = std::chrono::steady_clock;
  auto t_total_start = Clock::now();

  // ---- Parse input via CCSParser lib ----
  ccsparser::ParseOptions opts;
  opts.version = args.version;
  opts.error_policy = ccsparser::ErrorPolicy::kContinue;
  opts.keep_event_log = false;
  opts.keep_raw_json = false;

  // Determine effective input mode.
  InputFormat format = args.format;

  if (!args.url.empty() && format == InputFormat::kAuto) {
    format = InputFormat::kRestApi;
  }

  if (format == InputFormat::kAuto && !args.eventfeed.empty()) {
    // Auto-detect from file extension.
    std::string ext = args.eventfeed;
    auto dot = ext.rfind('.');
    if (dot != std::string::npos) {
      ext = ext.substr(dot);
    }
    if (ext == ".xml") {
      format = InputFormat::kXml;
    } else {
      format = InputFormat::kNdjson;
    }
  }

  std::string source_desc;
  ccsparser::StatusOr<ccsparser::ParseResult> result =
      ccsparser::Status(ccsparser::StatusCode::kInternal, "No input");

  auto t_parse_start = Clock::now();

  switch (format) {
    case InputFormat::kNdjson:
    case InputFormat::kAuto: {
      source_desc = args.eventfeed;
      std::cerr << "Parsing " << source_desc << " (NDJSON) ...\n";
      result = ccsparser::EventFeedParser::ParseFile(args.eventfeed, opts);
      break;
    }
    case InputFormat::kXml: {
      source_desc = args.eventfeed;
      std::cerr << "Parsing " << source_desc << " (XML) ...\n";
      result = ccsparser::XmlFeedParser::ParseFile(args.eventfeed, opts);
      break;
    }
    case InputFormat::kRestApi: {
      ccsparser::RestApiOptions rest_opts;
      rest_opts.base_url = args.url;
      rest_opts.contest_id = args.contest_id;
      rest_opts.timeout_seconds = 60;
      if (!args.auth.empty()) {
        auto colon = args.auth.find(':');
        if (colon != std::string::npos) {
          rest_opts.auth = ccsparser::HttpBasicAuth{
              args.auth.substr(0, colon), args.auth.substr(colon + 1)};
        }
      }
      source_desc = args.url + "/contests/" + args.contest_id;
      std::cerr << "Fetching from REST API: " << source_desc << " ...\n";
      result = ccsparser::RestApiParser::FetchAndParse(rest_opts, opts);
      break;
    }
  }

  auto t_parse_end = Clock::now();

  if (!result.ok()) {
    std::cerr << "Fatal parse error: " << result.status().ToString() << "\n";
    return 2;
  }

  auto& pr = result.value();
  double parse_sec = std::chrono::duration<double>(t_parse_end - t_parse_start).count();
  std::cerr << "Parsed " << pr.cursor.event_count << " events, "
            << pr.diagnostics.size() << " diagnostics.\n";

  // ---- Build scoreboard via lib ----
  auto t_sb_start = Clock::now();
  auto sb_or = ccsparser::BuildScoreboard(pr.store);
  auto t_sb_end = Clock::now();

  if (!sb_or.ok()) {
    std::cerr << "Scoreboard build failed: " << sb_or.status().ToString()
              << "\n";
    return 3;
  }

  auto& scoreboard = sb_or.value();
  double sb_sec = std::chrono::duration<double>(t_sb_end - t_sb_start).count();
  std::cerr << "Scoreboard: " << scoreboard.rows.size() << " teams, "
            << scoreboard.problems.size() << " problems.\n";

  // ---- Prepare render context ----
  ccsparser::tools::RenderContext ctx;
  ctx.page_title = args.title;
  ctx.eventfeed_path = source_desc;
  ctx.resolved_version = pr.resolved_version;
  ctx.generation_time = NowIso8601();
  ctx.cursor = pr.cursor;
  ctx.total_diagnostics = pr.diagnostics.size();
  constexpr size_t kMaxDiagSamples = 30;
  size_t diag_sample_count = 0;
  for (const auto& d : pr.diagnostics) {
    if (d.severity == ccsparser::Severity::kWarning) ctx.warning_count++;
    if (d.severity == ccsparser::Severity::kError) ctx.error_count++;
    if (diag_sample_count < kMaxDiagSamples) {
      ctx.diagnostic_samples.push_back(d.ToString());
      ++diag_sample_count;
    }
  }

  // ---- Render HTML ----
  auto t_render_start = Clock::now();
  std::ostringstream html_buf;
  // Pre-fill timing with placeholder; we'll finalize after render.
  ctx.parse_seconds = parse_sec;
  ctx.scoreboard_rebuild_seconds = sb_sec;
  ccsparser::tools::RenderScoreboardHtml(html_buf, scoreboard, ctx);
  auto t_render_end = Clock::now();
  double render_sec = std::chrono::duration<double>(t_render_end - t_render_start).count();

  // ---- Write file ----
  auto t_write_start = Clock::now();
  std::ofstream out(args.output);
  if (!out.is_open()) {
    std::cerr << "Cannot open output file: " << args.output << "\n";
    return 4;
  }
  out << html_buf.str();
  out.close();
  auto t_write_end = Clock::now();
  double write_sec = std::chrono::duration<double>(t_write_end - t_write_start).count();

  auto t_total_end = Clock::now();
  double total_sec = std::chrono::duration<double>(t_total_end - t_total_start).count();

  // ---- Print timing summary ----
  std::cerr << std::fixed << std::setprecision(3)
            << "  Parse:        " << parse_sec << "s\n"
            << "  Scoreboard:   " << sb_sec << "s\n"
            << "  HTML render:  " << render_sec << "s\n"
            << "  Write file:   " << write_sec << "s\n"
            << "  Total:        " << total_sec << "s\n";
  std::cerr << "HTML written to " << args.output << "\n";
  return 0;
}
