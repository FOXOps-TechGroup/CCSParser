// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

#include "time_utils.h"

#include <cctype>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

namespace ccsparser {
namespace internal {

StatusOr<RelativeTime> ParseRelTime(std::string_view s) {
  if (s.empty()) {
    return Status(StatusCode::kParseError, "Empty RELTIME string");
  }

  bool negative = false;
  size_t pos = 0;
  if (s[0] == '-') {
    negative = true;
    pos = 1;
  }

  // Parse hours (variable length).
  size_t colon1 = s.find(':', pos);
  if (colon1 == std::string_view::npos || colon1 == pos) {
    return Status(StatusCode::kParseError,
                  "Invalid RELTIME: missing hours: " + std::string(s));
  }

  int64_t hours = 0;
  auto hr_str = s.substr(pos, colon1 - pos);
  auto [hr_ptr, hr_ec] =
      std::from_chars(hr_str.data(), hr_str.data() + hr_str.size(), hours);
  if (hr_ec != std::errc() || hr_ptr != hr_str.data() + hr_str.size()) {
    return Status(StatusCode::kParseError,
                  "Invalid RELTIME hours: " + std::string(s));
  }

  // Parse minutes (exactly 2 digits).
  size_t colon2 = s.find(':', colon1 + 1);
  if (colon2 == std::string_view::npos || colon2 != colon1 + 3) {
    return Status(StatusCode::kParseError,
                  "Invalid RELTIME minutes: " + std::string(s));
  }

  int64_t minutes = 0;
  auto min_str = s.substr(colon1 + 1, 2);
  auto [min_ptr, min_ec] =
      std::from_chars(min_str.data(), min_str.data() + min_str.size(), minutes);
  if (min_ec != std::errc() || min_ptr != min_str.data() + min_str.size()) {
    return Status(StatusCode::kParseError,
                  "Invalid RELTIME minutes: " + std::string(s));
  }
  if (minutes < 0 || minutes > 59) {
    return Status(StatusCode::kParseError,
                  "RELTIME minutes out of range: " + std::string(s));
  }

  // Parse seconds (exactly 2 digits, optionally followed by .uuu).
  auto rest = s.substr(colon2 + 1);
  int64_t seconds = 0;
  int64_t frac_ms = 0;

  if (rest.size() < 2) {
    return Status(StatusCode::kParseError,
                  "Invalid RELTIME seconds: " + std::string(s));
  }

  auto sec_str = rest.substr(0, 2);
  auto [sec_ptr, sec_ec] =
      std::from_chars(sec_str.data(), sec_str.data() + sec_str.size(), seconds);
  if (sec_ec != std::errc() || sec_ptr != sec_str.data() + sec_str.size()) {
    return Status(StatusCode::kParseError,
                  "Invalid RELTIME seconds: " + std::string(s));
  }
  if (seconds < 0 || seconds > 59) {
    return Status(StatusCode::kParseError,
                  "RELTIME seconds out of range: " + std::string(s));
  }

  // Optional fractional part.
  if (rest.size() > 2) {
    if (rest[2] != '.') {
      return Status(StatusCode::kParseError,
                    "Invalid RELTIME fraction: " + std::string(s));
    }
    auto frac_str = rest.substr(3);
    if (frac_str.empty()) {
      return Status(StatusCode::kParseError,
                    "Invalid RELTIME: empty fraction: " + std::string(s));
    }
    // Truncate to at most 3 significant digits (millisecond precision),
    // consistent with ParseAbsTime behaviour.
    if (frac_str.size() > 3) {
      frac_str = frac_str.substr(0, 3);
    }
    int64_t frac_val = 0;
    auto [frac_ptr, frac_ec] = std::from_chars(
        frac_str.data(), frac_str.data() + frac_str.size(), frac_val);
    if (frac_ec != std::errc() ||
        frac_ptr != frac_str.data() + frac_str.size()) {
      return Status(StatusCode::kParseError,
                    "Invalid RELTIME fraction: " + std::string(s));
    }
    // Normalize to milliseconds.
    if (frac_str.size() == 1) {
      frac_ms = frac_val * 100;
    } else if (frac_str.size() == 2) {
      frac_ms = frac_val * 10;
    } else {
      frac_ms = frac_val;
    }
  }

  int64_t total_ms =
      (hours * 3600 + minutes * 60 + seconds) * 1000 + frac_ms;
  if (negative) total_ms = -total_ms;

  return RelativeTime{total_ms};
}

RelativeTime RelTimeFromMinutes(int minutes) {
  return RelativeTime{static_cast<int64_t>(minutes) * 60 * 1000};
}

StatusOr<AbsoluteTime> ParseAbsTime(std::string_view s) {
  // Expected: yyyy-mm-ddThh:mm:ss(.uuu)?[+-]zz(:mm)? or ...Z
  if (s.size() < 19) {
    return Status(StatusCode::kParseError,
                  "Invalid TIME: too short: " + std::string(s));
  }

  // Parse date part.
  int year = 0, month = 0, day = 0;
  int hour = 0, minute = 0, second = 0;
  int frac_ms = 0;

  auto parse_int = [](std::string_view sv, int& out) -> bool {
    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), out);
    return ec == std::errc() && ptr == sv.data() + sv.size();
  };

  if (!parse_int(s.substr(0, 4), year) || s[4] != '-' ||
      !parse_int(s.substr(5, 2), month) || s[7] != '-' ||
      !parse_int(s.substr(8, 2), day) || s[10] != 'T' ||
      !parse_int(s.substr(11, 2), hour) || s[13] != ':' ||
      !parse_int(s.substr(14, 2), minute) || s[16] != ':' ||
      !parse_int(s.substr(17, 2), second)) {
    return Status(StatusCode::kParseError,
                  "Invalid TIME format: " + std::string(s));
  }

  size_t pos = 19;

  // Optional fractional seconds.
  if (pos < s.size() && s[pos] == '.') {
    pos++;
    size_t frac_start = pos;
    while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) {
      pos++;
    }
    auto frac_str = s.substr(frac_start, pos - frac_start);
    if (frac_str.empty()) {
      return Status(StatusCode::kParseError,
                    "Invalid TIME: empty fraction: " + std::string(s));
    }
    int frac_val = 0;
    if (!parse_int(frac_str, frac_val)) {
      return Status(StatusCode::kParseError,
                    "Invalid TIME fraction: " + std::string(s));
    }
    if (frac_str.size() == 1) {
      frac_ms = frac_val * 100;
    } else if (frac_str.size() == 2) {
      frac_ms = frac_val * 10;
    } else if (frac_str.size() == 3) {
      frac_ms = frac_val;
    } else {
      // Truncate to milliseconds.
      std::string ms_str(frac_str.substr(0, 3));
      parse_int(ms_str, frac_ms);
    }
  }

  // Parse timezone.
  int tz_offset_minutes = 0;
  if (pos < s.size()) {
    if (s[pos] == 'Z') {
      tz_offset_minutes = 0;
      pos++;
    } else if (s[pos] == '+' || s[pos] == '-') {
      bool tz_neg = (s[pos] == '-');
      pos++;
      if (pos + 2 > s.size()) {
        return Status(StatusCode::kParseError,
                      "Invalid TIME timezone: " + std::string(s));
      }
      int tz_h = 0;
      if (!parse_int(s.substr(pos, 2), tz_h)) {
        return Status(StatusCode::kParseError,
                      "Invalid TIME timezone hours: " + std::string(s));
      }
      pos += 2;
      int tz_m = 0;
      if (pos < s.size() && s[pos] == ':') {
        pos++;
        if (pos + 2 > s.size()) {
          return Status(StatusCode::kParseError,
                        "Invalid TIME timezone minutes: " + std::string(s));
        }
        if (!parse_int(s.substr(pos, 2), tz_m)) {
          return Status(StatusCode::kParseError,
                        "Invalid TIME timezone minutes: " + std::string(s));
        }
        pos += 2;
      }
      tz_offset_minutes = (tz_h * 60 + tz_m) * (tz_neg ? -1 : 1);
    }
  }

  // Convert to epoch milliseconds using a simplified calculation.
  // days since epoch using civil calendar.
  auto civil_days = [](int y, int m, int d) -> int64_t {
    // Algorithm from Howard Hinnant's date library.
    y -= (m <= 2);
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = static_cast<unsigned>(y - era * 400);
    unsigned doy =
        (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
  };

  int64_t days = civil_days(year, month, day);
  int64_t epoch_sec =
      days * 86400 + hour * 3600 + minute * 60 + second;
  // Adjust for timezone.
  epoch_sec -= static_cast<int64_t>(tz_offset_minutes) * 60;
  int64_t epoch_ms = epoch_sec * 1000 + frac_ms;

  return AbsoluteTime{epoch_ms, tz_offset_minutes};
}

}  // namespace internal
}  // namespace ccsparser
