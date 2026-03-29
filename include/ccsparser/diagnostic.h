// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

#ifndef CCSPARSER_DIAGNOSTIC_H_
#define CCSPARSER_DIAGNOSTIC_H_

#include <cstddef>
#include <optional>
#include <string>

namespace ccsparser {

enum class Severity {
  kInfo,
  kWarning,
  kError,
};

// Diagnostic codes covering all error categories from the specification.
enum class DiagnosticCode {
  kMalformedJson,
  kInvalidUtf8,
  kLineTooLong,
  kMissingType,
  kInvalidTypeField,
  kMissingData,
  kInvalidIdType,
  kUnknownEventType,
  kInvalidObjectShape,
  kInvalidRequiredField,
  kInvalidTime,
  kInvalidReltime,
  kInvalidCollectionItem,
  kDeleteUnknownObject,
  kVersionConflict,
  kExplicitVersionMismatch,
  kMaxConsecutiveErrorsExceeded,
  kInvalidTokenType,
  kTruncatedJson,
  kNonObjectRoot,
  kMissingObjectId,
  kDuplicateEvent,
  kUnknownField,
  kMissingResourceFile,
  kPackageError,
};

// A diagnostic message emitted during parsing.
struct Diagnostic {
  Severity severity = Severity::kInfo;
  DiagnosticCode code = DiagnosticCode::kMalformedJson;
  std::string message;
  size_t line_no = 0;
  std::optional<std::string> raw_line;
  std::optional<std::string> object_type;
  std::optional<std::string> object_id;

  std::string ToString() const;
};

}  // namespace ccsparser

#endif  // CCSPARSER_DIAGNOSTIC_H_
