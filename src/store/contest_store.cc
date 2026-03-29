// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

#include "ccsparser/contest_store.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ccsparser {

// Hash for ObjectType enum to use as unordered_map key.
struct ObjectTypeHash {
  size_t operator()(ObjectType t) const noexcept {
    return std::hash<int>{}(static_cast<int>(t));
  }
};

struct ContestStore::Impl {
  // Typed singletons.
  std::unique_ptr<Contest> contest;
  std::unique_ptr<State> state;

  // Collection storage: type -> (id -> object).
  // Uses unordered_map for O(1) amortized insert/lookup on the hot path.
  std::unordered_map<ObjectType,
                     std::unordered_map<std::string,
                                        std::unique_ptr<ContestObject>>,
                     ObjectTypeHash>
      collections;

  // Event log for checkpoint/rollback.
  std::vector<RawEvent> event_log;

  // Observers.
  std::vector<std::shared_ptr<Observer>> observers;

  // Event count.
  size_t event_count = 0;
};

ContestStore::ContestStore() : impl_(std::make_unique<Impl>()) {}
ContestStore::~ContestStore() = default;
ContestStore::ContestStore(ContestStore&&) noexcept = default;
ContestStore& ContestStore::operator=(ContestStore&&) noexcept = default;

StatusOr<const ContestObject*> ContestStore::GetObject(
    ObjectType type, std::string_view id) const {
  if (type == ObjectType::kContest) {
    if (impl_->contest) return static_cast<const ContestObject*>(impl_->contest.get());
    return Status(StatusCode::kNotFound, "Contest not found");
  }
  if (type == ObjectType::kState) {
    if (impl_->state) return static_cast<const ContestObject*>(impl_->state.get());
    return Status(StatusCode::kNotFound, "State not found");
  }

  auto coll_it = impl_->collections.find(type);
  if (coll_it == impl_->collections.end()) {
    return Status(StatusCode::kNotFound, "No objects of type " +
                                              std::string(ObjectTypeToString(type)));
  }
  auto obj_it = coll_it->second.find(std::string(id));
  if (obj_it == coll_it->second.end()) {
    return Status(StatusCode::kNotFound,
                  "Object not found: " + std::string(ObjectTypeToString(type)) +
                      "/" + std::string(id));
  }
  return obj_it->second.get();
}

std::vector<const ContestObject*> ContestStore::ListObjects(
    ObjectType type) const {
  std::vector<const ContestObject*> result;

  if (type == ObjectType::kContest) {
    if (impl_->contest) result.push_back(impl_->contest.get());
    return result;
  }
  if (type == ObjectType::kState) {
    if (impl_->state) result.push_back(impl_->state.get());
    return result;
  }

  auto coll_it = impl_->collections.find(type);
  if (coll_it != impl_->collections.end()) {
    result.reserve(coll_it->second.size());
    for (const auto& [id, obj] : coll_it->second) {
      result.push_back(obj.get());
    }
  }
  return result;
}

const Contest* ContestStore::GetContest() const {
  return impl_->contest.get();
}

const State* ContestStore::GetState() const {
  return impl_->state.get();
}

size_t ContestStore::GetEventCount() const {
  return impl_->event_count;
}

std::vector<std::string> ContestStore::GetKnownProperties(
    ObjectType /*type*/) const {
  // Returns a list of property names seen for this type.
  // For simplicity, this is derived from the struct definition.
  return {};
}

Checkpoint ContestStore::CreateCheckpoint() const {
  return Checkpoint{impl_->event_log.size()};
}

Status ContestStore::Rollback(Checkpoint cp) {
  if (cp.event_index > impl_->event_log.size()) {
    return Status(StatusCode::kInvalidArgument,
                  "Checkpoint index exceeds event log size");
  }

  // Truncate the event log to the checkpoint position.
  std::vector<RawEvent> saved_events(impl_->event_log.begin(),
                                      impl_->event_log.begin() +
                                          static_cast<ptrdiff_t>(cp.event_index));

  // Clear all object state.  The caller (e.g. StreamingParseSession) is
  // responsible for replaying the saved events from the truncated log to
  // restore a consistent object state.
  impl_->contest.reset();
  impl_->state.reset();
  impl_->collections.clear();
  impl_->event_count = 0;
  impl_->event_log = std::move(saved_events);
  return Status::Ok();
}

void ContestStore::AddObserver(std::shared_ptr<Observer> observer) {
  impl_->observers.push_back(std::move(observer));
}

Status ContestStore::ApplyUpsert(ObjectType type, const std::string& id,
                                  std::unique_ptr<ContestObject> obj) {
  if (type == ObjectType::kContest) {
    auto* contest = dynamic_cast<Contest*>(obj.get());
    if (!contest) {
      return Status(StatusCode::kInternal, "Type mismatch for contest upsert");
    }
    impl_->contest.reset(static_cast<Contest*>(obj.release()));
    for (auto& obs : impl_->observers) {
      obs->OnObjectUpserted(type, id, *impl_->contest);
    }
  } else if (type == ObjectType::kState) {
    auto* state = dynamic_cast<State*>(obj.get());
    if (!state) {
      return Status(StatusCode::kInternal, "Type mismatch for state upsert");
    }
    impl_->state.reset(static_cast<State*>(obj.release()));
    for (auto& obs : impl_->observers) {
      obs->OnObjectUpserted(type, id, *impl_->state);
    }
  } else {
    auto& coll = impl_->collections[type];
    auto [it, _] = coll.insert_or_assign(id, std::move(obj));
    for (auto& obs : impl_->observers) {
      obs->OnObjectUpserted(type, id, *it->second);
    }
  }
  impl_->event_count++;
  return Status::Ok();
}

Status ContestStore::ApplyDelete(ObjectType type, const std::string& id) {
  if (type == ObjectType::kContest || type == ObjectType::kState) {
    // Singletons: deleting is unusual but we handle it.
    if (type == ObjectType::kContest) {
      impl_->contest.reset();
    } else {
      impl_->state.reset();
    }
    for (auto& obs : impl_->observers) {
      obs->OnObjectDeleted(type, id);
    }
    impl_->event_count++;
    return Status::Ok();
  }

  auto coll_it = impl_->collections.find(type);
  if (coll_it == impl_->collections.end() ||
      coll_it->second.find(id) == coll_it->second.end()) {
    // Object doesn't exist — this is a warning, not error.
    impl_->event_count++;
    return Status::Ok();
  }
  coll_it->second.erase(id);
  for (auto& obs : impl_->observers) {
    obs->OnObjectDeleted(type, id);
  }
  impl_->event_count++;
  return Status::Ok();
}

Status ContestStore::ApplyCollectionReplace(
    ObjectType type,
    std::vector<std::unique_ptr<ContestObject>> objects) {
  // Atomic: replace the entire collection.
  auto& coll = impl_->collections[type];
  coll.clear();
  size_t count = objects.size();
  for (auto& obj : objects) {
    std::string obj_id = obj->id;
    coll[obj_id] = std::move(obj);
  }
  for (auto& obs : impl_->observers) {
    obs->OnCollectionReplaced(type, count);
  }
  impl_->event_count++;
  return Status::Ok();
}

Status ContestStore::ApplySingletonUpdate(
    ObjectType type, std::unique_ptr<ContestObject> obj) {
  return ApplyUpsert(type, obj->id, std::move(obj));
}

void ContestStore::RecordEvent(RawEvent event) {
  impl_->event_log.push_back(std::move(event));
}

void ContestStore::NotifyDiagnostic(const Diagnostic& diag) {
  for (auto& obs : impl_->observers) {
    obs->OnDiagnostic(diag);
  }
}

const std::vector<RawEvent>& ContestStore::GetEventLog() const {
  return impl_->event_log;
}

}  // namespace ccsparser
