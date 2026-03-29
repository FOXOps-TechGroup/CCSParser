// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

#ifndef CCSPARSER_OBSERVER_H_
#define CCSPARSER_OBSERVER_H_

#include "ccsparser/diagnostic.h"
#include "ccsparser/event.h"
#include "ccsparser/objects.h"
#include "ccsparser/types.h"

namespace ccsparser {

// Observer interface for parser events.
class Observer {
 public:
  virtual ~Observer() = default;

  virtual void OnRawEventParsed(const RawEvent& event) { (void)event; }
  virtual void OnObjectUpserted(ObjectType type, const std::string& id,
                                const ContestObject& obj) {
    (void)type;
    (void)id;
    (void)obj;
  }
  virtual void OnObjectDeleted(ObjectType type, const std::string& id) {
    (void)type;
    (void)id;
  }
  virtual void OnCollectionReplaced(ObjectType type, size_t count) {
    (void)type;
    (void)count;
  }
  virtual void OnDiagnostic(const Diagnostic& diag) { (void)diag; }
  virtual void OnEndOfUpdates() {}
};

}  // namespace ccsparser

#endif  // CCSPARSER_OBSERVER_H_
