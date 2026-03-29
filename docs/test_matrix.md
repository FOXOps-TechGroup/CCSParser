# Test Matrix: CCSParser

Maps test cases to CCS specification clauses and icpctools reference behavior.

## Unit Tests

| Test File | Test Case | Spec Clause | icpctools Ref |
|-----------|-----------|-------------|---------------|
| time_utils_test | ParseRelTime valid formats | RELTIME: `(-)?(h)*h:mm:ss(.uuu)?` | Info.parsePenaltyTime |
| time_utils_test | ParseRelTime negative | RELTIME with leading `-` | Info.parsePenaltyTime |
| time_utils_test | ParseRelTime invalid | Reject non-conforming strings | N/A (no validation) |
| time_utils_test | ParseAbsTime ISO8601 | TIME: `yyyy-mm-ddThh:mm:ss(.uuu)?[+-]zz(:mm)?` | ContestObject.parseTimestamp |
| time_utils_test | ParseAbsTime with Z timezone | TIME with `Z` suffix | ContestObject.parseTimestamp |
| time_utils_test | RelTimeFromMinutes (2023-06 compat) | contest.penalty_time as integer | Info.getPenaltyTime |
| time_utils_test | RelTimeToString | RELTIME formatting | RelativeTime.format |
| ndjson_line_parser_test | Parse single object event | Notification format: `{"type","id","data":{}}` | NDJSONFeedParser.parseNewFormat |
| ndjson_line_parser_test | Parse delete event | `data: null` → deletion | NDJSONFeedParser.parseNewFormat |
| ndjson_line_parser_test | Parse collection replace | `id: null, data: [...]` | NDJSONFeedParser.parseNewFormat |
| ndjson_line_parser_test | Parse singleton update | `id: null, data: {}` (contest/state) | NDJSONFeedParser.parseNewFormat |
| ndjson_line_parser_test | Parse keepalive (empty line) | "newline must be sent if no event within 120 seconds" | NDJSONFeedParser.parse (skip empty) |
| ndjson_line_parser_test | Parse with token | `token` field tracking | NDJSONFeedParser.lastToken |
| ndjson_line_parser_test | Malformed JSON | Error handling | NDJSONFeedParser catch block |
| ndjson_line_parser_test | Missing type field | Event skeleton validation | N/A (no validation in icpctools) |
| ndjson_line_parser_test | Missing data field | Event skeleton validation | N/A |
| profile_2023_06_decoder_test | Decode contest with integer penalty_time | 2023-06 contest.penalty_time: `integer` | Info.add("penalty_time") |
| profile_2023_06_decoder_test | Decode clarification with to_team_id | 2023-06 clarification.to_team_id: `ID?` | Clarification.add("to_team_id") |
| profile_2023_06_decoder_test | Decode all object types | All 2023-06 endpoint schemas | ContestObject subclasses |
| profile_2026_01_decoder_test | Decode contest with RELTIME penalty_time | 2026-01 contest.penalty_time: `RELTIME` | N/A (2026-01 not in icpctools yet) |
| profile_2026_01_decoder_test | Decode clarification with to_team_ids/to_group_ids | 2026-01 clarification fields | N/A |
| profile_2026_01_decoder_test | Decode awards with honors variants | 2026-01 known awards | N/A |
| profile_2026_01_decoder_test | Decode judgement with current/simplified fields | 2026-01 judgement additions | N/A |
| profile_2026_01_decoder_test | Decode problems with memory/output/code limits | 2026-01 problem additions | N/A |
| profile_2026_01_decoder_test | Decode FILE with tag property | 2026-01 FILE.tag | N/A |
| contest_store_test | Upsert object | Event application: id != null, data is object | Contest.add (Delta.ADD/UPDATE) |
| contest_store_test | Delete object | Event application: id != null, data == null | Contest.add(Deletion) |
| contest_store_test | Delete unknown object warns | Spec: no guarantee on order | N/A (icpctools silently succeeds) |
| contest_store_test | Collection replace atomic | id == null, data is array → atomic replace | NDJSONFeedParser (non-atomic in icpctools) |
| contest_store_test | Singleton update | id == null, data is object (contest/state) | NDJSONFeedParser.parseNewFormat |
| contest_store_test | Duplicate event idempotent | "no guarantee on ... duplicate events" | Contest.add (Delta.UPDATE) |
| contest_store_test | Token only updates on valid event | Spec: token tracking | NDJSONFeedParser.lastToken |
| contest_store_test | Checkpoint and rollback | Requirement: checkpoint/rollback | Contest.removeSince |
| contest_store_test | Observer notifications | Listener pattern | IContestListener |
| diagnostics_test | Diagnostic creation with all fields | Custom requirement | N/A |
| diagnostics_test | Diagnostic codes coverage | Custom requirement | N/A |
| package_loader_test | Load from directory | Contest package: directory form | DiskContestSource |
| package_loader_test | Load from zip | Contest package: ZIP form | N/A |
| package_loader_test | Discover event-feed.ndjson | Package spec: `event-feed.ndjson` | DiskContestSource |
| file_ref_resolver_test | Resolve by endpoint/id/filename | Package spec: `<endpoint>/<id>/<filename>` | FileReference resolution |
| file_ref_resolver_test | Missing resource warns | Custom requirement | N/A |

## Integration Tests: spec_2023_06

| Test Case | Spec Clause | Description |
|-----------|-------------|-------------|
| basic_object_flow | All endpoint types | Parse a complete feed with all object types |
| clarification_to_team_id | 2023-06 clarification.to_team_id | Verify singular to_team_id normalized to to_team_ids |
| penalty_time_integer | 2023-06 contest.penalty_time: integer | Verify integer penalty_time normalized to RelativeTime |
| token_tracking | Notification format: token field | Verify token is tracked and accessible |
| keepalive_handling | Event-feed: empty line keepalive | Verify empty lines are skipped |
| end_of_updates | state.end_of_updates semantics | Verify end_of_updates is recognized |
| duplicate_event | "no guarantee on ... duplicate events" | Verify idempotent handling |
| unknown_field_preserve | Extensibility: accept unknown properties | Verify unknown fields preserved |
| malformed_line_continue | Error handling: kContinue mode | Verify bad lines skipped, parsing continues |
| malformed_line_failfast | Error handling: kFailFast mode | Verify parsing stops on first error |
| collection_replace | id=null, data=array | Verify atomic collection replacement |
| singleton_update | id=null, data=object for contest/state | Verify singleton update |

## Integration Tests: spec_2026_01

| Test Case | Spec Clause | Description |
|-----------|-------------|-------------|
| clarification_to_team_ids | 2026-01 clarification.to_team_ids/to_group_ids | Verify array recipient fields |
| penalty_time_reltime | 2026-01 contest.penalty_time: RELTIME | Verify RELTIME penalty_time |
| honors_awards | 2026-01 awards: honors/high-honors/highest-honors | Verify new award IDs accepted |
| commentary_compat | 2026-01 commentary endpoint | Verify commentary parsing |
| token_tracking | Same as 2023-06 | Token tracking |
| keepalive_handling | Same as 2023-06 | Keepalive handling |
| end_of_updates | Same as 2023-06 | end_of_updates |
| duplicate_event | Same as 2023-06 | Duplicate handling |
| unknown_field_preserve | Same as 2023-06 | Unknown field preservation |
| malformed_line_continue | Same as 2023-06 | Continue mode |
| malformed_line_failfast | Same as 2023-06 | Fail-fast mode |
| collection_replace | Same as 2023-06 | Collection replace |
| singleton_update | Same as 2023-06 | Singleton update |

## Package Tests (v1.1)

| Test Case | Spec Clause | Description |
|-----------|-------------|-------------|
| package_dir_with_event_feed | Package: event-feed.ndjson | Load from directory with event-feed |
| package_zip_with_event_feed | Package: ZIP form | Load from zip with event-feed |
| file_ref_resolution | Package: file references | Resolve file references to local paths |
| missing_resource_diagnostic | Custom requirement | Warning when referenced file missing |

## Regression Tests

| Test Case | What it guards against |
|-----------|----------------------|
| out_of_order_events_final_state_correct | Events may arrive in any order — final state must be correct |
| delete_unknown_object_warns | Deleting non-existent object should warn, not error |
| collection_replace_deletes_missing_objects | Objects not in replacement array must be removed |
| explicit_version_rejects_wrong_shape | Explicit version should reject incompatible data |
| auto_detect_conflict_reports_diagnostic | Auto-detect encountering conflicting signals |
| rollback_restores_previous_state | Checkpoint+rollback must restore exact prior state |
| observer_receives_expected_sequence | Observer must see events in correct order |
| malformed_record_does_not_mutate_store | Bad record must not change any store state |
| invalid_collection_replace_keeps_old_collection | Failed replace preserves old collection |
| consecutive_corruption_limit_triggers | max_consecutive_errors triggers abort |
| line_too_long_is_handled | Oversized lines are rejected with diagnostic |
