# CCSParser API Reference

## Overview

CCSParser is a C++20 library for parsing CCS (Contest Control System) event-feed
and contest-package data as defined in CCS Spec 2023-06 and 2026-01.  It provides
typed object models, a state store, scoreboard building, and comprehensive
diagnostics.

All public types live in `namespace ccsparser`.  Include `"ccsparser/ccsparser.h"`
for the full API or individual headers for specific modules.

---

## Parsing

### EventFeedParser

Static methods for parsing NDJSON event-feeds.

| Method | Description |
|--------|-------------|
| `ParseFile(path, options)` | Parse a local `.ndjson` file. Returns `StatusOr<ParseResult>`. |
| `ParseStream(istream, options)` | Parse from any `std::istream`. Returns `StatusOr<ParseResult>`. |
| `CreateStreamingSession(options)` | Create an incremental session for line-by-line feeding. Returns `StatusOr<unique_ptr<StreamingParseSession>>`. |

### PackageParser

| Method | Description |
|--------|-------------|
| `ParsePackageDirectory(path, options)` | Parse a CCS contest-package directory (looks for `event-feed.ndjson` first, then endpoint JSON files). |
| `ParsePackageZip(path, options)` | Extract a `.zip` contest package to a temp dir and parse it. |

### StreamingParseSession

| Method | Description |
|--------|-------------|
| `ConsumeLine(line)` | Feed one line of input. Returns `Status`. |
| `Finish()` | Signal end of input. |
| `cursor()` | Current `EventCursor` (line count, token, end-of-updates). |
| `store()` | Immutable reference to the built `ContestStore`. |
| `mutable_store()` | Mutable reference (for checkpoint/rollback). |
| `diagnostics()` | All diagnostics emitted so far. |
| `resolved_version()` | Detected or explicit `ApiVersion`. |

### ParseResult

Returned by batch parse methods.

| Field | Type | Description |
|-------|------|-------------|
| `resolved_version` | `ApiVersion` | Final resolved version. |
| `store` | `ContestStore` | Populated contest state. |
| `cursor` | `EventCursor` | Final cursor position. |
| `diagnostics` | `vector<Diagnostic>` | All diagnostics. |

---

## Configuration

### ParseOptions

| Field | Default | Description |
|-------|---------|-------------|
| `version` | `kAuto` | `kAuto`, `k2023_06`, or `k2026_01`. |
| `error_policy` | `kContinue` | `kContinue` skips bad records; `kFailFast` aborts. |
| `unknown_field_policy` | `kPreserve` | `kPreserve`, `kDrop`, or `kError`. |
| `unknown_type_policy` | `kWarnAndIgnore` | `kWarnAndIgnore`, `kPreserveRaw`, or `kError`. |
| `keep_event_log` | `true` | Store raw events for replay/checkpoint. |
| `keep_raw_json` | `false` | Attach raw line to diagnostics. |
| `enable_validation` | `true` | Run field validation. |
| `enable_checkpointing` | `true` | Enable checkpoint/rollback support. |
| `limits` | (see below) | Defensive parse limits. |

### ParseLimits

| Field | Default | Description |
|-------|---------|-------------|
| `max_line_bytes` | 64 MB | Skip lines exceeding this length. |
| `max_diagnostics` | 10 000 | Cap stored diagnostics. |
| `max_consecutive_errors` | 100 | Abort after N consecutive failures. |

---

## ContestStore

Central state store populated by the parser.

### Queries

| Method | Returns | Description |
|--------|---------|-------------|
| `GetObject(type, id)` | `StatusOr<const ContestObject*>` | Look up by type + ID. |
| `ListObjects(type)` | `vector<const ContestObject*>` | All objects of a type. |
| `GetContest()` | `const Contest*` | Singleton contest object (or `nullptr`). |
| `GetState()` | `const State*` | Singleton state object (or `nullptr`). |
| `GetEventCount()` | `size_t` | Total events applied. |

### Checkpoint / Rollback

| Method | Description |
|--------|-------------|
| `CreateCheckpoint()` | Capture current position as a `Checkpoint`. |
| `Rollback(cp)` | Truncate event log to checkpoint and clear store state. |

### Observer

Register via `store.AddObserver(shared_ptr<Observer>)`.

| Callback | When |
|----------|------|
| `OnRawEventParsed` | After line parsing, before decode. |
| `OnObjectUpserted` | After a successful upsert. |
| `OnObjectDeleted` | After a successful delete. |
| `OnCollectionReplaced` | After an atomic collection replace. |
| `OnDiagnostic` | On every diagnostic. |
| `OnEndOfUpdates` | When `state.end_of_updates` is seen. |

---

## Object Model

All objects inherit from `ContestObject` which carries `type`, `id`, and
`unknown_fields`.  Down-cast via `dynamic_cast<const T*>(obj)`.

| Struct | ObjectType | Key Fields |
|--------|-----------|------------|
| `Contest` | `kContest` | name, formal_name, start_time, duration, penalty_time, banner, logo |
| `JudgementType` | `kJudgementTypes` | name, penalty, solved |
| `Language` | `kLanguages` | name, extensions |
| `Problem` | `kProblems` | ordinal, label, name, rgb, time_limit, statement |
| `Group` | `kGroups` | name, hidden, sortorder |
| `Organization` | `kOrganizations` | name, country, logo |
| `Team` | `kTeams` | name, organization_id, group_ids, hidden |
| `Person` | `kPersons` | name, role, team_id |
| `Account` | `kAccounts` | username, account_type, team_id |
| `State` | `kState` | started, ended, frozen, finalized, end_of_updates |
| `Submission` | `kSubmissions` | language_id, problem_id, team_id, contest_time |
| `Judgement` | `kJudgements` | submission_id, judgement_type_id, start/end times |
| `Run` | `kRuns` | judgement_id, ordinal, judgement_type_id |
| `Clarification` | `kClarifications` | from_team_id, to_team_ids, text |
| `Award` | `kAwards` | citation, team_ids |
| `Commentary` | `kCommentary` | message, team_ids, problem_ids |

### Time Types

- **`RelativeTime`** — milliseconds; use `.ToString()` for `h:mm:ss.uuu` format.
- **`AbsoluteTime`** — epoch milliseconds + timezone offset; ISO 8601 via `.ToString()`.

### FileRef

Represents a CCS file reference: `href`, `filename`, `mime`, `width`, `height`,
`tags` (2026-01), and preserved unknown fields.

---

## Scoreboard Builder

`#include "ccsparser/scoreboard_builder.h"`

```cpp
StatusOr<ScoreboardSnapshot> BuildScoreboard(const ContestStore& store);
```

Constructs a final ICPC-style scoreboard from a populated store.

### ScoreboardSnapshot

| Field | Type | Description |
|-------|------|-------------|
| `contest_name` | `string` | Contest display name. |
| `penalty_time_minutes` | `int64_t` | Penalty per wrong attempt. |
| `problems` | `vector<ProblemColumn>` | Ordered problem columns. |
| `rows` | `vector<ScoreboardRow>` | Ranked team rows. |

### ScoreboardRow

| Field | Type | Description |
|-------|------|-------------|
| `place` | `int` | Rank (tied teams share a rank). |
| `team_id` / `team_name` | `string` | Team identity. |
| `organization_id` / `organization_name` | `string` | Organization. |
| `solved` | `int` | Total problems solved. |
| `penalty` | `int64_t` | Total penalty (minutes). |
| `cells` | `vector<ProblemResultCell>` | Per-problem results. |
| `awards` | `vector<TeamAward>` | Associated awards. |

### ProblemResultCell

| Field | Type | Description |
|-------|------|-------------|
| `status` | `ProblemStatus` | `kSolved`, `kFailed`, or `kEmpty`. |
| `attempts` | `int` | Total submission count. |
| `time_minutes` | `int64_t` | Accept time in contest minutes (0 if unsolved). |
| `is_first_to_solve` | `bool` | True if this team solved earliest. |

Ranking rules: solved ↓, penalty ↑, team_id (stable tie-break).

---

## Diagnostics

Every parse issue produces a `Diagnostic` with:

| Field | Description |
|-------|-------------|
| `severity` | `kInfo`, `kWarning`, or `kError`. |
| `code` | `DiagnosticCode` enum (e.g. `kMalformedJson`, `kDeleteUnknownObject`). |
| `message` | Human-readable description. |
| `line_no` | Source line number. |
| `raw_line` | Original line (if `keep_raw_json` is on). |
| `object_type` / `object_id` | Context (if available). |

### Error Recovery

| Category | Behavior |
|----------|----------|
| Bad JSON / too-long line | Skip line, no store mutation. |
| Missing type / data | Discard event, no token update. |
| Object decode failure | Atomic failure, old state preserved. |
| Collection replace failure | Entire replace fails, old collection preserved. |
| Delete unknown object | Warning only. |
| Unknown type / field | Warning + preserve (configurable). |
| Consecutive errors exceed limit | Abort with `kLimitExceeded`. |

---

## Version Differences

| Feature | 2023-06 | 2026-01 | Internal |
|---------|---------|---------|----------|
| `contest.penalty_time` | `int` (minutes) | `RELTIME` string | `RelativeTime` (ms) |
| Clarification recipient | `to_team_id` | `to_team_ids` / `to_group_ids` | `vector<string>` |
| Awards | standard | + honors variants | string, no enum |
| Problem limits | — | memory/output/code | `optional<int>` |
| `FILE.tag` | — | `array<string>` | `vector<string>` |

Auto-detection inspects `penalty_time` type and clarification field names; defaults
to 2023-06 when ambiguous.  Explicit version is recommended.

---

## Building

```bash
bazel build //:ccsparser          # Library only
bazel test  //...                  # All tests
bazel build //:eventfeed_scoreboard_preview   # Preview tool
```

Requires Bazel 9.0.1 and a C++20 toolchain.
