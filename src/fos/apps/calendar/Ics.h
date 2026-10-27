#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fos::calendar {

// Streaming iCalendar (.ics) reader for the agenda: feed() the file in chunks
// as it downloads, then agendaJson() gives the events that start in
// [fromDay, fromDay + days) in the backend's agenda format
// ({"from", "events": [{"title", "location", "start", "end", "allDay"}]}).
//
// Supports VEVENT with DTSTART/DTEND/DURATION (dates, UTC times and TZID
// times, which are read as local time), RRULE (DAILY, WEEKLY with BYDAY,
// MONTHLY by day of month, YEARLY; INTERVAL, COUNT, UNTIL), EXDATE, moved
// occurrences (RECURRENCE-ID) and cancelled events. Only events inside the
// window are kept, so memory stays small however long the calendar is.
class IcsReader {
 public:
  // fromDay is "YYYY-MM-DD" (local).
  IcsReader(const std::string& fromDay, int days);

  void feed(const uint8_t* data, size_t len);
  void finish();
  int eventCount() const { return static_cast<int>(events_.size()); }
  std::string agendaJson() const;

 private:
  struct Time {
    int64_t sec = 0;  // seconds since 1970-01-01 in local time ("naive")
    bool allDay = false;
    bool valid = false;
  };
  struct Out {
    std::string uid;
    int64_t start = 0;  // naive local seconds
    int64_t end = 0;
    bool allDay = false;
    std::string title;
    std::string location;
    int64_t original = 0;  // occurrence start before any move (for overrides)
    bool override = false;
  };
  struct Event {
    std::string uid;
    std::string summary;
    std::string location;
    Time start;
    Time end;
    int64_t durationSec = -1;
    std::string rrule;
    std::vector<int64_t> exdates;
    Time recurrenceId;
    bool cancelled = false;
  };

  void line(std::string text);
  void property(const std::string& name, const std::string& params, const std::string& value);
  void endEvent();
  void emit(const Event& e, int64_t start, int64_t length, int64_t original);
  static Time parseTime(const std::string& params, const std::string& value);
  static std::string unescape(const std::string& value);

  int64_t windowStart_ = 0;
  int64_t windowEnd_ = 0;
  std::string fromDay_;
  std::string pending_;  // current unfolded line
  std::string partial_;  // bytes since the last line break
  bool inEvent_ = false;
  int depth_ = 0;  // nested components inside the event (VALARM)
  Event event_;
  std::vector<Out> events_;
};

}  // namespace fos::calendar
