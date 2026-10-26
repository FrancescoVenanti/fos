#include "BudgetStore.h"

#include <ArduinoJson.h>
#include <Logging.h>
#include <esp_random.h>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <mutex>

#include "fos/core/Json.h"
#include "fos/core/Status.h"
#include "fos/sync/Backend.h"
#include "fos/sync/Sync.h"

namespace fos::budget {
namespace {

constexpr char QUEUE_FILE[] = "/fos/budgeteer/queue.json";
constexpr char RECENT_FILE[] = "/fos/budgeteer/recent.json";
constexpr size_t RECENT_PER_CATEGORY = 5;

// The queue is appended on the main task and drained by the sync worker.
std::mutex queueMutex;

std::string monthCache(const char* ym) { return sync::cachePath(AppId::Budgeteer, "cache-month-") + ym; }

void entryToJson(const Entry& e, JsonObject out) {
  out["clientId"] = e.clientId;
  out["type"] = e.income ? "income" : "expense";
  out["amount"] = e.amount;
  out["category"] = e.category;
  out["description"] = e.description;
  out["date"] = e.date;
}

Entry entryFromJson(JsonObjectConst in) {
  Entry e;
  e.clientId = in["clientId"] | "";
  e.income = strcmp(in["type"] | "expense", "income") == 0;
  e.amount = in["amount"] | 0LL;
  e.category = in["category"] | "";
  e.description = in["description"] | "";
  e.date = in["date"] | "";
  return e;
}

// Callers hold queueMutex.
std::vector<Entry> readQueue() {
  std::vector<Entry> out;
  JsonDocument doc;
  if (!loadJson(QUEUE_FILE, doc)) return out;
  JsonArrayConst entries = doc["entries"];
  out.reserve(entries.size());
  for (JsonObjectConst e : entries) out.push_back(entryFromJson(e));
  return out;
}

bool writeQueue(const std::vector<Entry>& entries) {
  JsonDocument doc;
  JsonArray arr = doc["entries"].to<JsonArray>();
  for (const Entry& e : entries) entryToJson(e, arr.add<JsonObject>());
  const bool ok = saveJson(QUEUE_FILE, doc);
  setPendingCount(static_cast<int>(entries.size()));
  return ok;
}

// Sends every queued entry. An entry leaves the queue only once the backend
// confirms it (a repeat is confirmed as a duplicate), so a lost reply can at
// worst make the backend see it twice, and it drops the repeat by clientId.
bool push() {
  std::vector<Entry> pending;
  {
    std::lock_guard<std::mutex> lock(queueMutex);
    pending = readQueue();
  }
  if (pending.empty()) return true;

  std::vector<std::string> confirmed;
  bool reachable = true;
  for (const Entry& e : pending) {
    JsonDocument doc;
    entryToJson(e, doc.to<JsonObject>());
    std::string body;
    serializeJson(doc, body);
    const net::Response r = net::backendPost("/api/v1/budgeteer/transactions", body);
    if (r.ok()) {
      confirmed.push_back(e.clientId);
    } else if (r.status < 0 || r.status >= 500) {
      LOG_INF("BUDGET", "Send failed (%d); keeping %d queued", r.status, static_cast<int>(pending.size()));
      reachable = false;
      break;
    } else {
      // Rejected (4xx): keep it so nothing typed is lost, and go on.
      LOG_ERR("BUDGET", "Backend rejected %s (%d)", e.clientId.c_str(), r.status);
    }
  }
  if (!confirmed.empty()) {
    std::lock_guard<std::mutex> lock(queueMutex);
    // Re-read: entries may have been added while sending.
    std::vector<Entry> now = readQueue();
    now.erase(std::remove_if(now.begin(), now.end(),
                             [&](const Entry& e) {
                               return std::find(confirmed.begin(), confirmed.end(), e.clientId) != confirmed.end();
                             }),
              now.end());
    writeQueue(now);
    LOG_INF("BUDGET", "Sent %d entries, %d left", static_cast<int>(confirmed.size()), static_cast<int>(now.size()));
  }
  return reachable;
}

void remember(const Entry& e) {
  if (e.description.empty()) return;
  JsonDocument doc;
  loadJson(RECENT_FILE, doc);
  JsonArray old = doc[e.category].as<JsonArray>();
  std::vector<std::string> list{e.description};
  for (JsonVariantConst v : old) {
    const char* d = v | "";
    if (*d && e.description != d && list.size() < RECENT_PER_CATEGORY) list.emplace_back(d);
  }
  JsonArray arr = doc[e.category].to<JsonArray>();
  for (const std::string& d : list) arr.add(d);
  saveJson(RECENT_FILE, doc);
}

}  // namespace

void init() {
  sync::setPusher(AppId::Budgeteer, push);
  setPendingCount(queuedCount());
}

bool loadCategories(std::vector<Category>& out) {
  out.clear();
  JsonDocument doc;
  if (!loadJson(sync::cachePath(AppId::Budgeteer, "cache-categories").c_str(), doc)) return false;
  JsonObjectConst data = doc["data"];
  for (const char* kind : {"expense", "income"}) {
    JsonArrayConst list = data[kind];
    for (JsonObjectConst c : list) {
      Category cat;
      cat.id = c["id"] | "";
      cat.name = c["name"] | cat.id.c_str();
      cat.income = strcmp(kind, "income") == 0;
      cat.excluded = c["excludeFromBalance"] | false;
      if (!cat.id.empty()) out.push_back(std::move(cat));
    }
  }
  return !out.empty();
}

bool loadMonth(const char* ym, Month& out) {
  out = Month();
  JsonDocument doc;
  if (!loadJson((monthCache(ym) + ".json").c_str(), doc)) return false;
  JsonObjectConst data = doc["data"];
  JsonObjectConst totals = data["totals"];
  out.income = totals["income"] | 0LL;
  out.expense = totals["expense"] | 0LL;
  out.balance = totals["balance"] | (out.income - out.expense);
  JsonArrayConst budgets = data["budgets"];
  out.budgets.reserve(budgets.size());
  for (JsonObjectConst b : budgets) {
    out.budgets.push_back({b["category"] | "", b["budget"] | 0LL, b["spent"] | 0LL});
  }
  JsonArrayConst txs = data["transactions"];
  out.transactions.reserve(txs.size());
  for (JsonObjectConst t : txs) {
    Entry e = entryFromJson(t);
    e.clientId = t["id"] | "";
    out.transactions.push_back(std::move(e));
  }
  out.loaded = true;
  return true;
}

void requestMonth(const char* ym) {
  sync::requestFetch(std::string("/api/v1/budgeteer/month?month=") + ym, monthCache(ym) + ".json");
}

bool enqueue(Entry entry) {
  char id[40];
  snprintf(id, sizeof(id), "fos-%lx-%08lx", static_cast<unsigned long>(std::time(nullptr)),
           static_cast<unsigned long>(esp_random()));
  entry.clientId = id;
  remember(entry);
  std::lock_guard<std::mutex> lock(queueMutex);
  std::vector<Entry> entries = readQueue();
  entries.push_back(std::move(entry));
  return writeQueue(entries);
}

std::vector<Entry> queued() {
  std::lock_guard<std::mutex> lock(queueMutex);
  return readQueue();
}

int queuedCount() {
  std::lock_guard<std::mutex> lock(queueMutex);
  return static_cast<int>(readQueue().size());
}

std::vector<std::string> recentDescriptions(const std::string& category, const Month* month, const size_t max) {
  std::vector<std::string> out;
  const auto add = [&](const std::string& d) {
    if (!d.empty() && out.size() < max && std::find(out.begin(), out.end(), d) == out.end()) out.push_back(d);
  };
  JsonDocument doc;
  if (loadJson(RECENT_FILE, doc)) {
    for (JsonVariantConst v : doc[category].as<JsonArrayConst>()) add(v | "");
  }
  if (month) {
    for (const Entry& e : month->transactions) {
      if (e.category == category) add(e.description);
    }
  }
  return out;
}

void formatYm(const int year, const int mon, char* out, const size_t size) {
  snprintf(out, size, "%04d-%02d", year, mon + 1);
}

}  // namespace fos::budget
