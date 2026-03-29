// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

#ifndef CCSPARSER_CONTEST_STORE_H_
#define CCSPARSER_CONTEST_STORE_H_

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ccsparser/diagnostic.h"
#include "ccsparser/event.h"
#include "ccsparser/objects.h"
#include "ccsparser/observer.h"
#include "ccsparser/status.h"
#include "ccsparser/types.h"

namespace ccsparser {

// Opaque checkpoint handle for rollback.
struct Checkpoint {
  size_t event_index = 0;
};

// Central contest state store.
//
// Manages all contest objects, supports observer notifications,
// checkpoint/rollback, and provides query APIs.
class ContestStore {
 public:
  ContestStore();
  ~ContestStore();

  // Move-only.
  ContestStore(ContestStore&&) noexcept;
  ContestStore& operator=(ContestStore&&) noexcept;

  // Object queries.
  StatusOr<const ContestObject*> GetObject(ObjectType type,
                                           std::string_view id) const;
  std::vector<const ContestObject*> ListObjects(ObjectType type) const;
  const Contest* GetContest() const;
  const State* GetState() const;
  size_t GetEventCount() const;
  std::vector<std::string> GetKnownProperties(ObjectType type) const;

  // Checkpoint/rollback.
  Checkpoint CreateCheckpoint() const;
  Status Rollback(Checkpoint cp);

  // Observer management.
  void AddObserver(std::shared_ptr<Observer> observer);

  // --- Internal mutation API (used by parser, not part of public API) ---

  // Apply a single object upsert.
  Status ApplyUpsert(ObjectType type, const std::string& id,
                     std::unique_ptr<ContestObject> obj);
  // Apply a delete.
  Status ApplyDelete(ObjectType type, const std::string& id);
  // Apply a collection replace (atomic).
  Status ApplyCollectionReplace(
      ObjectType type,
      std::vector<std::unique_ptr<ContestObject>> objects);
  // Apply a singleton update.
  Status ApplySingletonUpdate(ObjectType type,
                              std::unique_ptr<ContestObject> obj);

  // Record an event in the event log.
  void RecordEvent(RawEvent event);

  // Notify observers of a diagnostic.
  void NotifyDiagnostic(const Diagnostic& diag);

  // Get the event log.
  const std::vector<RawEvent>& GetEventLog() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace ccsparser

#endif  // CCSPARSER_CONTEST_STORE_H_
