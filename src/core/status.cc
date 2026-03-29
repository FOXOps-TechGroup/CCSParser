// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

#include "ccsparser/status.h"

#include <string>

namespace ccsparser {

std::string Status::ToString() const {
  if (ok()) return "OK";
  std::string result;
  switch (code_) {
    case StatusCode::kOk:
      result = "OK";
      break;
    case StatusCode::kInvalidArgument:
      result = "INVALID_ARGUMENT";
      break;
    case StatusCode::kNotFound:
      result = "NOT_FOUND";
      break;
    case StatusCode::kAlreadyExists:
      result = "ALREADY_EXISTS";
      break;
    case StatusCode::kFailedPrecondition:
      result = "FAILED_PRECONDITION";
      break;
    case StatusCode::kInternal:
      result = "INTERNAL";
      break;
    case StatusCode::kParseError:
      result = "PARSE_ERROR";
      break;
    case StatusCode::kCorruptedInput:
      result = "CORRUPTED_INPUT";
      break;
    case StatusCode::kLimitExceeded:
      result = "LIMIT_EXCEEDED";
      break;
    case StatusCode::kEndOfStream:
      result = "END_OF_STREAM";
      break;
    case StatusCode::kUnavailable:
      result = "UNAVAILABLE";
      break;
  }
  if (!message_.empty()) {
    result += ": ";
    result += message_;
  }
  return result;
}

}  // namespace ccsparser
