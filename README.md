# CCSParser

A high-stability, extensible C++20 library for parsing CCS (Contest Control System) event-feed and contest package data. Designed for ICPC-style programming contests.

## Supported Versions

- **CCS Spec 2023-06** — full event-feed + contest package support
- **CCS Spec 2026-01** — full event-feed + contest package support
- **Auto-detection** — best-effort version detection from feed content

## Supported Inputs

| Input Type | API |
|---|---|
| `std::istream` stream | `EventFeedParser::ParseStream(stream, options)` |
| Local `.ndjson` file | `EventFeedParser::ParseFile(path, options)` |
| Contest package directory | `PackageParser::ParsePackageDirectory(path, options)` |
| Contest package ZIP | `PackageParser::ParsePackageZip(path, options)` |
| Incremental line-by-line | `EventFeedParser::CreateStreamingSession(options)` |

## Quick Start

```cpp
#include "ccsparser/ccsparser.h"

using namespace ccsparser;

// Parse from file
ParseOptions opts;
opts.version = ApiVersion::kAuto;
opts.error_policy = ErrorPolicy::kContinue;

auto result = EventFeedParser::ParseFile("event-feed.ndjson", opts);
if (result.ok()) {
    auto& pr = result.value();
    auto* contest = pr.store.GetContest();
    auto teams = pr.store.ListObjects(ObjectType::kTeams);
    // ...
}
```

## Core API

### ParseOptions

```cpp
struct ParseOptions {
    ApiVersion version = ApiVersion::kAuto;
    ErrorPolicy error_policy = ErrorPolicy::kContinue;
    UnknownFieldPolicy unknown_field_policy = UnknownFieldPolicy::kPreserve;
    UnknownTypePolicy unknown_type_policy = UnknownTypePolicy::kWarnAndIgnore;
    bool keep_event_log = true;
    bool keep_raw_json = false;
    bool enable_validation = true;
    bool enable_checkpointing = true;
    ParseLimits limits;
};
```

### ContestStore

- `GetObject(type, id)` — lookup by type and ID
- `ListObjects(type)` — list all objects of a type
- `GetContest()` / `GetState()` — singleton access
- `GetEventCount()` — total events processed
- `CreateCheckpoint()` / `Rollback(cp)` — checkpoint/rollback
- `AddObserver(observer)` — register for notifications

### Observer

```cpp
class Observer {
    virtual void OnRawEventParsed(const RawEvent& event);
    virtual void OnObjectUpserted(ObjectType type, const std::string& id, const ContestObject& obj);
    virtual void OnObjectDeleted(ObjectType type, const std::string& id);
    virtual void OnCollectionReplaced(ObjectType type, size_t count);
    virtual void OnDiagnostic(const Diagnostic& diag);
    virtual void OnEndOfUpdates();
};
```

### StreamingParseSession

```cpp
auto session = EventFeedParser::CreateStreamingSession(opts);
session->ConsumeLine(line);
session->Finish();
const auto& store = session->store();
```

## Supported Object Types

contest, judgement-types, languages, problems, groups, organizations, teams, persons, accounts, state, submissions, judgements, runs, clarifications, awards, commentary

## Version Differences Handled

| Feature | 2023-06 | 2026-01 | Internal |
|---|---|---|---|
| `contest.penalty_time` | `int` (minutes) | `RELTIME` string | `RelativeTime` (ms) |
| `clarifications` recipient | `to_team_id` | `to_team_ids` / `to_group_ids` | `vector<string>` |
| `awards` | standard | + honors/high-honors/highest-honors | string (no enum) |
| `problems` limits | — | memory/output/code_limit | `optional<int>` |
| `judgements.current` | — | `boolean` | `optional<bool>` |
| `FILE.tag` | — | `array of string` | `vector<string>` |

## Diagnostics & Error Handling

Two error policies:
- **`kContinue`** (default) — skip bad records, emit diagnostics, continue
- **`kFailFast`** — abort on first error

### Diagnostic Codes

`malformed_json`, `invalid_utf8`, `line_too_long`, `missing_type`, `invalid_type_field`, `missing_data`, `invalid_id_type`, `unknown_event_type`, `invalid_object_shape`, `invalid_required_field`, `invalid_time`, `invalid_reltime`, `invalid_collection_item`, `delete_unknown_object`, `version_conflict`, `max_consecutive_errors_exceeded`

## Corrupted Record Handling

| Category | Treatment |
|---|---|
| **Line-level** (bad JSON, too long) | Skip line, emit diagnostic, no store mutation |
| **Event skeleton** (missing type/data) | Discard event, no token/cursor update |
| **Object data** (bad fields) | Atomic event failure, old state preserved |
| **Collection replace** (any element fails) | Entire replace fails, old collection preserved |
| **Singleton update** (decode failure) | Old singleton preserved |
| **Delete unknown** | Warning diagnostic, not fatal |
| **Unknown type** | Warning + ignore (configurable) |
| **Unknown field** | Preserve + warning (configurable) |

### Parse Limits

```cpp
struct ParseLimits {
    size_t max_line_bytes = 64 * 1024 * 1024;
    size_t max_diagnostics = 10000;
    size_t max_consecutive_errors = 100;
};
```

## Building

```bash
bazel build //:ccsparser
bazel test //...
```

Requires: Bazel 9.0.1, C++20 compiler.

## Scoreboard Builder

The library includes a reusable scoreboard module (`#include "ccsparser/scoreboard_builder.h"`) that constructs ICPC-style final standings from a `ContestStore`:

```cpp
auto sb = ccsparser::BuildScoreboard(parse_result.store);
// sb->rows: ranked teams with solved, penalty, per-problem cells, awards
```

Ranking: solved ↓, penalty ↑, team_id (stable tie-break).  Awards are associated
with team rows.  First-to-solve is flagged per cell.

## Scoreboard Preview Tool

A standalone `cc_binary` for human review of parsed event-feeds:

```bash
bazel run //:eventfeed_scoreboard_preview -- \
    --eventfeed=/path/to/event-feed.ndjson \
    --output=scoreboard.html \
    [--title="My Contest"] \
    [--version=auto|2023-06|2026-01]
```

Produces a self-contained HTML file with:
- Full ranked scoreboard table with problem cells
- Award badges (gold/silver/bronze/default styling)
- First-to-solve highlights
- Problem color blocks in column headers
- Parser diagnostics summary in footer

## Architecture

```
include/ccsparser/    — Public headers
src/
  core/               — Status, Diagnostic, types, time utilities
  event/              — NDJSON line parser, RawEvent
  profile/            — Version profiles, object decoders
  store/              — ContestStore implementation
  scoreboard/         — ScoreboardBuilder
  api/                — EventFeedParser, StreamingParseSession
  io/                 — PackageLoader, FileRefResolver
tools/                — Standalone binaries (manual tag)
tests/
  unit/               — Unit tests
  integration/        — Integration tests (per-version, package, case files)
  regression/         — Regression tests
  fixtures/           — Test data
docs/                 — API reference, architecture
```

### Key Design Principles

1. **Event layer decoupled from object layer** — NDJSON parsing produces RawEvents; version profiles decode them into typed objects
2. **Version differences isolated in profiles** — adding a new CCS version requires only a new profile, not changes to the main loop
3. **Atomic event application** — objects are fully decoded and validated before any store mutation
4. **No JSON types in public API** — public headers have zero dependency on nlohmann/json
5. **Unknown fields preserved** — extensibility without data loss

## Not Supported

- HTTP client / REST API consumption
- XML feed
- Pre-2020 `op=create/update/delete` feed format
- Resolver business logic
- UI / scoreboard rendering

## Design Tradeoffs

1. **ZIP support via system `unzip`**: Rather than bundling a C library (miniz/libzip), ZIP extraction delegates to the system `unzip` command. This keeps dependencies minimal; production deployments can ensure `unzip` is available.

2. **Rollback via replay**: Checkpoint/rollback clears state and truncates the event log. Full state restoration requires re-feeding events through the session, which provides correctness guarantees but is O(n) in events.

3. **Auto-detection heuristic**: Version auto-detection looks at `penalty_time` type and `clarification` field names. It defaults to 2023-06 when ambiguous. Explicit version specification is recommended for reliable behavior.
