// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

// Tests for ContestStore operations: upsert, delete, collection replace,
// query, observer notifications, and checkpoint/rollback.

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "ccsparser/contest_store.h"
#include "ccsparser/diagnostic.h"
#include "ccsparser/objects.h"
#include "ccsparser/observer.h"
#include "ccsparser/parse_options.h"
#include "ccsparser/parser.h"
#include "ccsparser/status.h"
#include "ccsparser/types.h"

namespace ccsparser {
namespace {

// Helper: create a session and return it.
std::unique_ptr<StreamingParseSession> MakeSession() {
  ParseOptions opts;
  opts.version = ApiVersion::k2023_06;
  opts.error_policy = ErrorPolicy::kContinue;
  opts.keep_event_log = true;
  auto s = EventFeedParser::CreateStreamingSession(opts);
  EXPECT_TRUE(s.ok());
  return std::move(s.value());
}

// Recording observer for testing.
class RecordingObserver : public Observer {
 public:
  struct UpsertRecord {
    ObjectType type;
    std::string id;
  };
  struct DeleteRecord {
    ObjectType type;
    std::string id;
  };
  struct ReplaceRecord {
    ObjectType type;
    size_t count;
  };

  std::vector<UpsertRecord> upserts;
  std::vector<DeleteRecord> deletes;
  std::vector<ReplaceRecord> replaces;
  std::vector<Diagnostic> observed_diagnostics;
  int end_of_updates_count = 0;

  void OnObjectUpserted(ObjectType type, const std::string& id,
                        const ContestObject& /*obj*/) override {
    upserts.push_back({type, id});
  }

  void OnObjectDeleted(ObjectType type, const std::string& id) override {
    deletes.push_back({type, id});
  }

  void OnCollectionReplaced(ObjectType type, size_t count) override {
    replaces.push_back({type, count});
  }

  void OnDiagnostic(const Diagnostic& diag) override {
    observed_diagnostics.push_back(diag);
  }

  void OnEndOfUpdates() override { end_of_updates_count++; }
};

// =====================================================================
// Direct store operations
// =====================================================================

TEST(ContestStoreTest, UpsertAndRetrieveProblem) {
  ContestStore store;
  auto prob = std::make_unique<Problem>();
  prob->id = "A";
  prob->label = "A";
  prob->name = "Apples";

  auto s = store.ApplyUpsert(ObjectType::kProblems, "A", std::move(prob));
  ASSERT_TRUE(s.ok()) << s.ToString();

  auto result = store.GetObject(ObjectType::kProblems, "A");
  ASSERT_TRUE(result.ok()) << result.status().ToString();
  const auto* obj = dynamic_cast<const Problem*>(result.value());
  ASSERT_NE(obj, nullptr);
  EXPECT_EQ(obj->name.value_or(""), "Apples");
}

TEST(ContestStoreTest, UpsertOverwritesExistingObject) {
  ContestStore store;
  auto prob1 = std::make_unique<Problem>();
  prob1->id = "A";
  prob1->name = "Apples";
  store.ApplyUpsert(ObjectType::kProblems, "A", std::move(prob1));

  auto prob2 = std::make_unique<Problem>();
  prob2->id = "A";
  prob2->name = "Avocados";
  store.ApplyUpsert(ObjectType::kProblems, "A", std::move(prob2));

  auto result = store.GetObject(ObjectType::kProblems, "A");
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(dynamic_cast<const Problem*>(result.value())->name.value_or(""),
            "Avocados");
}

TEST(ContestStoreTest, DeleteRemovesObject) {
  ContestStore store;
  auto prob = std::make_unique<Problem>();
  prob->id = "A";
  store.ApplyUpsert(ObjectType::kProblems, "A", std::move(prob));

  auto s = store.ApplyDelete(ObjectType::kProblems, "A");
  ASSERT_TRUE(s.ok());

  auto result = store.GetObject(ObjectType::kProblems, "A");
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), StatusCode::kNotFound);
}

TEST(ContestStoreTest, DeleteNonexistentIsNotError) {
  ContestStore store;
  auto s = store.ApplyDelete(ObjectType::kProblems, "missing");
  EXPECT_TRUE(s.ok());
}

TEST(ContestStoreTest, ListObjectsEmpty) {
  ContestStore store;
  auto objs = store.ListObjects(ObjectType::kProblems);
  EXPECT_TRUE(objs.empty());
}

TEST(ContestStoreTest, ListObjectsMultiple) {
  ContestStore store;
  auto p1 = std::make_unique<Problem>();
  p1->id = "A";
  store.ApplyUpsert(ObjectType::kProblems, "A", std::move(p1));

  auto p2 = std::make_unique<Problem>();
  p2->id = "B";
  store.ApplyUpsert(ObjectType::kProblems, "B", std::move(p2));

  auto objs = store.ListObjects(ObjectType::kProblems);
  EXPECT_EQ(objs.size(), 2);
}

TEST(ContestStoreTest, GetObjectNotFound) {
  ContestStore store;
  auto result = store.GetObject(ObjectType::kProblems, "missing");
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), StatusCode::kNotFound);
}

// =====================================================================
// Singleton operations (contest, state)
// =====================================================================

TEST(ContestStoreTest, SingletonContestUpsert) {
  ContestStore store;
  auto contest = std::make_unique<Contest>();
  contest->name = "World Finals";
  store.ApplyUpsert(ObjectType::kContest, "", std::move(contest));

  ASSERT_NE(store.GetContest(), nullptr);
  EXPECT_EQ(store.GetContest()->name.value_or(""), "World Finals");
}

TEST(ContestStoreTest, SingletonStateUpsert) {
  ContestStore store;
  auto state = std::make_unique<State>();
  state->started = AbsoluteTime{1713099600000LL, 0};
  store.ApplyUpsert(ObjectType::kState, "", std::move(state));

  ASSERT_NE(store.GetState(), nullptr);
  ASSERT_TRUE(store.GetState()->started.has_value());
}

TEST(ContestStoreTest, GetContestNullWhenEmpty) {
  ContestStore store;
  EXPECT_EQ(store.GetContest(), nullptr);
}

TEST(ContestStoreTest, GetStateNullWhenEmpty) {
  ContestStore store;
  EXPECT_EQ(store.GetState(), nullptr);
}

// =====================================================================
// Collection replace
// =====================================================================

TEST(ContestStoreTest, CollectionReplaceAtomic) {
  ContestStore store;
  // Insert initial objects.
  auto p1 = std::make_unique<Problem>();
  p1->id = "A";
  p1->name = "Apples";
  store.ApplyUpsert(ObjectType::kProblems, "A", std::move(p1));

  // Replace entire collection.
  std::vector<std::unique_ptr<ContestObject>> new_problems;
  auto p2 = std::make_unique<Problem>();
  p2->id = "X";
  p2->name = "Xylophone";
  new_problems.push_back(std::move(p2));
  auto p3 = std::make_unique<Problem>();
  p3->id = "Y";
  p3->name = "Yak";
  new_problems.push_back(std::move(p3));

  auto s = store.ApplyCollectionReplace(ObjectType::kProblems,
                                         std::move(new_problems));
  ASSERT_TRUE(s.ok());

  auto objs = store.ListObjects(ObjectType::kProblems);
  EXPECT_EQ(objs.size(), 2);
  // Original "A" should be gone.
  auto result = store.GetObject(ObjectType::kProblems, "A");
  EXPECT_FALSE(result.ok());
}

// =====================================================================
// Event counting
// =====================================================================

TEST(ContestStoreTest, EventCountIncrements) {
  ContestStore store;
  EXPECT_EQ(store.GetEventCount(), 0);

  auto prob = std::make_unique<Problem>();
  prob->id = "A";
  store.ApplyUpsert(ObjectType::kProblems, "A", std::move(prob));
  EXPECT_EQ(store.GetEventCount(), 1);

  store.ApplyDelete(ObjectType::kProblems, "A");
  EXPECT_EQ(store.GetEventCount(), 2);
}

// =====================================================================
// Observer notifications
// =====================================================================

TEST(ContestStoreTest, ObserverNotifiedOnUpsert) {
  ContestStore store;
  auto obs = std::make_shared<RecordingObserver>();
  store.AddObserver(obs);

  auto prob = std::make_unique<Problem>();
  prob->id = "A";
  store.ApplyUpsert(ObjectType::kProblems, "A", std::move(prob));

  ASSERT_EQ(obs->upserts.size(), 1);
  EXPECT_EQ(obs->upserts[0].type, ObjectType::kProblems);
  EXPECT_EQ(obs->upserts[0].id, "A");
}

TEST(ContestStoreTest, ObserverNotifiedOnDelete) {
  ContestStore store;
  auto obs = std::make_shared<RecordingObserver>();
  store.AddObserver(obs);

  auto prob = std::make_unique<Problem>();
  prob->id = "A";
  store.ApplyUpsert(ObjectType::kProblems, "A", std::move(prob));
  store.ApplyDelete(ObjectType::kProblems, "A");

  ASSERT_EQ(obs->deletes.size(), 1);
  EXPECT_EQ(obs->deletes[0].type, ObjectType::kProblems);
  EXPECT_EQ(obs->deletes[0].id, "A");
}

TEST(ContestStoreTest, ObserverNotifiedOnCollectionReplace) {
  ContestStore store;
  auto obs = std::make_shared<RecordingObserver>();
  store.AddObserver(obs);

  std::vector<std::unique_ptr<ContestObject>> problems;
  auto p1 = std::make_unique<Problem>();
  p1->id = "A";
  problems.push_back(std::move(p1));
  auto p2 = std::make_unique<Problem>();
  p2->id = "B";
  problems.push_back(std::move(p2));

  store.ApplyCollectionReplace(ObjectType::kProblems, std::move(problems));

  ASSERT_EQ(obs->replaces.size(), 1);
  EXPECT_EQ(obs->replaces[0].type, ObjectType::kProblems);
  EXPECT_EQ(obs->replaces[0].count, 2);
}

TEST(ContestStoreTest, ObserverNotifiedOnDiagnostic) {
  ContestStore store;
  auto obs = std::make_shared<RecordingObserver>();
  store.AddObserver(obs);

  Diagnostic diag;
  diag.severity = Severity::kWarning;
  diag.code = DiagnosticCode::kUnknownField;
  diag.message = "test warning";
  store.NotifyDiagnostic(diag);

  ASSERT_EQ(obs->observed_diagnostics.size(), 1);
  EXPECT_EQ(obs->observed_diagnostics[0].message, "test warning");
}

// =====================================================================
// Event log
// =====================================================================

TEST(ContestStoreTest, EventLogRecording) {
  ContestStore store;
  RawEvent event;
  event.type_str = "problems";
  event.object_type = ObjectType::kProblems;
  event.id = "A";
  event.shape = EventShape::kSingleObject;
  event.line_no = 1;
  store.RecordEvent(event);

  const auto& log = store.GetEventLog();
  ASSERT_EQ(log.size(), 1);
  EXPECT_EQ(log[0].type_str, "problems");
  EXPECT_EQ(log[0].id.value_or(""), "A");
}

// =====================================================================
// Checkpoint/rollback
// =====================================================================

TEST(ContestStoreTest, CheckpointRollback) {
  ContestStore store;

  auto p1 = std::make_unique<Problem>();
  p1->id = "A";
  store.ApplyUpsert(ObjectType::kProblems, "A", std::move(p1));

  RawEvent ev1;
  ev1.type_str = "problems";
  ev1.object_type = ObjectType::kProblems;
  ev1.id = "A";
  ev1.line_no = 1;
  store.RecordEvent(ev1);

  auto cp = store.CreateCheckpoint();

  auto p2 = std::make_unique<Problem>();
  p2->id = "B";
  store.ApplyUpsert(ObjectType::kProblems, "B", std::move(p2));

  RawEvent ev2;
  ev2.type_str = "problems";
  ev2.object_type = ObjectType::kProblems;
  ev2.id = "B";
  ev2.line_no = 2;
  store.RecordEvent(ev2);

  // Before rollback: 2 problems, 2 events in log.
  EXPECT_EQ(store.ListObjects(ObjectType::kProblems).size(), 2);
  EXPECT_EQ(store.GetEventLog().size(), 2);

  auto s = store.Rollback(cp);
  ASSERT_TRUE(s.ok()) << s.ToString();

  // After rollback: state is cleared (rollback resets and stores truncated log).
  // The event log is truncated to the checkpoint.
  EXPECT_EQ(store.GetEventLog().size(), 1);
  // Objects are cleared on rollback (caller replays from log).
  EXPECT_EQ(store.GetEventCount(), 0);
}

TEST(ContestStoreTest, CheckpointInvalidRollback) {
  ContestStore store;
  Checkpoint bad{999};
  auto s = store.Rollback(bad);
  EXPECT_FALSE(s.ok());
  EXPECT_EQ(s.code(), StatusCode::kInvalidArgument);
}

// =====================================================================
// Via StreamingParseSession (integration-style)
// =====================================================================

TEST(ContestStoreTest, SessionPopulatesStore) {
  auto session = MakeSession();
  session->ConsumeLine(
      R"({"type":"contest","data":{"id":"c1","name":"WF 2024"}})");
  session->ConsumeLine(
      R"({"type":"languages","id":"cpp","data":{"id":"cpp","name":"C++","extensions":["cpp","cc"]}})");
  session->ConsumeLine(
      R"({"type":"problems","id":"A","data":{"id":"A","ordinal":1,"label":"A","name":"Apples"}})");
  session->ConsumeLine(
      R"({"type":"problems","id":"B","data":{"id":"B","ordinal":2,"label":"B","name":"Bananas"}})");

  const auto& store = session->store();
  ASSERT_NE(store.GetContest(), nullptr);
  EXPECT_EQ(store.GetContest()->name.value_or(""), "WF 2024");

  auto langs = store.ListObjects(ObjectType::kLanguages);
  EXPECT_EQ(langs.size(), 1);

  auto probs = store.ListObjects(ObjectType::kProblems);
  EXPECT_EQ(probs.size(), 2);

  EXPECT_EQ(store.GetEventCount(), 4);
}

TEST(ContestStoreTest, SessionDeleteThenList) {
  auto session = MakeSession();
  session->ConsumeLine(
      R"({"type":"teams","id":"t1","data":{"id":"t1","name":"Alpha"}})");
  session->ConsumeLine(
      R"({"type":"teams","id":"t2","data":{"id":"t2","name":"Beta"}})");
  EXPECT_EQ(session->store().ListObjects(ObjectType::kTeams).size(), 2);

  session->ConsumeLine(R"({"type":"teams","id":"t1","data":null})");
  EXPECT_EQ(session->store().ListObjects(ObjectType::kTeams).size(), 1);

  auto remaining = session->store().GetObject(ObjectType::kTeams, "t2");
  ASSERT_TRUE(remaining.ok());
  EXPECT_EQ(dynamic_cast<const Team*>(remaining.value())->name.value_or(""),
            "Beta");
}

TEST(ContestStoreTest, SessionObserverIntegration) {
  auto session = MakeSession();
  auto obs = std::make_shared<RecordingObserver>();
  session->mutable_store().AddObserver(obs);

  session->ConsumeLine(
      R"({"type":"problems","id":"A","data":{"id":"A","label":"A"}})");
  session->ConsumeLine(
      R"({"type":"problems","id":"A","data":null})");

  EXPECT_EQ(obs->upserts.size(), 1);
  EXPECT_EQ(obs->deletes.size(), 1);
}

// =====================================================================
// Multiple types in store
// =====================================================================

TEST(ContestStoreTest, MultipleObjectTypes) {
  ContestStore store;

  auto team = std::make_unique<Team>();
  team->id = "t1";
  store.ApplyUpsert(ObjectType::kTeams, "t1", std::move(team));

  auto prob = std::make_unique<Problem>();
  prob->id = "A";
  store.ApplyUpsert(ObjectType::kProblems, "A", std::move(prob));

  auto lang = std::make_unique<Language>();
  lang->id = "cpp";
  store.ApplyUpsert(ObjectType::kLanguages, "cpp", std::move(lang));

  EXPECT_EQ(store.ListObjects(ObjectType::kTeams).size(), 1);
  EXPECT_EQ(store.ListObjects(ObjectType::kProblems).size(), 1);
  EXPECT_EQ(store.ListObjects(ObjectType::kLanguages).size(), 1);
  EXPECT_EQ(store.ListObjects(ObjectType::kOrganizations).size(), 0);
}

// =====================================================================
// Move semantics
// =====================================================================

TEST(ContestStoreTest, MoveSemantics) {
  ContestStore store;
  auto prob = std::make_unique<Problem>();
  prob->id = "A";
  store.ApplyUpsert(ObjectType::kProblems, "A", std::move(prob));

  ContestStore store2 = std::move(store);
  auto result = store2.GetObject(ObjectType::kProblems, "A");
  EXPECT_TRUE(result.ok());
}

}  // namespace
}  // namespace ccsparser
