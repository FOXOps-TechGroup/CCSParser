// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

#include "html_scoreboard_renderer.h"

#include <cstddef>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <string>

#include "ccsparser/scoreboard.h"
#include "ccsparser/types.h"

namespace ccsparser {
namespace tools {

namespace {

// Escape HTML special characters.
std::string HtmlEscape(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    switch (c) {
      case '&':
        out += "&amp;";
        break;
      case '<':
        out += "&lt;";
        break;
      case '>':
        out += "&gt;";
        break;
      case '"':
        out += "&quot;";
        break;
      case '\'':
        out += "&#39;";
        break;
      default:
        out += c;
    }
  }
  return out;
}

std::string VersionString(ApiVersion v) {
  switch (v) {
    case ApiVersion::kAuto:
      return "auto";
    case ApiVersion::k2023_06:
      return "2023-06";
    case ApiVersion::k2026_01:
      return "2026-01";
  }
  return "unknown";
}

// Inline CSS for the entire page.
constexpr const char* kStyleSheet = R"CSS(
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Helvetica,Arial,sans-serif;
     background:#f5f7fa;color:#1a1a1a;padding:20px;line-height:1.5}
.header{text-align:center;margin-bottom:24px}
.header h1{font-size:1.6em;margin-bottom:4px}
.header .meta{color:#666;font-size:0.85em}
.header .meta span{margin:0 8px}
table{border-collapse:collapse;width:100%;background:#fff;
      box-shadow:0 1px 4px rgba(0,0,0,0.08);border-radius:6px;overflow:hidden}
thead{background:#2d3748;color:#fff}
thead th{padding:8px 6px;font-size:0.78em;text-transform:uppercase;letter-spacing:0.04em;
         white-space:nowrap;text-align:center;border-right:1px solid #3d4a5e}
thead th:last-child{border-right:none}
thead th.prob-hdr{min-width:56px;position:relative}
thead th.prob-hdr .prob-color{display:block;width:14px;height:14px;border-radius:2px;
      margin:0 auto 2px auto;border:1px solid rgba(255,255,255,0.3)}
tbody tr{border-bottom:1px solid #e8ecf0;transition:background .15s}
tbody tr:hover{background:#f0f4f8}
tbody tr.awarded{background:#fffbe6}
tbody td{padding:6px 8px;font-size:0.82em;text-align:center;vertical-align:middle;
         border-right:1px solid #e8ecf0}
tbody td:last-child{border-right:none}
td.place{font-weight:700;color:#2d3748;min-width:38px}
td.team-cell{text-align:left;max-width:300px;min-width:160px}
td.org-cell{text-align:left;color:#555;max-width:220px;min-width:100px;font-size:0.78em}
td.solved{font-weight:700;color:#2d3748}
td.penalty{color:#555}
.team-name{font-weight:600}
.badge{display:inline-block;font-size:0.68em;padding:1px 6px;border-radius:3px;
       margin-left:4px;vertical-align:middle;font-weight:600;white-space:nowrap}
.badge-gold{background:#ffd700;color:#4a3800}
.badge-silver{background:#c0c0c0;color:#333}
.badge-bronze{background:#cd7f32;color:#fff}
.badge-default{background:#e2e8f0;color:#2d3748}
.cell-solved{background:#d4edda;color:#155724}
.cell-failed{background:#f8d7da;color:#721c24}
.cell-empty{background:#f7f8fa;color:#aaa}
.cell-fts{background:#b8daff;color:#004085;font-weight:700}
.cell-main{font-weight:600;font-size:0.9em}
.cell-detail{font-size:0.72em;color:inherit;opacity:0.8}
.footer{margin-top:24px;padding:16px;background:#fff;border-radius:6px;
        box-shadow:0 1px 4px rgba(0,0,0,0.08);font-size:0.82em;color:#555}
.footer h3{font-size:0.95em;margin-bottom:8px;color:#2d3748}
.footer .stat{margin:2px 0}
.footer .diag-list{margin-top:8px;font-family:monospace;font-size:0.78em;
                   max-height:200px;overflow-y:auto;background:#f7f8fa;padding:8px;border-radius:4px}
)CSS";

// Choose a badge CSS class based on award id.
std::string BadgeClass(const std::string& award_id) {
  if (award_id.find("gold") != std::string::npos) return "badge-gold";
  if (award_id.find("silver") != std::string::npos) return "badge-silver";
  if (award_id.find("bronze") != std::string::npos) return "badge-bronze";
  if (award_id.find("winner") != std::string::npos) return "badge-gold";
  return "badge-default";
}

}  // namespace

void RenderScoreboardHtml(std::ostream& out,
                          const ScoreboardSnapshot& snapshot,
                          const RenderContext& ctx) {
  // Title.
  std::string title = ctx.page_title.empty()
                          ? HtmlEscape(snapshot.contest_formal_name.empty()
                                           ? snapshot.contest_name
                                           : snapshot.contest_formal_name)
                          : HtmlEscape(ctx.page_title);
  if (title.empty()) title = "Scoreboard Preview";

  out << "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n"
      << "<meta charset=\"utf-8\">\n"
      << "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n"
      << "<title>" << title << "</title>\n"
      << "<style>" << kStyleSheet << "</style>\n"
      << "</head>\n<body>\n";

  // ---- Header ----
  out << "<div class=\"header\">\n"
      << "  <h1>" << title << "</h1>\n"
      << "  <div class=\"meta\">";
  if (!snapshot.contest_name.empty()) {
    out << "<span>Contest: " << HtmlEscape(snapshot.contest_name) << "</span>";
  }
  if (!ctx.eventfeed_path.empty()) {
    out << "<span>Source: <code>" << HtmlEscape(ctx.eventfeed_path)
        << "</code></span>";
  }
  out << "<span>Version: " << VersionString(ctx.resolved_version) << "</span>";
  if (!ctx.generation_time.empty()) {
    out << "<span>Generated: " << HtmlEscape(ctx.generation_time) << "</span>";
  }
  // Phase timing.
  if (ctx.parse_seconds > 0.0) {
    out << "<br/><span style=\"font-size:0.78em;color:#888\">"
        << "Parse: " << std::fixed << std::setprecision(3)
        << ctx.parse_seconds << "s"
        << " | Scoreboard: " << ctx.scoreboard_rebuild_seconds << "s"
        << " | Render: " << ctx.html_render_seconds << "s"
        << " | Total: " << ctx.total_seconds << "s"
        << "</span>";
  }
  out << "</div>\n</div>\n";

  // ---- Table ----
  out << "<table>\n<thead><tr>\n"
      << "  <th>#</th>\n"
      << "  <th>Team</th>\n"
      << "  <th>Organization</th>\n"
      << "  <th>Solved</th>\n"
      << "  <th>Penalty</th>\n";
  for (const auto& pc : snapshot.problems) {
    out << "  <th class=\"prob-hdr\">";
    if (!pc.rgb.empty()) {
      out << "<span class=\"prob-color\" style=\"background:"
          << HtmlEscape(pc.rgb) << "\"></span>";
    }
    out << HtmlEscape(pc.label) << "</th>\n";
  }
  out << "</tr></thead>\n<tbody>\n";

  for (const auto& row : snapshot.rows) {
    bool has_awards = !row.awards.empty();
    out << "<tr" << (has_awards ? " class=\"awarded\"" : "") << ">\n";

    // Place.
    out << "  <td class=\"place\">" << row.place << "</td>\n";

    // Team + awards.
    out << "  <td class=\"team-cell\"><span class=\"team-name\">"
        << HtmlEscape(row.team_name) << "</span>";
    for (const auto& aw : row.awards) {
      out << "<span class=\"badge " << BadgeClass(aw.award_id) << "\">"
          << HtmlEscape(aw.citation) << "</span>";
    }
    out << "</td>\n";

    // Organization.
    out << "  <td class=\"org-cell\">"
        << HtmlEscape(row.organization_name) << "</td>\n";

    // Solved / penalty.
    out << "  <td class=\"solved\">" << row.solved << "</td>\n";
    out << "  <td class=\"penalty\">" << row.penalty << "</td>\n";

    // Problem cells.
    for (const auto& cell : row.cells) {
      std::string cls;
      std::string main_text;
      std::string detail_text;
      switch (cell.status) {
        case ProblemStatus::kSolved:
          cls = cell.is_first_to_solve ? "cell-fts" : "cell-solved";
          main_text = "+";
          if (cell.attempts > 1) {
            main_text += std::to_string(cell.attempts - 1);
          }
          detail_text =
              std::to_string(cell.attempts) + "/" +
              std::to_string(cell.time_minutes);
          break;
        case ProblemStatus::kFailed:
          cls = "cell-failed";
          main_text = "-" + std::to_string(cell.attempts);
          detail_text =
              std::to_string(cell.attempts) + " try";
          break;
        case ProblemStatus::kEmpty:
          cls = "cell-empty";
          main_text = ".";
          break;
      }
      out << "  <td class=\"" << cls << "\">"
          << "<div class=\"cell-main\">" << main_text << "</div>";
      if (!detail_text.empty()) {
        out << "<div class=\"cell-detail\">" << detail_text << "</div>";
      }
      out << "</td>\n";
    }

    out << "</tr>\n";
  }

  out << "</tbody>\n</table>\n";

  // ---- Footer / diagnostics ----
  out << "<div class=\"footer\">\n"
      << "  <h3>Parser Diagnostics</h3>\n"
      << "  <div class=\"stat\">Total events: "
      << ctx.cursor.event_count << "</div>\n"
      << "  <div class=\"stat\">Lines processed: "
      << ctx.cursor.line_no << "</div>\n"
      << "  <div class=\"stat\">End of updates: "
      << (ctx.cursor.end_of_updates ? "yes" : "no") << "</div>\n";
  if (ctx.cursor.last_token.has_value()) {
    out << "  <div class=\"stat\">Last token: "
        << HtmlEscape(*ctx.cursor.last_token) << "</div>\n";
  }
  out << "  <div class=\"stat\">Diagnostics: "
      << ctx.total_diagnostics << " total ("
      << ctx.error_count << " errors, "
      << ctx.warning_count << " warnings)</div>\n";

  if (!ctx.diagnostic_samples.empty()) {
    out << "  <div class=\"diag-list\">\n";
    for (const auto& msg : ctx.diagnostic_samples) {
      out << "    <div>" << HtmlEscape(msg) << "</div>\n";
    }
    if (ctx.total_diagnostics > ctx.diagnostic_samples.size()) {
      out << "    <div>... and "
          << (ctx.total_diagnostics - ctx.diagnostic_samples.size())
          << " more</div>\n";
    }
    out << "  </div>\n";
  }

  out << "</div>\n</body>\n</html>\n";
}

}  // namespace tools
}  // namespace ccsparser
