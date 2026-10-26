#include "CalendarStore.h"

#include <ArduinoJson.h>

#include <algorithm>
#include <cstdio>

#include "fos/core/Json.h"
#include "fos/core/Status.h"
#include "fos/sync/Sync.h"

namespace fos::calendar {
namespace {

// "2026-10-26T09:30" -> date "2026-10-26", time "09:30" (time empty for a date).
void split(const char* iso, std::string& date, std::string& time) {
  const std::string s(iso ? iso : "");
  date = s.substr(0, std::min<size_t>(10, s.size()));
  time = s.size() >= 16 && s[10] == 'T' ? s.substr(11, 5) : std::string();
}

}  // namespace

std::string endOnDay(const Event& e) {
  if (e.overnight) return "24:00";
  return e.end.empty() ? e.start : e.end;
}

bool loadAgenda(Agenda& out) {
  out = Agenda();
  JsonDocument doc;
  if (!loadJson(sync::cachePath(AppId::Calendar).c_str(), doc)) return false;
  out.fetched = doc["fetched"] | 0u;
  JsonArrayConst events = doc["data"]["events"];
  out.events.reserve(events.size());
  for (JsonObjectConst e : events) {
    Event ev;
    std::string endDate;
    split(e["start"] | "", ev.date, ev.start);
    split(e["end"] | "", endDate, ev.end);
    ev.allDay = (e["allDay"] | false) || ev.start.empty();
    if (ev.allDay) ev.start.clear(), ev.end.clear();
    ev.overnight = !ev.allDay && !endDate.empty() && endDate > ev.date;
    ev.title = e["title"] | "";
    ev.location = e["location"] | "";
    if (ev.date.size() == 10) out.events.push_back(std::move(ev));
  }
  std::stable_sort(out.events.begin(), out.events.end(), [](const Event& a, const Event& b) {
    if (a.date != b.date) return a.date < b.date;
    if (a.allDay != b.allDay) return a.allDay;
    return a.start < b.start;
  });
  out.loaded = true;
  return true;
}

const Event* nextEvent(const Agenda& agenda, const std::string& now) {
  if (now.size() < 16) return nullptr;
  const std::string today = now.substr(0, 10);
  const std::string time = now.substr(11, 5);
  // A timed event still running today beats today's all-day ones, which are
  // only the answer when nothing else is left.
  const Event* allDayToday = nullptr;
  for (const Event& e : agenda.events) {
    if (e.date < today) continue;
    if (e.date > today) return &e;
    if (e.allDay) {
      if (!allDayToday) allDayToday = &e;
      continue;
    }
    if (endOnDay(e) > time) return &e;
  }
  return allDayToday;
}

std::string nowStamp() {
  std::tm t{};
  if (!localNow(t)) return {};
  char out[20];
  snprintf(out, sizeof(out), "%04d-%02d-%02dT%02d:%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour,
           t.tm_min);
  return out;
}

}  // namespace fos::calendar
