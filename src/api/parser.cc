// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

#include "ccsparser/parser.h"

#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "ccsparser/contest_store.h"
#include "ccsparser/diagnostic.h"
#include "ccsparser/event.h"
#include "ccsparser/objects.h"
#include "ccsparser/parse_options.h"
#include "ccsparser/status.h"
#include "ccsparser/types.h"
#include "src/event/ndjson_line_parser.h"
#include "src/profile/version_profile.h"

namespace ccsparser {

struct StreamingParseSession::Impl {
  ParseOptions options;
  ContestStore store;
  EventCursor cursor;
  std::vector<Diagnostic> diagnostics;
  std::unique_ptr<internal::VersionProfile> profile;
  ApiVersion resolved_version = ApiVersion::kAuto;
  size_t consecutive_errors = 0;
  bool finished = false;
  bool version_detected = false;

  void AddDiagnostic(Diagnostic diag) {
    store.NotifyDiagnostic(diag);
    if (diagnostics.size() < options.limits.max_diagnostics) {
      diagnostics.push_back(std::move(diag));
    }
  }

  Status ApplyEvent(internal::LineParseResult& lr) {
    auto& event = lr.event;

    // Notify observers of raw event.
    // (Observers are registered on the store and notified via store methods.)

    // Handle unknown type.
    if (event.object_type == ObjectType::kUnknown) {
      Diagnostic diag;
      diag.severity = Severity::kWarning;
      diag.code = DiagnosticCode::kUnknownEventType;
      diag.message = "Unknown event type: " + event.type_str;
      diag.line_no = event.line_no;
      diag.object_type = event.type_str;
      AddDiagnostic(std::move(diag));

      if (options.unknown_type_policy == UnknownTypePolicy::kError) {
        return Status(StatusCode::kParseError,
                      "Unknown event type: " + event.type_str);
      }
      if (options.unknown_type_policy == UnknownTypePolicy::kWarnAndIgnore) {
        cursor.contiguous = false;
        return Status::Ok();
      }
      if (event.shape != EventShape::kDelete) {
        event.data_json = event.data_is_null ? "null" : lr.parsed_data.dump();
      }
      auto status = store.ApplyRawExtension(event);
      if (!status.ok()) return status;
      if (options.keep_event_log) store.RecordEvent(event);
      ++cursor.event_count;
      if (cursor.contiguous) {
        if (event.token) cursor.last_token = event.token;
        if (event.id) cursor.last_event_id = event.id;
      }
      return Status::Ok();
    }

    // Auto-detect version from pre-parsed data (no re-parsing needed).
    if (options.version == ApiVersion::kAuto && !version_detected &&
        event.shape != EventShape::kDelete) {
      DetectVersion(event, lr.parsed_data);
    }

    // Apply event based on shape, forwarding pre-parsed JSON.
    Status status = Status::Ok();
    switch (event.shape) {
      case EventShape::kDelete:
        status = ApplyDelete(event);
        break;
      case EventShape::kSingleObject:
        status = ApplySingleObject(event, lr.parsed_data);
        break;
      case EventShape::kCollectionReplace:
        status = ApplyCollectionReplace(event, lr.parsed_data);
        break;
      case EventShape::kSingletonUpdate:
        status = ApplySingletonUpdate(event, lr.parsed_data);
        break;
    }
    if (!status.ok()) return status;

    if (options.keep_event_log) {
      if (event.data_json.empty() && event.shape != EventShape::kDelete) {
        event.data_json = lr.parsed_data.dump();
      }
      store.RecordEvent(event);
    }
    cursor.event_count++;
    if (cursor.contiguous) {
      if (event.token.has_value()) cursor.last_token = event.token;
      if (event.id.has_value()) cursor.last_event_id = event.id;
    }
    return Status::Ok();
  }

  void DetectVersion(const RawEvent& event,
                     const nlohmann::json& data) {
    if (data.empty()) return;

    if (event.object_type == ObjectType::kContest) {
      auto pt_it = data.find("penalty_time");
      if (pt_it != data.end()) {
        if (pt_it->is_number_integer()) {
          SetDetectedVersion(ApiVersion::k2023_06);
        } else if (pt_it->is_string()) {
          SetDetectedVersion(ApiVersion::k2026_01);
        }
      }
    } else if (event.object_type == ObjectType::kClarifications) {
      if (data.find("to_team_ids") != data.end()) {
        SetDetectedVersion(ApiVersion::k2026_01);
      } else if (data.find("to_team_id") != data.end()) {
        SetDetectedVersion(ApiVersion::k2023_06);
      }
    }
  }

  void SetDetectedVersion(ApiVersion ver) {
    version_detected = true;
    resolved_version = ver;
    profile = internal::CreateProfile(ver);
  }

  Status ApplyDelete(RawEvent& event) {
    if (!event.id.has_value()) {
      return Status(StatusCode::kParseError,
                    "Delete event missing id at line " +
                        std::to_string(event.line_no));
    }

    auto obj_result = store.GetObject(event.object_type, *event.id);
    if (!obj_result.ok()) {
      Diagnostic diag;
      diag.severity = Severity::kWarning;
      diag.code = DiagnosticCode::kDeleteUnknownObject;
      diag.message =
          "Delete of unknown object: " + event.type_str + "/" + *event.id;
      diag.line_no = event.line_no;
      diag.object_type = event.type_str;
      diag.object_id = *event.id;
      AddDiagnostic(std::move(diag));
    }

    return store.ApplyDelete(event.object_type, *event.id);
  }

  Status ApplySingleObject(RawEvent& event,
                           const nlohmann::json& data) {
    auto decode_result = profile->DecodeObject(
        event.object_type, data, event.line_no, options);
    if (!decode_result.ok()) {
      return decode_result.status();
    }

    auto& dr = decode_result.value();
    for (auto& d : dr.diagnostics) {
      AddDiagnostic(std::move(d));
    }

    if (!dr.object) {
      return Status(StatusCode::kInternal,
                    "Decoder returned null object at line " +
                        std::to_string(event.line_no));
    }

    const std::string id = event.id.value_or(dr.object->id);
    return store.ApplyUpsert(event.object_type, id, std::move(dr.object));
  }

  Status ApplyCollectionReplace(RawEvent& event,
                                const nlohmann::json& data) {
    auto decode_result = profile->DecodeCollection(
        event.object_type, data, event.line_no, options);
    if (!decode_result.ok()) {
      // Collection replace failure: entire replace is atomic failure.
      return decode_result.status();
    }

    auto& cr = decode_result.value();
    for (auto& d : cr.diagnostics) {
      AddDiagnostic(std::move(d));
    }

    return store.ApplyCollectionReplace(event.object_type,
                                         std::move(cr.objects));
  }

  Status ApplySingletonUpdate(RawEvent& event,
                              const nlohmann::json& data) {
    auto decode_result = profile->DecodeObject(
        event.object_type, data, event.line_no, options);
    if (!decode_result.ok()) {
      return decode_result.status();
    }

    auto& dr = decode_result.value();
    for (auto& d : dr.diagnostics) {
      AddDiagnostic(std::move(d));
    }

    if (!dr.object) {
      return Status(StatusCode::kInternal,
                    "Decoder returned null singleton at line " +
                        std::to_string(event.line_no));
    }

    bool end_of_updates = false;
    if (event.object_type == ObjectType::kState) {
      auto* state_obj = dynamic_cast<State*>(dr.object.get());
      end_of_updates = state_obj && state_obj->end_of_updates.has_value();
    }
    auto status =
        store.ApplySingletonUpdate(event.object_type, std::move(dr.object));
    if (status.ok() && end_of_updates) {
      cursor.end_of_updates = true;
      store.NotifyEndOfUpdates();
    }
    return status;
  }
};

StreamingParseSession::StreamingParseSession(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

StreamingParseSession::~StreamingParseSession() = default;

Status StreamingParseSession::ConsumeLine(std::string_view line) {
  if (impl_->finished) {
    return Status(StatusCode::kFailedPrecondition,
                  "Session already finished");
  }

  impl_->cursor.line_no++;
  size_t current_line = impl_->cursor.line_no;

  auto parse_result =
      internal::ParseNdjsonLine(line, current_line, impl_->options);

  if (!parse_result.ok()) {
    impl_->cursor.contiguous = false;
    impl_->consecutive_errors++;

    Diagnostic diag;
    diag.severity = Severity::kError;
    diag.line_no = current_line;
    diag.message = parse_result.status().message();

    // Determine diagnostic code from error message.
    const auto& msg = parse_result.status().message();
    if (msg.find("Malformed JSON") != std::string::npos) {
      diag.code = DiagnosticCode::kMalformedJson;
    } else if (msg.find("Non-object") != std::string::npos) {
      diag.code = DiagnosticCode::kNonObjectRoot;
    } else if (msg.find("too long") != std::string::npos) {
      diag.code = DiagnosticCode::kLineTooLong;
    } else if (msg.find("Missing 'type'") != std::string::npos) {
      diag.code = DiagnosticCode::kMissingType;
    } else if (msg.find("Invalid 'type'") != std::string::npos) {
      diag.code = DiagnosticCode::kInvalidTypeField;
    } else if (msg.find("Missing 'data'") != std::string::npos) {
      diag.code = DiagnosticCode::kMissingData;
    } else if (msg.find("Invalid 'id'") != std::string::npos) {
      diag.code = DiagnosticCode::kInvalidIdType;
    } else if (msg.find("Invalid 'token'") != std::string::npos) {
      diag.code = DiagnosticCode::kInvalidTokenType;
    } else {
      diag.code = DiagnosticCode::kMalformedJson;
    }

    if (impl_->options.keep_raw_json) {
      diag.raw_line = std::string(line);
    }
    impl_->AddDiagnostic(std::move(diag));

    if (impl_->consecutive_errors >=
        impl_->options.limits.max_consecutive_errors) {
      Diagnostic limit_diag;
      limit_diag.severity = Severity::kError;
      limit_diag.code = DiagnosticCode::kMaxConsecutiveErrorsExceeded;
      limit_diag.message = "Max consecutive errors exceeded (" +
                            std::to_string(impl_->consecutive_errors) + ")";
      limit_diag.line_no = current_line;
      impl_->AddDiagnostic(std::move(limit_diag));
      return Status(StatusCode::kLimitExceeded,
                    "Max consecutive errors exceeded");
    }

    if (impl_->options.error_policy == ErrorPolicy::kFailFast) {
      return parse_result.status();
    }
    return Status::Ok();
  }

  auto& lr = parse_result.value();

  if (lr.is_keepalive) {
    impl_->consecutive_errors = 0;
    return Status::Ok();
  }

  auto apply_status = impl_->cursor.end_of_updates
                          ? Status(StatusCode::kCorruptedInput,
                                   "Event received after end_of_updates")
                          : impl_->ApplyEvent(lr);
  if (!apply_status.ok()) {
    impl_->cursor.contiguous = false;
    impl_->consecutive_errors++;

    Diagnostic diag;
    diag.severity = Severity::kError;
    diag.code = DiagnosticCode::kInvalidObjectShape;
    diag.message = apply_status.message();
    diag.line_no = current_line;
    diag.object_type = lr.event.type_str;
    if (lr.event.id.has_value()) diag.object_id = *lr.event.id;
    if (impl_->options.keep_raw_json) {
      diag.raw_line = std::string(line);
    }
    impl_->AddDiagnostic(std::move(diag));

    if (impl_->consecutive_errors >=
        impl_->options.limits.max_consecutive_errors) {
      return Status(StatusCode::kLimitExceeded,
                    "Max consecutive errors exceeded");
    }

    if (impl_->options.error_policy == ErrorPolicy::kFailFast) {
      return apply_status;
    }
    return Status::Ok();
  }

  impl_->consecutive_errors = 0;
  return Status::Ok();
}

Status StreamingParseSession::Finish() {
  impl_->finished = true;
  return Status::Ok();
}

const EventCursor& StreamingParseSession::cursor() const {
  return impl_->cursor;
}

const ContestStore& StreamingParseSession::store() const {
  return impl_->store;
}

ContestStore& StreamingParseSession::mutable_store() {
  return impl_->store;
}

const std::vector<Diagnostic>& StreamingParseSession::diagnostics() const {
  return impl_->diagnostics;
}

ApiVersion StreamingParseSession::resolved_version() const {
  return impl_->resolved_version;
}

bool StreamingParseSession::has_explicit_version() const {
  return impl_->options.version != ApiVersion::kAuto;
}

bool StreamingParseSession::enforces_declared_version() const {
  return has_explicit_version() && impl_->options.strict_version;
}

// ---- EventFeedParser ----

StatusOr<std::unique_ptr<StreamingParseSession>>
EventFeedParser::CreateStreamingSession(const ParseOptions& options) {
  if (options.strict_version && options.version == ApiVersion::kAuto) {
    return Status(StatusCode::kInvalidArgument,
                  "Strict version validation requires an explicit version");
  }
  auto impl = std::make_unique<StreamingParseSession::Impl>();
  impl->options = options;

  if (options.version != ApiVersion::kAuto) {
    impl->resolved_version = options.version;
    impl->profile = internal::CreateProfile(options.version);
    impl->version_detected = true;
  } else {
    impl->resolved_version = ApiVersion::k2023_06;
    impl->profile = internal::CreateProfile(ApiVersion::k2023_06);
  }

  return std::unique_ptr<StreamingParseSession>(
      new StreamingParseSession(std::move(impl)));
}

StatusOr<ParseResult> EventFeedParser::ParseStream(
    std::istream& stream, const ParseOptions& options) {
  auto session_or = CreateStreamingSession(options);
  if (!session_or.ok()) return session_or.status();

  auto& session = session_or.value();
  std::string line;
  line.reserve(4096);  // Pre-allocate to reduce repeated small allocations.
  while (std::getline(stream, line)) {
    auto status = session->ConsumeLine(line);
    if (!status.ok()) {
      if (status.code() == StatusCode::kLimitExceeded) {
        break;
      }
      if (options.error_policy == ErrorPolicy::kFailFast) {
        return status;
      }
    }
  }

  session->Finish();

  ParseResult result;
  result.resolved_version = session->resolved_version();
  result.store = std::move(session->mutable_store());
  result.cursor = session->cursor();
  result.diagnostics = session->diagnostics();
  return result;
}

StatusOr<ParseResult> EventFeedParser::ParseFile(
    const std::filesystem::path& path, const ParseOptions& options) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    return Status(StatusCode::kNotFound,
                  "Cannot open file: " + path.string());
  }
  // Use a large read buffer to reduce syscall overhead.
  static constexpr size_t kReadBufSize = 1 << 20;  // 1 MB
  std::unique_ptr<char[]> buf(new char[kReadBufSize]);
  file.rdbuf()->pubsetbuf(buf.get(), kReadBufSize);
  return ParseStream(file, options);
}

}  // namespace ccsparser
