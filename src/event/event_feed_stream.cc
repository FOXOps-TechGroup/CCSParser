// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

#include "ccsparser/event_feed_stream.h"

#include <utility>

namespace ccsparser {

EventFeedStreamDecoder::EventFeedStreamDecoder(StreamingParseSession &session,
                                               size_t max_line_bytes,
                                               EventCallback callback,
                                               ActivityCallback on_activity)
    : session_(session), max_line_bytes_(max_line_bytes),
      callback_(std::move(callback)), on_activity_(std::move(on_activity)) {}

Status EventFeedStreamDecoder::Feed(std::string_view bytes) {
  if (failed_ || stopped_ || ended_) {
    return Status(StatusCode::kFailedPrecondition,
                  "Event feed decoder is no longer accepting input");
  }
  if (!session_.cursor().contiguous) {
    failed_ = true;
    return Status(StatusCode::kFailedPrecondition,
                  "Event feed session contains an unapplied event");
  }

  while (!bytes.empty()) {
    const size_t newline = bytes.find('\n');
    const size_t count =
        newline == std::string_view::npos ? bytes.size() : newline;
    if (pending_.size() > max_line_bytes_ ||
        count > max_line_bytes_ - pending_.size()) {
      failed_ = true;
      return Status(StatusCode::kLimitExceeded,
                    "Event feed line exceeds limit");
    }
    pending_.append(bytes.data(), count);
    if (newline == std::string_view::npos)
      return Status::Ok();

    const size_t before = session_.cursor().event_count;
    Status status = session_.ConsumeLine(pending_);
    pending_.clear();
    if (!status.ok()) {
      failed_ = true;
      return status;
    }
    if (!session_.cursor().contiguous) {
      failed_ = true;
      return Status(StatusCode::kCorruptedInput,
                    "Event feed record was not applied to the session");
    }
    if (on_activity_)
      on_activity_();
    if (session_.cursor().event_count != before) {
      ended_ = session_.cursor().end_of_updates;
      if (callback_ && !callback_(session_))
        stopped_ = true;
      if (ended_) {
        const auto remaining = bytes.substr(newline + 1);
        if (remaining.find_first_not_of(" \t\r\n") != std::string_view::npos) {
          failed_ = true;
          return Status(StatusCode::kCorruptedInput,
                        "Event feed contained data after end_of_updates");
        }
      }
      if (stopped_ || ended_)
        return Status::Ok();
    }
    bytes.remove_prefix(newline + 1);
  }
  return Status::Ok();
}

Status EventFeedStreamDecoder::Finish() {
  if (failed_) {
    return Status(StatusCode::kFailedPrecondition, "Event feed decoder failed");
  }
  if (!pending_.empty()) {
    failed_ = true;
    return Status(StatusCode::kCorruptedInput, "Truncated event feed line");
  }
  return Status::Ok();
}

} // namespace ccsparser
