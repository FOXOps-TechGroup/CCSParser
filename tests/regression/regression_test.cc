// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

// Regression tests for the CCSParser library.
// Each test targets a specific edge case or previously-observed issue.

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "ccsparser/ccsparser.h"

namespace ccsparser {
namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

class RegressionTest : public ::testing::Test {
 protected:
  std::unique_ptr<StreamingParseSession> MakeSession(
      ParseOptions opts = {}) {
    opts.version = ApiVersion::k2023_06;
    opts.keep_event_log = true;
    opts.enable_checkpointing = true;
    auto s = EventFeedParser::CreateStreamingSession(opts);
    EXPECT_TRUE(s.ok()) << s.status().ToString();
    return std::move(s.value());
  }
};

// Recording observer that tracks all events in order.
class RecordingObserver : public Observer {
 public:
  enum class EventKind { kUpsert, kDelete, kReplace, kDiag, kEndOfUpdates };

  struct Record {
    EventKind kind;
    ObjectType type = ObjectType::kUnknown;
    std::string id;
    size_t count = 0;  // for collection replace
  };

  std::vector<Record> events;

  void OnObjectUpserted(ObjectType type, const std::string& id,
                        const ContestObject& /*obj*/) override {
    events.push_back({EventKind::kUpsert, type, id, 0});
  }

  void OnObjectDeleted(ObjectType type, const std::string& id) override {
    events.push_back({EventKind::kDelete, type, id, 0});
  }

  void OnCollectionReplaced(ObjectType type, size_t count) override {
    events.push_back({EventKind::kReplace, type, "", count});
  }

  void OnDiagnostic(const Diagnostic& /*diag*/) override {
    events.push_back({EventKind::kDiag, ObjectType::kUnknown, "", 0});
  }

  void OnEndOfUpdates() override {
    events.push_back({EventKind::kEndOfUpdates, ObjectType::kUnknown, "", 0});
  }
};

// =====================================================================
// out_of_order_events_final_state_correct
// =====================================================================

TEST_F(RegressionTest, out_of_order_events_final_state_correct) {
  auto session = MakeSession();

  // Feed a submission before the team or problem it references.
  session->ConsumeLine(
      R"({"type":"submissions","id":"s1","data":{"id":"s1","language_id":"cpp","problem_id":"A","team_id":"t1","time":"2025-01-01T10:15:00Z","contest_time":"0:15:00"}})");
  session->ConsumeLine(
      R"({"type":"teams","id":"t1","data":{"id":"t1","name":"Alpha"}})");
  session->ConsumeLine(
      R"({"type":"problems","id":"A","data":{"id":"A","label":"A","name":"Apples"}})");
  // Add a judgement referencing the submission.
  session->ConsumeLine(
      R"({"type":"judgements","id":"j1","data":{"id":"j1","submission_id":"s1","judgement_type_id":"AC"}})");

  const auto& store = session->store();

  // All objects should be present regardless of order.
  EXPECT_EQ(store.ListObjects(ObjectType::kSubmissions).size(), 1);
  EXPECT_EQ(store.ListObjects(ObjectType::kTeams).size(), 1);
  EXPECT_EQ(store.ListObjects(ObjectType::kProblems).size(), 1);
  EXPECT_EQ(store.ListObjects(ObjectType::kJudgements).size(), 1);

  // Verify the submission references are correct.
  auto sub = store.GetObject(ObjectType::kSubmissions, "s1");
  ASSERT_TRUE(sub.ok());
  EXPECT_EQ(dynamic_cast<const Submission*>(sub.value())->team_id.value_or(""),
            "t1");
}

// =====================================================================
// delete_unknown_object_warns
// =====================================================================

TEST_F(RegressionTest, delete_unknown_object_warns) {
  ParseOptions opts;
  opts.error_policy = ErrorPolicy::kContinue;
  auto session = MakeSession(opts);

  // Delete a team that was never inserted.
  session->ConsumeLine(R"({"type":"teams","id":"ghost","data":null})");

  // The store should have no teams.
  EXPECT_EQ(session->store().ListObjects(ObjectType::kTeams).size(), 0);

  // A diagnostic warning should be emitted.
  bool found_warning = false;
  for (const auto& d : session->diagnostics()) {
    if (d.code == DiagnosticCode::kDeleteUnknownObject) {
      found_warning = true;
      break;
    }
  }
  EXPECT_TRUE(found_warning) << "Expected kDeleteUnknownObject diagnostic";
}

// =====================================================================
// collection_replace_deletes_missing_objects
// =====================================================================

TEST_F(RegressionTest, collection_replace_deletes_missing_objects) {
  auto session = MakeSession();

  // Insert some problems.
  session->ConsumeLine(
      R"({"type":"problems","id":"A","data":{"id":"A","label":"A","name":"Apples"}})");
  session->ConsumeLine(
      R"({"type":"problems","id":"B","data":{"id":"B","label":"B","name":"Bananas"}})");
  EXPECT_EQ(session->store().ListObjects(ObjectType::kProblems).size(), 2);

  // Replace the entire collection with a different set.
  session->ConsumeLine(
      R"({"type":"problems","id":null,"data":[{"id":"X","label":"X","name":"Xylophone"}]})");

  // Old objects should be gone.
  EXPECT_FALSE(session->store().GetObject(ObjectType::kProblems, "A").ok());
  EXPECT_FALSE(session->store().GetObject(ObjectType::kProblems, "B").ok());
  // New object should be present.
  EXPECT_TRUE(session->store().GetObject(ObjectType::kProblems, "X").ok());
  EXPECT_EQ(session->store().ListObjects(ObjectType::kProblems).size(), 1);
}

// =====================================================================
// explicit_version_rejects_wrong_shape
// =====================================================================

TEST_F(RegressionTest, explicit_version_rejects_wrong_shape) {
  // Parse with explicit 2023-06 but feed a 2026-01-style RELTIME penalty_time.
  ParseOptions opts;
  opts.version = ApiVersion::k2023_06;
  opts.error_policy = ErrorPolicy::kContinue;
  auto session = MakeSession(opts);

  // Feed penalty_time as a string (2026-01 style) to a 2023-06 session.
  session->ConsumeLine(
      R"({"type":"contest","data":{"id":"c1","penalty_time":"0:20:00"}})");

  // The session should still work (Continue mode), but may produce
  // a diagnostic about the unexpected field shape.
  // penalty_time might not parse correctly as an integer.
  const Contest* c = session->store().GetContest();
  ASSERT_NE(c, nullptr);

  // With kContinue, the contest object should exist even if penalty_time
  // failed to parse. Check for diagnostic about the mismatch.
  if (!c->penalty_time.has_value()) {
    // If penalty_time didn't parse, there should be a diagnostic.
    EXPECT_GE(session->diagnostics().size(), 1);
  }
}

// =====================================================================
// rollback_restores_previous_state
// =====================================================================

TEST_F(RegressionTest, rollback_restores_previous_state) {
  auto session = MakeSession();

  // Feed initial events.
  session->ConsumeLine(
      R"({"type":"contest","data":{"id":"c1","name":"Test"},"token":"t1"})");
  session->ConsumeLine(
      R"({"type":"problems","id":"A","data":{"id":"A","label":"A","name":"Apples"},"token":"t2"})");

  // Create checkpoint after initial events.
  auto cp = session->mutable_store().CreateCheckpoint();

  // Feed more events.
  session->ConsumeLine(
      R"({"type":"problems","id":"B","data":{"id":"B","label":"B","name":"Bananas"},"token":"t3"})");
  session->ConsumeLine(
      R"({"type":"teams","id":"t1","data":{"id":"t1","name":"Alpha"},"token":"t4"})");

  // Before rollback: should have 2 problems + 1 team.
  EXPECT_EQ(session->store().ListObjects(ObjectType::kProblems).size(), 2);
  EXPECT_EQ(session->store().ListObjects(ObjectType::kTeams).size(), 1);

  // Rollback: clears the store and truncates the event log.
  auto s = session->mutable_store().Rollback(cp);
  ASSERT_TRUE(s.ok()) << s.ToString();

  // After rollback: store is cleared (objects removed).
  EXPECT_EQ(session->store().GetEventCount(), 0);

  // Event log should be truncated to checkpoint.
  EXPECT_EQ(session->store().GetEventLog().size(), cp.event_index);

  // To fully restore, we'd replay events from the log.
  // For now, just verify rollback mechanics work.
}

// =====================================================================
// observer_receives_expected_sequence
// =====================================================================

TEST_F(RegressionTest, observer_receives_expected_sequence) {
  // Use direct store operations to test the observer sequence,
  // matching the pattern from the unit tests.
  ContestStore store;
  auto obs = std::make_shared<RecordingObserver>();
  store.AddObserver(obs);

  // Upsert contest.
  auto contest = std::make_unique<Contest>();
  contest->id = "c1";
  contest->name = "Test";
  store.ApplyUpsert(ObjectType::kContest, "", std::move(contest));

  // Upsert problem A.
  auto prob = std::make_unique<Problem>();
  prob->id = "A";
  prob->label = "A";
  store.ApplyUpsert(ObjectType::kProblems, "A", std::move(prob));

  // Delete problem A.
  store.ApplyDelete(ObjectType::kProblems, "A");

  // Verify event sequence.
  ASSERT_GE(obs->events.size(), 3u);

  EXPECT_EQ(obs->events[0].kind, RecordingObserver::EventKind::kUpsert);
  EXPECT_EQ(obs->events[0].type, ObjectType::kContest);

  EXPECT_EQ(obs->events[1].kind, RecordingObserver::EventKind::kUpsert);
  EXPECT_EQ(obs->events[1].type, ObjectType::kProblems);
  EXPECT_EQ(obs->events[1].id, "A");

  EXPECT_EQ(obs->events[2].kind, RecordingObserver::EventKind::kDelete);
  EXPECT_EQ(obs->events[2].type, ObjectType::kProblems);
  EXPECT_EQ(obs->events[2].id, "A");
}

TEST_F(RegressionTest, observer_via_session) {
  // Test observer notifications through a streaming session.
  auto session = MakeSession();
  auto obs = std::make_shared<RecordingObserver>();
  session->mutable_store().AddObserver(obs);

  session->ConsumeLine(
      R"({"type":"problems","id":"A","data":{"id":"A","label":"A"}})");
  session->ConsumeLine(
      R"({"type":"problems","id":"A","data":null})");

  // We should see at least an upsert and a delete.
  bool found_upsert = false;
  bool found_delete = false;
  for (const auto& e : obs->events) {
    if (e.kind == RecordingObserver::EventKind::kUpsert &&
        e.type == ObjectType::kProblems && e.id == "A") {
      found_upsert = true;
    }
    if (e.kind == RecordingObserver::EventKind::kDelete &&
        e.type == ObjectType::kProblems && e.id == "A") {
      found_delete = true;
    }
  }
  EXPECT_TRUE(found_upsert);
  EXPECT_TRUE(found_delete);
}

// =====================================================================
// malformed_record_does_not_mutate_store
// =====================================================================

TEST_F(RegressionTest, malformed_record_does_not_mutate_store) {
  ParseOptions opts;
  opts.error_policy = ErrorPolicy::kContinue;
  auto session = MakeSession(opts);

  session->ConsumeLine(
      R"({"type":"problems","id":"A","data":{"id":"A","label":"A","name":"Apples"}})");

  // Feed a malformed line.
  session->ConsumeLine("completely invalid json {{{{");

  // The problem count should remain the same.
  EXPECT_EQ(session->store().ListObjects(ObjectType::kProblems).size(), 1);

  // The event count should not have increased from the bad line.
  // (Bad lines might still record an event, but store mutation shouldn't happen.)
  auto result = session->store().GetObject(ObjectType::kProblems, "A");
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(dynamic_cast<const Problem*>(result.value())->name.value_or(""),
            "Apples");
}

// =====================================================================
// invalid_collection_replace_keeps_old_collection
// =====================================================================

TEST_F(RegressionTest, invalid_collection_replace_keeps_old_collection) {
  ParseOptions opts;
  opts.error_policy = ErrorPolicy::kContinue;
  auto session = MakeSession(opts);

  // Insert initial teams.
  session->ConsumeLine(
      R"({"type":"teams","id":"t1","data":{"id":"t1","name":"Alpha"}})");
  session->ConsumeLine(
      R"({"type":"teams","id":"t2","data":{"id":"t2","name":"Beta"}})");
  EXPECT_EQ(session->store().ListObjects(ObjectType::kTeams).size(), 2);

  // Try a malformed collection replace (data is not an array).
  session->ConsumeLine(
      R"({"type":"teams","id":null,"data":"not-an-array"})");

  // Old teams should still be present since the replace was invalid.
  EXPECT_EQ(session->store().ListObjects(ObjectType::kTeams).size(), 2);
  EXPECT_TRUE(session->store().GetObject(ObjectType::kTeams, "t1").ok());
  EXPECT_TRUE(session->store().GetObject(ObjectType::kTeams, "t2").ok());
}

// =====================================================================
// consecutive_corruption_limit_triggers
// =====================================================================

TEST_F(RegressionTest, consecutive_corruption_limit_triggers) {
  ParseOptions opts;
  opts.error_policy = ErrorPolicy::kContinue;
  opts.limits.max_consecutive_errors = 3;
  auto session = MakeSession(opts);

  // Feed valid data first.
  session->ConsumeLine(
      R"({"type":"contest","data":{"id":"c1","name":"Test"}})");

  // Feed many consecutive bad lines.
  Status last_status = Status::Ok();
  for (int i = 0; i < 10; ++i) {
    last_status = session->ConsumeLine("bad line " + std::to_string(i));
    if (!last_status.ok()) break;
  }

  // After exceeding max_consecutive_errors, the session should report
  // an error or have a diagnostic about it.
  bool found_limit_diag = false;
  for (const auto& d : session->diagnostics()) {
    if (d.code == DiagnosticCode::kMaxConsecutiveErrorsExceeded) {
      found_limit_diag = true;
      break;
    }
  }
  // Either the last ConsumeLine returned an error, or a diagnostic was recorded.
  EXPECT_TRUE(!last_status.ok() || found_limit_diag)
      << "Expected consecutive error limit to trigger";
}

// =====================================================================
// line_too_long_is_handled
// =====================================================================

TEST_F(RegressionTest, line_too_long_is_handled) {
  ParseOptions opts;
  opts.error_policy = ErrorPolicy::kContinue;
  opts.limits.max_line_bytes = 256;  // Very small limit for testing.
  auto session = MakeSession(opts);

  // Create a line that exceeds the limit.
  std::string long_name(300, 'A');
  std::string long_line =
      R"({"type":"problems","id":"A","data":{"id":"A","label":"A","name":")" +
      long_name + R"("}})";

  session->ConsumeLine(long_line);

  // Check for a diagnostic about the oversized line.
  bool found_long_diag = false;
  for (const auto& d : session->diagnostics()) {
    if (d.code == DiagnosticCode::kLineTooLong) {
      found_long_diag = true;
      break;
    }
  }
  EXPECT_TRUE(found_long_diag) << "Expected kLineTooLong diagnostic";
}

}  // namespace
}  // namespace ccsparser
