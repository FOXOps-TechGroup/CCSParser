// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

#include "ccsparser/event_feed_stream.h"

#include <curl/curl.h>
#include <limits>

namespace ccsparser {

StatusOr<EventFeedStreamResult>
StreamEventFeed(const std::string &url, StreamingParseSession &session,
                const EventFeedStreamOptions &options) {
  if (!session.enforces_declared_version()) {
    return Status(
        StatusCode::kFailedPrecondition,
        "An explicit, strictly validated contest API version is required");
  }
  if (!session.cursor().contiguous) {
    return Status(StatusCode::kFailedPrecondition,
                  "Event feed session contains an unapplied event");
  }

  if (url.find('#') != std::string::npos) {
    return Status(StatusCode::kInvalidArgument,
                  "Event feed URL must not contain a fragment");
  }
  HttpClient client;
  std::string request_url = url;
  if (options.since_token) {
    if (options.since_token->empty()) {
      return Status(StatusCode::kInvalidArgument, "Event feed token is empty");
    }
    if (options.since_token->size() >
        static_cast<size_t>(std::numeric_limits<int>::max())) {
      return Status(StatusCode::kInvalidArgument,
                    "Event feed token is too long");
    }
    char *encoded =
        curl_easy_escape(nullptr, options.since_token->c_str(),
                         static_cast<int>(options.since_token->size()));
    if (!encoded) {
      return Status(StatusCode::kInternal, "Could not encode event feed token");
    }
    request_url += request_url.find('?') == std::string::npos ? '?' : '&';
    request_url += "since_token=";
    request_url += encoded;
    curl_free(encoded);
  }

  EventFeedStreamDecoder decoder(session, options.max_line_bytes,
                                 options.on_event, options.on_activity);
  Status parse_status = Status::Ok();
  auto response = client.GetStreaming(
      request_url,
      [&](const char *data, size_t size) {
        parse_status = decoder.Feed(std::string_view(data, size));
        return parse_status.ok() && !decoder.stopped() && !decoder.ended();
      },
      options.http);
  if (!parse_status.ok())
    return parse_status;
  if (!response.ok())
    return response.status();

  EventFeedStreamResult result{response.value(), decoder.stopped(),
                               decoder.ended()};
  if (result.http_status >= 200 && result.http_status < 300 &&
      !result.stopped_by_callback && !result.end_of_updates) {
    auto status = decoder.Finish();
    if (!status.ok())
      return status;
  }
  return result;
}

} // namespace ccsparser
