// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

#ifndef CCSPARSER_EVENT_FEED_STREAM_H_
#define CCSPARSER_EVENT_FEED_STREAM_H_

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "ccsparser/parser.h"
#include "ccsparser/status.h"

namespace ccsparser {

// Delivers complete NDJSON records to an existing parse session. A callback
// returning false stops consumption after the current event.
class EventFeedStreamDecoder {
  public:
  using EventCallback = std::function<bool(const StreamingParseSession &)>;
  using ActivityCallback = std::function<void()>;

  explicit EventFeedStreamDecoder(StreamingParseSession &session,
                                  size_t max_line_bytes,
                                  EventCallback callback = {},
                                  ActivityCallback on_activity = {});

  Status Feed(std::string_view bytes);
  // An unfinished line is not applied, so the previous cursor is reusable.
  Status Finish();
  bool stopped() const { return stopped_; }
  bool ended() const { return ended_; }

  private:
  StreamingParseSession &session_;
  size_t max_line_bytes_;
  EventCallback callback_;
  ActivityCallback on_activity_;
  std::string pending_;
  bool stopped_ = false;
  bool ended_ = false;
  bool failed_ = false;
};

struct EventFeedStreamOptions {
  HttpRequestOptions http;
  std::optional<std::string> since_token;
  size_t max_line_bytes = 16 * 1024 * 1024;
  EventFeedStreamDecoder::EventCallback on_event;
  EventFeedStreamDecoder::ActivityCallback on_activity;
};

struct EventFeedStreamResult {
  int http_status = 0;
  bool stopped_by_callback = false;
  bool end_of_updates = false;
};

// The URL is the event-feed URL; since_token is percent-encoded if supplied.
// Non-2xx HTTP status is returned in the result without parsing its body.
StatusOr<EventFeedStreamResult>
StreamEventFeed(const std::string &url, StreamingParseSession &session,
                const EventFeedStreamOptions &options = {});

} // namespace ccsparser

#endif // CCSPARSER_EVENT_FEED_STREAM_H_
