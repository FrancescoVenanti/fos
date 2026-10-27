#include "Ics.h"

#include <ArduinoJson.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace fos::calendar {
namespace {

constexpr int64_t DAY = 86400;
constexpr int MAX_STEPS = 40000;  // recurrence expansion cap per event

// Days since 1970-01-01 of a civil date (Howard Hinnant's algorithm).
int64_t daysFromCivil(int y, const unsigned m, const unsigned d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

void civilFromDays(int64_t z, int& y, int& m, int& d) {
  z += 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int64_t yy = static_cast<int64_t>(yoe) + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
  m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
  y = static_cast<int>(yy + (m <= 2));
}

int weekday(const int64_t days) {  // 0 = Sunday; 1970-01-01 was a Thursday
  const int64_t w = (days + 4) % 7;
  return static_cast<int>(w < 0 ? w + 7 : w);
}

int daysInMonth(const int y, const int m) {
  static constexpr int DAYS[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
  return m == 2 && leap ? 29 : DAYS[m - 1];
}

std::string param(const std::string& params, const char* key) {
  const std::string k = std::string(key) + "=";
  size_t i = 0;
  while (i < params.size()) {
    const size_t end = params.find(';', i);
    const std::string p = params.substr(i, end == std::string::npos ? std::string::npos : end - i);
    if (p.compare(0, k.size(), k) == 0) return p.substr(k.size());
    if (end == std::string::npos) break;
    i = end + 1;
  }
  return {};
}

std::string ruleValue(const std::string& rule, const char* key) { return param(rule, key); }

// "PT1H30M", "P1D", "-PT15M" -> seconds.
int64_t parseDuration(const std::string& v) {
  int64_t total = 0;
  int64_t number = 0;
  bool time = false;
  const bool negative = !v.empty() && v[0] == '-';
  for (const char ch : v) {
    if (ch >= '0' && ch <= '9') {
      number = number * 10 + (ch - '0');
    } else if (ch == 'T') {
      time = true;
    } else {
      int64_t unit = 0;
      if (ch == 'W') unit = 7 * DAY;
      if (ch == 'D') unit = DAY;
      if (ch == 'H' && time) unit = 3600;
      if (ch == 'M' && time) unit = 60;
      if (ch == 'S' && time) unit = 1;
      total += number * unit;
      number = 0;
    }
  }
  return negative ? -total : total;
}

std::string formatNaive(const int64_t sec, const bool allDay) {
  int y = 0;
  int m = 0;
  int d = 0;
  const int64_t days = sec >= 0 ? sec / DAY : (sec - DAY + 1) / DAY;
  civilFromDays(days, y, m, d);
  char out[24];
  if (allDay) {
    snprintf(out, sizeof(out), "%04d-%02d-%02d", y, m, d);
  } else {
    const int64_t s = sec - days * DAY;
    snprintf(out, sizeof(out), "%04d-%02d-%02dT%02d:%02d", y, m, d, static_cast<int>(s / 3600),
             static_cast<int>(s / 60 % 60));
  }
  return out;
}

}  // namespace

IcsReader::IcsReader(const std::string& fromDay, const int days) : fromDay_(fromDay) {
  int y = 1970;
  int m = 1;
  int d = 1;
  sscanf(fromDay.c_str(), "%d-%d-%d", &y, &m, &d);
  windowStart_ = daysFromCivil(y, m, d) * DAY;
  windowEnd_ = windowStart_ + days * DAY;
}

IcsReader::Time IcsReader::parseTime(const std::string& params, const std::string& value) {
  Time t;
  int y = 0;
  int mo = 0;
  int d = 0;
  int hh = 0;
  int mi = 0;
  int ss = 0;
  if (value.size() < 8 || sscanf(value.c_str(), "%4d%2d%2d", &y, &mo, &d) != 3) return t;
  t.valid = true;
  if (param(params, "VALUE") == "DATE" || value.size() < 15 || value[8] != 'T') {
    t.allDay = true;
    t.sec = daysFromCivil(y, mo, d) * DAY;
    return t;
  }
  sscanf(value.c_str() + 9, "%2d%2d%2d", &hh, &mi, &ss);
  t.sec = daysFromCivil(y, mo, d) * DAY + hh * 3600 + mi * 60 + ss;
  if (value.back() == 'Z') {
    // UTC: convert to the device's local time.
    const std::time_t utc = static_cast<std::time_t>(t.sec);
    std::tm local{};
    localtime_r(&utc, &local);
    t.sec = daysFromCivil(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday) * DAY + local.tm_hour * 3600 +
            local.tm_min * 60 + local.tm_sec;
  }
  // TZID times are read as local time: right when the calendar and the
  // device share a time zone, which is the common case.
  return t;
}

std::string IcsReader::unescape(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '\\' && i + 1 < value.size()) {
      const char n = value[++i];
      out += n == 'n' || n == 'N' ? ' ' : n;
    } else {
      out += value[i];
    }
  }
  return out;
}

void IcsReader::feed(const uint8_t* data, const size_t len) {
  for (size_t i = 0; i < len; ++i) {
    const char ch = static_cast<char>(data[i]);
    if (ch == '\r') continue;
    if (ch != '\n') {
      if (partial_.size() < 4096) partial_ += ch;
      continue;
    }
    // A line that starts with a space or tab continues the previous one.
    if (!partial_.empty() && (partial_[0] == ' ' || partial_[0] == '\t')) {
      pending_ += partial_.substr(1);
    } else {
      if (!pending_.empty()) line(std::move(pending_));
      pending_ = std::move(partial_);
    }
    partial_.clear();
  }
}

void IcsReader::finish() {
  if (!partial_.empty()) {
    const uint8_t nl = '\n';
    feed(&nl, 1);
  }
  if (!pending_.empty()) line(std::move(pending_));
  pending_.clear();

  // Moved occurrences replace the original ones of their series.
  std::vector<std::pair<std::string, int64_t>> moved;
  for (const Out& o : events_) {
    if (o.override) moved.emplace_back(o.uid, o.original);
  }
  events_.erase(std::remove_if(events_.begin(), events_.end(),
                               [&](const Out& o) {
                                 if (o.override) return o.start < windowStart_ || o.start >= windowEnd_;
                                 return std::find(moved.begin(), moved.end(), std::make_pair(o.uid, o.original)) !=
                                        moved.end();
                               }),
                events_.end());
  std::sort(events_.begin(), events_.end(), [](const Out& a, const Out& b) { return a.start < b.start; });
}

void IcsReader::line(std::string text) {
  const size_t colon = text.find(':');
  if (colon == std::string::npos) return;
  std::string head = text.substr(0, colon);
  const std::string value = text.substr(colon + 1);
  const size_t semi = head.find(';');
  const std::string params = semi == std::string::npos ? std::string() : head.substr(semi + 1);
  std::string name = semi == std::string::npos ? head : head.substr(0, semi);
  for (char& ch : name) ch = static_cast<char>(toupper(static_cast<unsigned char>(ch)));

  if (name == "BEGIN") {
    if (value == "VEVENT" && !inEvent_) {
      inEvent_ = true;
      depth_ = 0;
      event_ = Event();
    } else if (inEvent_) {
      ++depth_;
    }
    return;
  }
  if (name == "END") {
    if (!inEvent_) return;
    if (depth_ > 0) {
      --depth_;
    } else if (value == "VEVENT") {
      endEvent();
      inEvent_ = false;
    }
    return;
  }
  if (inEvent_ && depth_ == 0) property(name, params, value);
}

void IcsReader::property(const std::string& name, const std::string& params, const std::string& value) {
  if (name == "UID") {
    event_.uid = value;
  } else if (name == "SUMMARY") {
    event_.summary = unescape(value);
  } else if (name == "LOCATION") {
    event_.location = unescape(value);
  } else if (name == "DTSTART") {
    event_.start = parseTime(params, value);
  } else if (name == "DTEND") {
    event_.end = parseTime(params, value);
  } else if (name == "DURATION") {
    event_.durationSec = parseDuration(value);
  } else if (name == "RRULE") {
    event_.rrule = value;
  } else if (name == "EXDATE") {
    size_t i = 0;
    while (i <= value.size()) {
      const size_t comma = value.find(',', i);
      const std::string one = value.substr(i, comma == std::string::npos ? std::string::npos : comma - i);
      const Time t = parseTime(params, one);
      if (t.valid) event_.exdates.push_back(t.sec);
      if (comma == std::string::npos) break;
      i = comma + 1;
    }
  } else if (name == "RECURRENCE-ID") {
    event_.recurrenceId = parseTime(params, value);
  } else if (name == "STATUS") {
    event_.cancelled = value == "CANCELLED";
  }
}

void IcsReader::emit(const Event& e, const int64_t start, const int64_t length, const int64_t original) {
  if (std::find(e.exdates.begin(), e.exdates.end(), start) != e.exdates.end()) return;
  Out o;
  o.uid = e.uid;
  o.start = start;
  o.end = start + length;
  o.allDay = e.start.allDay;
  o.title = e.summary;
  o.location = e.location;
  o.original = original;
  o.override = e.recurrenceId.valid;
  events_.push_back(std::move(o));
}

void IcsReader::endEvent() {
  const Event& e = event_;
  if (!e.start.valid) return;
  int64_t length = 0;
  if (e.end.valid) {
    length = e.end.sec - e.start.sec;
  } else if (e.durationSec >= 0) {
    length = e.durationSec;
  } else if (e.start.allDay) {
    length = DAY;
  }
  if (length < 0) length = 0;

  if (e.recurrenceId.valid) {
    // A moved or cancelled occurrence: it hides the original either way.
    Event shown = e;
    if (e.cancelled) shown.start.sec = windowEnd_ + DAY;  // emitted, then dropped as outside the window
    emit(shown, shown.start.sec, length, e.recurrenceId.sec);
    return;
  }
  if (e.cancelled) return;

  const auto inWindow = [this](const int64_t s) { return s >= windowStart_ && s < windowEnd_; };
  if (e.rrule.empty()) {
    if (inWindow(e.start.sec)) emit(e, e.start.sec, length, e.start.sec);
    return;
  }

  const std::string freq = ruleValue(e.rrule, "FREQ");
  const int interval = std::max(1, atoi(ruleValue(e.rrule, "INTERVAL").c_str()));
  const std::string countText = ruleValue(e.rrule, "COUNT");
  const long count = countText.empty() ? -1 : atol(countText.c_str());
  int64_t until = INT64_MAX;
  const std::string untilText = ruleValue(e.rrule, "UNTIL");
  if (!untilText.empty()) {
    const Time u = parseTime("", untilText);
    if (u.valid) until = u.allDay ? u.sec + DAY - 1 : u.sec;
  }
  const int64_t timeOfDay = e.start.sec - (e.start.sec / DAY) * DAY;
  const int64_t startDay = e.start.sec / DAY;

  long produced = 0;
  const auto occurrence = [&](const int64_t s) -> bool {  // false = stop
    if (s > until || s >= windowEnd_ || (count >= 0 && produced >= count)) return false;
    ++produced;
    if (inWindow(s)) emit(e, s, length, s);
    return true;
  };

  if (freq == "DAILY") {
    for (int64_t n = 0; n < MAX_STEPS; ++n) {
      if (!occurrence(e.start.sec + n * interval * DAY)) break;
    }
  } else if (freq == "WEEKLY") {
    // BYDAY=MO,WE: those weekdays of every interval-th week; else the start's weekday.
    bool days[7] = {};
    const std::string byday = ruleValue(e.rrule, "BYDAY");
    static constexpr const char* NAMES[] = {"SU", "MO", "TU", "WE", "TH", "FR", "SA"};
    for (int i = 0; i < 7; ++i) days[i] = byday.find(NAMES[i]) != std::string::npos;
    if (byday.empty()) days[weekday(startDay)] = true;
    // Weeks start on Monday (WKST default).
    const int64_t weekStart = startDay - (weekday(startDay) + 6) % 7;
    bool go = true;
    for (int64_t w = 0; go && w < MAX_STEPS; ++w) {
      const int64_t base = weekStart + w * 7 * interval;
      for (int i = 0; i < 7 && go; ++i) {
        const int64_t day = base + i;
        if (day < startDay || !days[weekday(day)]) continue;
        go = occurrence(day * DAY + timeOfDay);
      }
    }
  } else if (freq == "MONTHLY" || freq == "YEARLY") {
    int y = 0;
    int m = 0;
    int d = 0;
    civilFromDays(startDay, y, m, d);
    const int stepMonths = freq == "MONTHLY" ? interval : 12 * interval;
    for (int n = 0; n < MAX_STEPS / 10; ++n) {
      const int total = (m - 1) + n * stepMonths;
      const int yy = y + total / 12;
      const int mm = total % 12 + 1;
      if (d > daysInMonth(yy, mm)) continue;  // e.g. the 31st: skipped in short months, as RFC 5545
      if (!occurrence(daysFromCivil(yy, mm, d) * DAY + timeOfDay)) break;
    }
  }
}

std::string IcsReader::agendaJson() const {
  JsonDocument doc;
  doc["from"] = fromDay_;
  JsonArray events = doc["events"].to<JsonArray>();
  for (const Out& o : events_) {
    JsonObject e = events.add<JsonObject>();
    e["id"] = o.uid;
    e["title"] = o.title;
    e["location"] = o.location;
    e["start"] = formatNaive(o.start, o.allDay);
    e["end"] = formatNaive(o.allDay ? o.start : o.end, o.allDay);
    e["allDay"] = o.allDay;
  }
  std::string out;
  serializeJson(doc, out);
  return out;
}

}  // namespace fos::calendar
