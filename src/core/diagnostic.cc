// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

#include "ccsparser/diagnostic.h"

#include <sstream>
#include <string>

namespace ccsparser {

std::string Diagnostic::ToString() const {
  std::ostringstream oss;
  switch (severity) {
    case Severity::kInfo:
      oss << "[INFO]";
      break;
    case Severity::kWarning:
      oss << "[WARNING]";
      break;
    case Severity::kError:
      oss << "[ERROR]";
      break;
  }
  oss << " line " << line_no << ": " << message;
  if (object_type.has_value()) {
    oss << " (type=" << *object_type;
    if (object_id.has_value()) {
      oss << ", id=" << *object_id;
    }
    oss << ")";
  }
  return oss.str();
}

}  // namespace ccsparser
