# CCSParser Research Document

## 1. CCS 2023-06 vs 2026-01 Differences

### Event-Feed / Notification Format

Both versions share the same NDJSON event-feed format:
- `{"type":"<type>","id":"<id>","data":{...}}` — single object upsert
- `{"type":"<type>","id":"<id>","data":null}` — single object delete
- `{"type":"<type>","id":null,"data":[...]}` — collection replace
- `{"type":"<type>","id":null,"data":{...}}` — singleton update (contest, state)
- Empty line — keepalive (must be sent if no event within 120s)
- `token` field — optional, used for reconnection via `since_token`
- `end_of_updates` in state — must be the last event in the feed

No structural differences between versions in event-feed format.

### Object Schema Differences

| Area | 2023-06 | 2026-01 | Internal Normalization |
|------|---------|---------|----------------------|
| `contest.penalty_time` | `integer` (minutes) | `RELTIME` string (e.g. `"0:20:00"`) | `RelativeTime` (ms) |
| `clarifications` recipient | `to_team_id: ID?` (singular) | `to_team_ids: array of ID?`, `to_group_ids: array of ID?` | `vector<string> to_team_ids`, `vector<string> to_group_ids` |
| `awards` known IDs | standard medals/ranks | adds `honors`, `high-honors`, `highest-honors` | string (no enum restriction) |
| `contest.main_scoreboard_group_id` | not present | `ID?` | `optional<string>` |
| `teams.hidden` | `boolean?` (defaults false) | removed | `optional<bool>` (absent in 2026-01) |
| `judgement-types.simplified_judgement_type_id` | not present | `ID?` | `optional<string>` |
| `judgements.simplified_judgement_type_id` | not present | `ID?` | `optional<string>` |
| `judgements.current` | not present | `boolean?` (defaults true) | `optional<bool>` |
| `submissions.account_id` | not present | `ID?` | `optional<string>` |
| `submissions.team_id` | `ID` (required) | `ID?` (nullable) | `optional<string>` |
| `problems.memory_limit` | not present | `integer` | `optional<int>` |
| `problems.output_limit` | not present | `integer` | `optional<int>` |
| `problems.code_limit` | not present | `integer` | `optional<int>` |
| `problems.attachments` | not present | `array of FILE?` | `vector<FileRef>` |
| `organizations.country_subdivision` | not present | `string?` | `optional<string>` |
| `organizations.country_subdivision_flag` | not present | `array of FILE?` | `vector<FileRef>` |
| `FILE.tag` | not present | `array of string` | `vector<string>` |
| `scoreboard.score.total_time` | `integer` (minutes) | `RELTIME` | N/A (scoreboard not parsed) |
| Clarification capability | `team_clar` | `post_clar` | N/A (not parser concern) |
| Commentary modification | not available | `post_comment` capability | N/A |

### Contest Package Differences

| Area | 2023-06 | 2026-01 |
|------|---------|---------|
| File reference resolution | `<endpoint>/<id>/<filename>` | Same, plus default filename pattern: `<endpoint>/<id>/<property>(.<tag>)*.<extension>` |
| Default filename discovery | Not specified | Explicitly defined with tag/dimension parsing |
| `href` fallback | Stale URLs ignored, use local file | Same, more explicitly documented |

### Auto-Detection Strategy

1. If `api.json` exists in package → check `version` field
2. If `contest.penalty_time` is integer → likely 2023-06
3. If `contest.penalty_time` is string → likely 2026-01
4. If `clarifications` have `to_team_id` → 2023-06; `to_team_ids` → 2026-01
5. If `FILE` objects have `tag` property → 2026-01
6. Fallback: try 2023-06 first (more common in existing data)

## 2. icpctools Reference Implementation Capabilities

### NDJSONFeedParser.java
- Reads NDJSON line by line from `InputStream` via `BufferedReader`
- Supports both old (`op` field) and new format
- Tracks `lastId` and `lastToken`
- Handles: single object upsert, delete (data=null), collection replace (id=null, data=array), singleton update
- **Weakness**: Collection replace is NOT atomic — adds new objects one by one, then deletes missing ones. If any element fails, partial state corruption occurs.
- **Weakness**: No diagnostics/error reporting beyond Trace.trace logging
- **Weakness**: No fail-fast mode
- **Weakness**: No line number tracking
- **Weakness**: Catches exceptions per-line but no structured recovery

### Contest.java
- Central contest state store with typed arrays per object type
- Listener pattern (`IContestListener`) with `contestChanged(contest, obj, delta)` where delta ∈ {ADD, UPDATE, DELETE, NOOP}
- Modifier pattern for transforming objects before storage
- Cache invalidation on every change (aggressive — clears derived data like standings/results)
- `clone()` with optional deep copy and filtering
- `removeSince(int)` for checkpoint-like rollback (removes all objects after index N)
- Tracks known properties per type
- **Weakness**: No atomic transaction support
- **Weakness**: `removeSince` is index-based, not snapshot-based
- **Weakness**: No validation before adding objects
- **Weakness**: Linear scans for many lookups (no index beyond ID lookup)

### RESTContestSource.java (inferred from structure)
- HTTP client + event-feed consumer
- Handles reconnection with `since_token`
- Caches to disk
- Not relevant for our lib-only parser

### FileReference handling
- File references stored as structured objects with href, filename, mime, width, height
- Resolution via contest source (HTTP or disk)
- Package mode: local path resolution based on `<endpoint>/<id>/<filename>` pattern

## 3. Capabilities to Inherit

1. **Line-by-line NDJSON parsing** — proven approach, simple and robust
2. **Event format detection** — new format (type/id/data) vs old format (op field). We only support new format per requirements.
3. **Collection replace logic** — replace entire collection when id=null and data=array
4. **Singleton update** — contest and state are singletons
5. **Delete semantics** — data=null means delete; id=null + data=null means delete all in collection
6. **Token tracking** — update lastToken only on valid events
7. **Listener/observer pattern** — notify on changes
8. **Known properties tracking** — preserve unknown fields per object
9. **Object type registry** — map type string to enum/factory

## 4. Points to Refactor (Not Copy)

1. **Atomic collection replace**: icpctools adds elements one-by-one then deletes missing ones. We MUST decode+validate ALL elements first, then atomically swap the collection. If any element fails, the entire replace fails and old collection is preserved.

2. **Structured diagnostics**: icpctools only uses Trace.trace for error logging. We implement a full Diagnostic system with severity, code, line number, raw line, object type/id.

3. **Fail-fast mode**: icpctools has no concept of ErrorPolicy. We support both kContinue (skip bad records) and kFailFast (abort on first error).

4. **Validation before mutation**: icpctools adds objects to the store without validation. We validate BEFORE mutating the store — decode, validate, then atomically apply.

5. **Checkpoint/rollback**: icpctools has `removeSince(int)` which is fragile. We implement proper checkpoint/rollback by replaying from checkpoint's event log.

6. **Version-aware decoding**: icpctools doesn't separate version-specific decoding. We use VersionProfile + typed decoders so version differences are isolated.

7. **Parse limits**: icpctools has no protection against consecutive corruption. We implement max_consecutive_errors, max_diagnostics, max_line_bytes.

8. **Cache invalidation**: icpctools aggressively clears all derived caches on any change. We can be more targeted since we're a parser library, not a full contest system.

9. **Typed models**: icpctools uses generic property maps with typed getters. We use proper C++ structs with typed fields + unknown fields preservation.

## 5. Corrupted Record Processing Rules

### Classification & Treatment

| Category | Examples | Treatment |
|----------|----------|-----------|
| Line-level | Invalid JSON, truncated line, non-object root, line too long | Skip line, emit diagnostic, no store mutation |
| Event skeleton | Missing type, missing data, invalid id type, invalid token type | Discard entire event, emit diagnostic, don't update token/cursor |
| Object data | Wrong data shape, missing required fields, invalid time/reltime, invalid ID format | Atomic event failure, emit diagnostic |
| Collection replace partial | Any element in array fails decode/validate | Entire replace fails, old collection preserved |
| Singleton update | Decode/validate failure | Old singleton preserved |
| Delete unknown | Object not in store | Warning diagnostic, not fatal |
| Unknown type | Unrecognized type string | Warning + ignore (configurable) |
| Unknown field | Unrecognized property in object | Preserve + warning (configurable) |

### Recovery Boundary

- Always: newline is the recovery boundary
- Never attempt mid-line resync
- Consecutive error limit triggers abort

## 6. Test Matrix

See `docs/test_matrix.md` for full mapping.
