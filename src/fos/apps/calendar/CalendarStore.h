#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fos::calendar {

// The agenda the sync worker downloads into /fos/calendar/cache.json:
// {"fetched": unix, "data": {"from": "YYYY-MM-DD", "events": [{"id", "title",
// "location", "start", "end", "allDay"}]}}. Times are local ISO 8601
// ("2026-10-26T09:30"); all-day events carry only the date.

struct Event {
  std::string date;   // "YYYY-MM-DD"
  std::string start;  // "HH:MM", empty when all day
  std::string end;
  bool allDay = false;
  bool overnight = false;  // ends on a later day
  std::string title;
  std::string location;
};

// End of an event on its own day, for comparisons with "HH:MM" ("24:00" when
// it runs past midnight).
std::string endOnDay(const Event& e);

struct Agenda {
  bool loaded = false;
  uint32_t fetched = 0;       // unix time of the download
  std::vector<Event> events;  // sorted by date, all-day first, then start time
};

bool loadAgenda(Agenda& out);

// The event that matters now: the first timed event today that has not ended,
// else the first event of a later day, else one of today's all-day events.
// Null when there is none. now is "YYYY-MM-DDTHH:MM".
const Event* nextEvent(const Agenda& agenda, const std::string& now);

// Local time as "YYYY-MM-DDTHH:MM", empty if the clock was never set.
std::string nowStamp();

}  // namespace fos::calendar
