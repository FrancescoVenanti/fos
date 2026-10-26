#include "TodoStore.h"

#include <ArduinoJson.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "fos/core/Json.h"
#include "fos/core/Status.h"

namespace fos::todo {
namespace {

constexpr char DATA_FILE[] = "/fos/todo/todo.json";

Data store;
bool loaded = false;

bool parse(const std::string& date, std::tm& out) {
  int y = 0;
  int m = 0;
  int d = 0;
  if (sscanf(date.c_str(), "%d-%d-%d", &y, &m, &d) != 3 || m < 1 || m > 12 || d < 1 || d > 31) return false;
  out = std::tm{};
  out.tm_year = y - 1900;
  out.tm_mon = m - 1;
  out.tm_mday = d;
  out.tm_hour = 12;  // away from DST edges
  out.tm_isdst = -1;
  std::mktime(&out);
  return true;
}

std::string format(const std::tm& t) {
  char out[16];
  snprintf(out, sizeof(out), "%04d-%02d-%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
  return out;
}

int daysInMonth(const int year, const int mon) {
  static constexpr int DAYS[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
  return mon == 1 && leap ? 29 : DAYS[mon];
}

// One step of a repeat; monthly keeps the day, clamped to the month's length.
std::string step(const std::string& date, const Repeat repeat, const int anchorDay) {
  switch (repeat) {
    case Repeat::Daily:
      return addDays(date, 1);
    case Repeat::Weekly:
      return addDays(date, 7);
    case Repeat::Monthly: {
      std::tm t{};
      if (!parse(date, t)) return date;
      int year = t.tm_year + 1900;
      int mon = t.tm_mon + 1;
      if (mon == 12) {
        mon = 0;
        ++year;
      }
      char out[16];
      snprintf(out, sizeof(out), "%04d-%02d-%02d", year, mon + 1, std::min(anchorDay, daysInMonth(year, mon)));
      return out;
    }
    case Repeat::None:
      break;
  }
  return date;
}

const char* repeatKey(const Repeat r) {
  switch (r) {
    case Repeat::Daily:
      return "daily";
    case Repeat::Weekly:
      return "weekly";
    case Repeat::Monthly:
      return "monthly";
    case Repeat::None:
      break;
  }
  return "none";
}

Repeat repeatFromKey(const char* key) {
  if (strcmp(key, "daily") == 0) return Repeat::Daily;
  if (strcmp(key, "weekly") == 0) return Repeat::Weekly;
  if (strcmp(key, "monthly") == 0) return Repeat::Monthly;
  return Repeat::None;
}

void load() {
  loaded = true;
  store = Data();
  JsonDocument doc;
  if (loadJson(DATA_FILE, doc)) {
    store.nextId = doc["nextId"] | 1u;
    for (JsonObjectConst l : doc["lists"].as<JsonArrayConst>()) {
      store.lists.push_back({l["id"] | 0u, l["name"] | ""});
    }
    store.tasks.reserve(doc["tasks"].size());
    for (JsonObjectConst t : doc["tasks"].as<JsonArrayConst>()) {
      Task task;
      task.id = t["id"] | 0u;
      task.list = t["list"] | 0u;
      task.title = t["title"] | "";
      task.due = t["due"] | "";
      task.repeat = repeatFromKey(t["repeat"] | "none");
      task.done = t["done"] | false;
      task.doneOn = t["doneOn"] | "";
      store.tasks.push_back(std::move(task));
    }
  }
  if (store.lists.empty()) store.lists.push_back({store.nextId++, "Inbox"});
}

}  // namespace

const List* Data::list(const uint32_t id) const {
  const auto it = std::find_if(lists.begin(), lists.end(), [id](const List& l) { return l.id == id; });
  return it == lists.end() ? nullptr : &*it;
}

Task* Data::task(const uint32_t id) {
  const auto it = std::find_if(tasks.begin(), tasks.end(), [id](const Task& t) { return t.id == id; });
  return it == tasks.end() ? nullptr : &*it;
}

uint32_t Data::addTask(const uint32_t listId, std::string title) {
  Task t;
  t.id = nextId++;
  t.list = list(listId) ? listId : lists.front().id;
  t.title = std::move(title);
  tasks.push_back(std::move(t));
  return tasks.back().id;
}

bool Data::addList(std::string name) {
  if (lists.size() >= MAX_LISTS || name.empty()) return false;
  lists.push_back({nextId++, std::move(name)});
  return true;
}

bool Data::removeList(const uint32_t id) {
  if (lists.size() <= 1 || !list(id)) return false;
  tasks.erase(std::remove_if(tasks.begin(), tasks.end(), [id](const Task& t) { return t.list == id; }), tasks.end());
  lists.erase(std::remove_if(lists.begin(), lists.end(), [id](const List& l) { return l.id == id; }), lists.end());
  return true;
}

void Data::removeTask(const uint32_t id) {
  tasks.erase(std::remove_if(tasks.begin(), tasks.end(), [id](const Task& t) { return t.id == id; }), tasks.end());
}

void Data::toggleDone(const uint32_t id) {
  Task* t = task(id);
  if (!t) return;
  if (t->done) {
    t->done = false;
    t->doneOn.clear();
  } else if (t->repeat != Repeat::None) {
    t->due = nextOccurrence(t->due, t->repeat);
    t->doneOn = today();
  } else {
    t->done = true;
    t->doneOn = today();
  }
}

int Data::clearDone(const uint32_t listId) {
  const size_t before = tasks.size();
  tasks.erase(std::remove_if(tasks.begin(), tasks.end(),
                             [listId](const Task& t) { return t.done && (listId == 0 || t.list == listId); }),
              tasks.end());
  return static_cast<int>(before - tasks.size());
}

Data& data() {
  if (!loaded) load();
  return store;
}

bool save() {
  const Data& d = data();
  JsonDocument doc;
  doc["nextId"] = d.nextId;
  JsonArray lists = doc["lists"].to<JsonArray>();
  for (const List& l : d.lists) {
    JsonObject o = lists.add<JsonObject>();
    o["id"] = l.id;
    o["name"] = l.name;
  }
  JsonArray tasks = doc["tasks"].to<JsonArray>();
  for (const Task& t : d.tasks) {
    JsonObject o = tasks.add<JsonObject>();
    o["id"] = t.id;
    o["list"] = t.list;
    o["title"] = t.title;
    if (!t.due.empty()) o["due"] = t.due;
    if (t.repeat != Repeat::None) o["repeat"] = repeatKey(t.repeat);
    if (t.done) o["done"] = true;
    if (!t.doneOn.empty()) o["doneOn"] = t.doneOn;
  }
  const bool ok = saveJson(DATA_FILE, doc);
  if (!ok) LOG_ERR("TODO", "Cannot write %s", DATA_FILE);
  return ok;
}

std::string today() {
  std::tm t{};
  if (!localNow(t)) return {};
  return format(t);
}

std::string addDays(const std::string& date, const int days) {
  std::tm t{};
  if (!parse(date, t)) return date;
  t.tm_mday += days;
  std::mktime(&t);
  return format(t);
}

std::string nextOccurrence(const std::string& due, const Repeat repeat) {
  if (repeat == Repeat::None) return due;
  const std::string now = today();
  std::string next = due.empty() ? now : due;
  if (next.empty()) return due;  // no clock and no date
  std::tm anchor{};
  const int day = parse(next, anchor) ? anchor.tm_mday : 1;
  // At least one step, then on until the date is after today.
  do {
    next = step(next, repeat, day);
  } while (!now.empty() && next <= now);
  return next;
}

std::string describeDate(const std::string& date) {
  std::tm t{};
  if (!parse(date, t)) return date;
  const std::string now = today();
  if (!now.empty()) {
    if (date == now) return "Today";
    if (date == addDays(now, 1)) return "Tomorrow";
    if (date == addDays(now, -1)) return "Yesterday";
  }
  char out[24];
  snprintf(out, sizeof(out), "%.3s %d %.3s", weekdayName(t.tm_wday), t.tm_mday, monthName(t.tm_mon));
  // The year only when it is not this year.
  if (now.size() >= 4 && date.compare(0, 4, now, 0, 4) != 0) {
    const size_t len = strlen(out);
    snprintf(out + len, sizeof(out) - len, " %d", t.tm_year + 1900);
  }
  return out;
}

const char* repeatName(const Repeat repeat) {
  switch (repeat) {
    case Repeat::Daily:
      return "Daily";
    case Repeat::Weekly:
      return "Weekly";
    case Repeat::Monthly:
      return "Monthly";
    case Repeat::None:
      break;
  }
  return "Never";
}

}  // namespace fos::todo
