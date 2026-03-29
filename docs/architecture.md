# CCSParser Architecture

## Layered Design

```
┌──────────────────────────────────────────────────┐
│                   Public API                      │
│  EventFeedParser  PackageParser  ScoreboardBuilder│
├──────────────────────────────────────────────────┤
│              ContestStore Layer                    │
│  Upsert · Delete · CollectionReplace · Singleton  │
│  Observer · Checkpoint · Rollback                 │
├──────────────────────────────────────────────────┤
│           Version Profile / Decoders              │
│  Profile2023_06   Profile2026_01                  │
│  16 object decoders + common helpers              │
├──────────────────────────────────────────────────┤
│              Event Layer (NDJSON)                  │
│  NdjsonLineParser → RawEvent                      │
│  Keepalive · Token · Shape detection              │
├──────────────────────────────────────────────────┤
│              Core Types                           │
│  Status · Diagnostic · RelativeTime · AbsoluteTime│
│  ObjectType · FileRef · ParseOptions              │
└──────────────────────────────────────────────────┘
```

## Directory Layout

```
include/ccsparser/       Public headers (no JSON types exposed)
src/
  core/                  Status, Diagnostic, types, time utilities
  event/                 NDJSON line parser
  profile/               VersionProfile + per-type decoders
    decoders/            Shared decode helpers
  store/                 ContestStore (pimpl)
  scoreboard/            ScoreboardBuilder
  api/                   EventFeedParser, StreamingParseSession
  io/                    PackageLoader, FileRefResolver
tools/                   Standalone binaries (manual tag)
tests/
  unit/                  Per-module unit tests
  integration/           Spec-version and package tests
  regression/            Edge-case regression guards
  fixtures/              Minimal test data
docs/                    Documentation
```

## Key Design Principles

### 1. Event Layer ↔ Object Layer Decoupling

The NDJSON line parser produces `RawEvent` with the raw `data_json` string.
Object decoding only happens inside a `VersionProfile`, keeping the event loop
version-agnostic.

### 2. Version Differences Isolated in Profiles

Both 2023-06 and 2026-01 share a common `ProfileImpl` parameterised by
`ApiVersion`.  Differences (e.g. `penalty_time` integer vs. RELTIME,
`to_team_id` vs. `to_team_ids`) are handled in the decoder dispatch — not
scattered across the main parse loop.  Adding a future version requires a
new profile only.

### 3. Atomic Event Application

Every event is fully decoded and validated before mutating the store:

- **Single object upsert**: decode → validate → `ApplyUpsert`.
- **Collection replace**: decode *all* elements → validate → `ApplyCollectionReplace` (swap entire map).
- **Delete**: verify type → `ApplyDelete`.
- **Singleton update**: decode → `ApplySingletonUpdate`.

If any decode step fails, the store is untouched.

### 4. No JSON in Public API

Public headers include no `nlohmann/json` types.  Unknown fields are stored as
`map<string, string>` (JSON-serialised values).  Consumers never need to link
against the JSON library.

### 5. Scoreboard Decoupled from Rendering

`ScoreboardBuilder` produces a `ScoreboardSnapshot` — a plain data model with
no rendering logic.  The HTML renderer in `tools/` is one consumer; a future
resolver or REST API can use the same snapshot.

## Data Flow

```
event-feed.ndjson
       │
       ▼
 NdjsonLineParser     ──►  RawEvent (type, id, shape, data_json)
       │
       ▼
 VersionProfile        ──►  Typed ContestObject (Contest, Team, …)
       │
       ▼
 ContestStore          ──►  upsert / delete / replace
       │                    observer notifications
       ▼
 BuildScoreboard()     ──►  ScoreboardSnapshot (rows, cells, awards)
       │
       ▼
 (consumer)            ──►  HTML / JSON / Resolver / …
```

## Error Recovery Model

Recovery always happens at the **newline boundary**.  A bad line or event is
never partially applied — it is either fully committed to the store or fully
discarded.

```
┌─ Parse line ──────────────────────────────────────┐
│ OK?  ──► Decode object(s)                         │
│          OK?  ──► Apply to store ──► Reset errors  │
│          Fail ──► Diagnostic, skip, keep old state │
│ Fail ──► Diagnostic, skip line                    │
└───────────────────────────────────────────────────┘
```

Consecutive-error counting triggers an abort (`kLimitExceeded`) when the feed
appears irreparably corrupt.

## Checkpoint / Rollback

`ContestStore::CreateCheckpoint()` captures the current event-log length.
`Rollback(cp)` truncates the log and clears all store state.  Full restoration
requires re-feeding events through the session (O(n) in events).  This is simple
and correct; a copy-on-write snapshot scheme could optimise large stores in future.
