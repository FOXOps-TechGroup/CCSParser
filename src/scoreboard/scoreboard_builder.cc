// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

#include "ccsparser/scoreboard_builder.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ccsparser/contest_store.h"
#include "ccsparser/objects.h"
#include "ccsparser/scoreboard.h"
#include "ccsparser/status.h"
#include "ccsparser/types.h"

namespace ccsparser {

namespace {

// Per-problem accumulator while building the scoreboard.
struct ProblemAccum {
  int num_judged = 0;         // All judged submissions (per CCS spec).
  int penalty_attempts = 0;   // Attempts that carry penalty (excl. CE etc.).
  bool solved = false;
  int64_t accept_time_ms = 0; // Contest-relative ms of the first AC.
};

// Per-team accumulator.
struct TeamAccum {
  std::string team_id;
  std::unordered_map<std::string, ProblemAccum> problems;  // problem_id -> accum.
};

}  // namespace

StatusOr<ScoreboardSnapshot> BuildScoreboard(const ContestStore& store) {
  // ---- Collect contest metadata ----
  ScoreboardSnapshot snap;
  int64_t contest_duration_ms = INT64_MAX;  // Unlimited if not set.
  const Contest* contest = store.GetContest();
  if (contest) {
    snap.contest_name = contest->name.value_or("");
    snap.contest_formal_name = contest->formal_name.value_or("");
    if (contest->penalty_time.has_value()) {
      snap.penalty_time_minutes =
          contest->penalty_time->milliseconds / 60000;
    }
    if (contest->duration.has_value()) {
      contest_duration_ms = contest->duration->milliseconds;
    }
  }

  // Per CCS spec, state.ended is "Time when the contest ended" — the
  // authoritative end.  contest.duration is a configuration field that CMS
  // admins can update independently (e.g. DOMjudge may extend judging window
  // via duration while the contest itself already ended at state.ended).
  // We use min(duration, state.ended - start_time) so that:
  //   • Genuine extensions (both duration and state.ended updated) pass
  //     through correctly.
  //   • Admin window hacks (duration extended but state.ended unchanged) are
  //     correctly capped at the actual contest end.
  const State* state = store.GetState();
  if (state && state->ended.has_value() &&
      contest && contest->start_time.has_value()) {
    int64_t effective_ms =
        state->ended->epoch_ms - contest->start_time->epoch_ms;
    if (effective_ms > 0 && effective_ms < contest_duration_ms) {
      contest_duration_ms = effective_ms;
    }
  }

  // ---- Build problem columns sorted by ordinal ----
  auto prob_objs = store.ListObjects(ObjectType::kProblems);
  struct ProbInfo {
    int ordinal;
    std::string id;
    std::string label;
    std::string name;
    std::string rgb;
  };
  std::vector<ProbInfo> prob_infos;
  prob_infos.reserve(prob_objs.size());
  for (const auto* obj : prob_objs) {
    const auto* p = dynamic_cast<const Problem*>(obj);
    if (!p) continue;
    prob_infos.push_back({p->ordinal.value_or(0), p->id,
                          p->label.value_or(p->id),
                          p->name.value_or(""), p->rgb.value_or("")});
  }
  std::sort(prob_infos.begin(), prob_infos.end(),
            [](const ProbInfo& a, const ProbInfo& b) {
              if (a.ordinal != b.ordinal) return a.ordinal < b.ordinal;
              return a.label < b.label;
            });
  snap.problems.reserve(prob_infos.size());
  for (const auto& pi : prob_infos) {
    snap.problems.push_back({pi.id, pi.label, pi.name, pi.rgb});
  }

  // ---- Index judgement-types by id ----
  std::unordered_map<std::string, const JudgementType*> jt_map;
  for (const auto* obj : store.ListObjects(ObjectType::kJudgementTypes)) {
    const auto* jt = dynamic_cast<const JudgementType*>(obj);
    if (jt) jt_map[jt->id] = jt;
  }

  // ---- Index submissions by id ----
  std::unordered_map<std::string, const Submission*> sub_map;
  {
    auto sub_objs = store.ListObjects(ObjectType::kSubmissions);
    sub_map.reserve(sub_objs.size());
    for (const auto* obj : sub_objs) {
      const auto* s = dynamic_cast<const Submission*>(obj);
      if (s) sub_map[s->id] = s;
    }
  }

  // ---- Accumulate per-team, per-problem results from judgements ----
  // We iterate over judgements (not submissions) because a submission may
  // have multiple judgements.  Per CCS spec:
  //   • A judgement object is updated "once with the final verdict" (null
  //     judgement_type_id → final verdict).  The ContestStore holds the
  //     latest version of each judgement object.
  //   • Post-contest rejudges produce a NEW judgement ID with the same
  //     submission_id.  In that case the judgement with the later end_time
  //     is the authoritative result (CCS awards note: first-to-solve "must
  //     never change once set, except if there are rejudgements").
  //
  // Strategy: for each submission, pick the judgement with the latest
  // end_time (absolute wall-clock) among all completed judgements.  This
  // correctly handles both in-contest judging and post-contest rejudges.

  struct JudgeSummary {
    std::string judgement_type_id;
    int64_t end_time_ms = 0;  // Absolute epoch ms; larger = more recent.
  };
  std::unordered_map<std::string, JudgeSummary> best_judgement;
  {
    auto jdg_objs = store.ListObjects(ObjectType::kJudgements);
    best_judgement.reserve(jdg_objs.size());
    for (const auto* obj : jdg_objs) {
      const auto* j = dynamic_cast<const Judgement*>(obj);
      // Skip judgements without a verdict (still pending).
      if (!j || !j->submission_id.has_value() ||
          !j->judgement_type_id.has_value())
        continue;
      int64_t et = j->end_time.has_value() ? j->end_time->epoch_ms : 0;
      auto [it, inserted] =
          best_judgement.try_emplace(*j->submission_id,
                                     JudgeSummary{*j->judgement_type_id, et});
      if (!inserted && et > it->second.end_time_ms) {
        it->second = {*j->judgement_type_id, et};
      }
    }
  }

  // Collect hidden team ids so we can skip their submissions.
  std::unordered_set<std::string> hidden_team_ids;

  // Build team accumulators and index teams/orgs in a single pass.
  std::unordered_map<std::string, TeamAccum> team_accums;
  std::unordered_map<std::string, const Team*> team_map;
  {
    auto team_objs = store.ListObjects(ObjectType::kTeams);
    team_accums.reserve(team_objs.size());
    team_map.reserve(team_objs.size());
    for (const auto* obj : team_objs) {
      const auto* t = dynamic_cast<const Team*>(obj);
      if (!t) continue;
      team_map[t->id] = t;
      // Skip hidden teams from scoreboard (CCS spec: teams.hidden).
      if (t->hidden.has_value() && *t->hidden) {
        hidden_team_ids.insert(t->id);
        continue;
      }
      team_accums[t->id].team_id = t->id;
    }
  }

  std::unordered_map<std::string, const Organization*> org_map;
  for (const auto* obj : store.ListObjects(ObjectType::kOrganizations)) {
    const auto* o = dynamic_cast<const Organization*>(obj);
    if (o) org_map[o->id] = o;
  }

  // Process submissions in chronological order (by contest_time), so that
  // per-team, per-problem accumulators correctly count attempts before AC.
  struct SubWithJudgement {
    const Submission* sub;
    const JudgeSummary* js;
    int64_t contest_time_ms;
  };
  std::vector<SubWithJudgement> ordered_subs;
  ordered_subs.reserve(best_judgement.size());
  for (const auto& [sub_id, js] : best_judgement) {
    auto sit = sub_map.find(sub_id);
    if (sit == sub_map.end()) continue;
    const Submission* sub = sit->second;
    if (!sub->team_id.has_value() || !sub->problem_id.has_value()) continue;
    int64_t ct =
        sub->contest_time.has_value() ? sub->contest_time->milliseconds : 0;
    // Skip submissions made at or after the contest duration (post-contest).
    if (ct >= contest_duration_ms) continue;
    ordered_subs.push_back({sub, &js, ct});
  }
  std::sort(ordered_subs.begin(), ordered_subs.end(),
            [](const SubWithJudgement& a, const SubWithJudgement& b) {
              return a.contest_time_ms < b.contest_time_ms;
            });

  for (const auto& swj : ordered_subs) {
    const Submission* sub = swj.sub;
    const std::string& team_id = *sub->team_id;
    const std::string& prob_id = *sub->problem_id;

    // Skip submissions from hidden teams (CCS spec: teams.hidden).
    if (hidden_team_ids.count(team_id)) continue;

    // Skip submissions from teams not present in the scoreboard (unknown
    // team_ids in the feed — invalid by spec but we must be robust).
    auto ta_it = team_accums.find(team_id);
    if (ta_it == team_accums.end()) continue;
    auto& ta = ta_it->second;
    auto& pa = ta.problems[prob_id];

    // Already solved — ignore further submissions for this problem.
    if (pa.solved) continue;

    auto jt_it = jt_map.find(swj.js->judgement_type_id);
    bool is_solved =
        jt_it != jt_map.end() && jt_it->second->solved.value_or(false);
    bool counts_as_penalty =
        jt_it != jt_map.end() && jt_it->second->penalty.value_or(true);

    // CCS spec: num_judged counts all judged submissions up to and
    // including the first correct one.
    pa.num_judged++;

    // Only count attempts that carry penalty (CCS spec: judgement-types.penalty).
    if (is_solved || counts_as_penalty) {
      pa.penalty_attempts++;
    }
    if (is_solved) {
      pa.solved = true;
      pa.accept_time_ms =
          sub->contest_time.has_value() ? sub->contest_time->milliseconds : 0;
    }
  }

  // ---- Determine first-to-solve per problem ----
  // Earliest accept_time_ms among solved teams for each problem.
  std::unordered_map<std::string, std::pair<int64_t, std::string>> first_solve;
  for (const auto& [tid, ta] : team_accums) {
    for (const auto& [pid, pa] : ta.problems) {
      if (!pa.solved) continue;
      auto [it, inserted] =
          first_solve.try_emplace(pid, pa.accept_time_ms, tid);
      if (!inserted && pa.accept_time_ms < it->second.first) {
        it->second = {pa.accept_time_ms, tid};
      }
    }
  }

  // ---- Index awards per team ----
  std::unordered_map<std::string, std::vector<TeamAward>> team_awards;
  for (const auto* obj : store.ListObjects(ObjectType::kAwards)) {
    const auto* a = dynamic_cast<const Award*>(obj);
    if (!a) continue;
    TeamAward ta;
    ta.award_id = a->id;
    ta.citation = a->citation.value_or(a->id);
    for (const auto& tid : a->team_ids) {
      team_awards[tid].push_back(ta);
    }
  }

  // ---- Build rows ----
  std::vector<ScoreboardRow> rows;
  rows.reserve(team_accums.size());
  for (const auto& [tid, ta] : team_accums) {
    ScoreboardRow row;
    row.team_id = tid;

    auto tm_it = team_map.find(tid);
    const Team* team = (tm_it != team_map.end()) ? tm_it->second : nullptr;

    // CCS spec: "display_name: If not set, a client should revert to name."
    if (team) {
      row.team_name = team->display_name.has_value()
                          ? *team->display_name
                          : team->name.value_or(tid);
    } else {
      row.team_name = tid;
    }
    if (team && team->organization_id.has_value()) {
      row.organization_id = *team->organization_id;
      auto oit = org_map.find(*team->organization_id);
      if (oit != org_map.end()) {
        row.organization_name = oit->second->name.value_or("");
      }
    }

    // Awards.
    auto aw_it = team_awards.find(tid);
    if (aw_it != team_awards.end()) {
      row.awards = aw_it->second;
    }

    // Problem cells.
    // Per-ICPC rules: floor each problem's solve time to minutes individually,
    // then sum. Do NOT accumulate raw milliseconds and divide at the end, as
    // that yields a different (incorrect) result when fractional-second parts
    // across multiple problems sum to >= 60 seconds.
    int total_solved = 0;
    int64_t total_penalty_minutes = 0;
    row.cells.reserve(prob_infos.size());
    for (const auto& pc : prob_infos) {
      ProblemResultCell cell;
      cell.problem_id = pc.id;
      cell.label = pc.label;
      auto pit = ta.problems.find(pc.id);
      if (pit != ta.problems.end()) {
        const auto& pa = pit->second;
        if (pa.solved) {
          cell.status = ProblemStatus::kSolved;
          cell.attempts = pa.num_judged;
          // CCS spec: "time: integer - Minutes into the contest when this
          // problem was solved." Negative is valid (pre-contest submissions).
          cell.time_minutes = pa.accept_time_ms / 60000;
          total_solved++;
          // Penalty = floor(solve_time_ms / 60000) + wrong_penalty_attempts *
          // penalty_time_minutes. Only penalty-carrying attempts count.
          total_penalty_minutes +=
              cell.time_minutes +
              static_cast<int64_t>(pa.penalty_attempts - 1) *
                  snap.penalty_time_minutes;
          // First to solve?
          auto fts = first_solve.find(pc.id);
          if (fts != first_solve.end() && fts->second.second == tid) {
            cell.is_first_to_solve = true;
          }
        } else if (pa.num_judged > 0) {
          cell.status = ProblemStatus::kFailed;
          cell.attempts = pa.num_judged;
        }
      }
      row.cells.push_back(std::move(cell));
    }
    row.solved = total_solved;
    row.penalty = total_penalty_minutes;
    rows.push_back(std::move(row));
  }

  // ---- Sort by ICPC rules ----
  // CCS spec: "sorted according to rank and alphabetical on team name
  // within identically ranked teams."
  std::sort(rows.begin(), rows.end(),
            [](const ScoreboardRow& a, const ScoreboardRow& b) {
              if (a.solved != b.solved) return a.solved > b.solved;
              if (a.penalty != b.penalty) return a.penalty < b.penalty;
              return a.team_name < b.team_name;  // Alphabetical tie-break.
            });

  // ---- Assign places ----
  for (size_t i = 0; i < rows.size(); ++i) {
    if (i == 0) {
      rows[i].place = 1;
    } else if (rows[i].solved == rows[i - 1].solved &&
               rows[i].penalty == rows[i - 1].penalty) {
      rows[i].place = rows[i - 1].place;
    } else {
      rows[i].place = static_cast<int>(i + 1);
    }
  }

  snap.rows = std::move(rows);
  return snap;
}

}  // namespace ccsparser
