// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

// HTML scoreboard renderer — generates a self-contained static HTML page
// from a ScoreboardSnapshot.  Used by the eventfeed_scoreboard_preview tool
// for human review.

#ifndef CCSPARSER_TOOLS_HTML_SCOREBOARD_RENDERER_H_
#define CCSPARSER_TOOLS_HTML_SCOREBOARD_RENDERER_H_

#include <ostream>
#include <string>
#include <vector>

#include "ccsparser/diagnostic.h"
#include "ccsparser/event.h"
#include "ccsparser/scoreboard.h"
#include "ccsparser/types.h"

namespace ccsparser {
namespace tools {

// Metadata displayed in the HTML header/footer.
struct RenderContext {
  std::string page_title;         // Custom title override.
  std::string eventfeed_path;     // Source file path for display.
  ApiVersion resolved_version = ApiVersion::kAuto;
  std::string generation_time;    // ISO timestamp string.

  // Parser diagnostics summary.
  EventCursor cursor;
  size_t total_diagnostics = 0;
  size_t warning_count = 0;
  size_t error_count = 0;
  std::vector<std::string> diagnostic_samples;  // First N messages.

  // Phase timing (seconds).
  double parse_seconds = 0.0;
  double scoreboard_rebuild_seconds = 0.0;
  double html_render_seconds = 0.0;
  double write_file_seconds = 0.0;
  double total_seconds = 0.0;
};

// Renders a complete self-contained HTML page to `out`.
void RenderScoreboardHtml(std::ostream& out,
                          const ScoreboardSnapshot& snapshot,
                          const RenderContext& ctx);

}  // namespace tools
}  // namespace ccsparser

#endif  // CCSPARSER_TOOLS_HTML_SCOREBOARD_RENDERER_H_
