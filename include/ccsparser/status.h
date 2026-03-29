// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

#ifndef CCSPARSER_STATUS_H_
#define CCSPARSER_STATUS_H_

#include <string>
#include <string_view>
#include <variant>

namespace ccsparser {

// Error codes for Status.
enum class StatusCode {
  kOk = 0,
  kInvalidArgument,
  kNotFound,
  kAlreadyExists,
  kFailedPrecondition,
  kInternal,
  kParseError,
  kCorruptedInput,
  kLimitExceeded,
  kEndOfStream,
  kUnavailable,  // Network/HTTP error.
};

// Lightweight status type for operation results.
class Status {
 public:
  Status() : code_(StatusCode::kOk) {}
  Status(StatusCode code, std::string message)
      : code_(code), message_(std::move(message)) {}

  static Status Ok() { return Status(); }

  bool ok() const { return code_ == StatusCode::kOk; }
  StatusCode code() const { return code_; }
  const std::string& message() const { return message_; }

  std::string ToString() const;

 private:
  StatusCode code_;
  std::string message_;
};

// StatusOr<T>: holds either a value or an error Status.
template <typename T>
class StatusOr {
 public:
  StatusOr(T value) : data_(std::move(value)) {}  // NOLINT: implicit
  StatusOr(Status status) : data_(std::move(status)) {}  // NOLINT: implicit

  bool ok() const { return std::holds_alternative<T>(data_); }

  const T& value() const& { return std::get<T>(data_); }
  T& value() & { return std::get<T>(data_); }
  T&& value() && { return std::get<T>(std::move(data_)); }

  const Status& status() const { return std::get<Status>(data_); }

  const T& operator*() const& { return value(); }
  T& operator*() & { return value(); }
  T&& operator*() && { return std::move(*this).value(); }

  const T* operator->() const { return &value(); }
  T* operator->() { return &value(); }

 private:
  std::variant<T, Status> data_;
};

}  // namespace ccsparser

#endif  // CCSPARSER_STATUS_H_
