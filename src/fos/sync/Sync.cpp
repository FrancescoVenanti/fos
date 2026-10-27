#include "Sync.h"

#include <HalStorage.h>
#include <Logging.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <ctime>
#include <deque>
#include <mutex>

#include "Backend.h"
#include "fos/core/Json.h"
#include "fos/core/Status.h"
#include "fos/core/Storage.h"
#include "fos/settings/FosSettings.h"

namespace fos::sync {
namespace {

struct Pull {
  AppId app;
  const char* path;
  const char* cacheName;
};

// What the apps without a fetcher download from the backend (docs/PRODUCT.md,
// "Sync and configuration"). Budgeteer, Calendar, Calculator and the lock
// screen register fetchers with direct sources (Direct.cpp, BudgetStore.cpp);
// the terminal session is fetched live by its app.
constexpr Pull PULLS[] = {
    {AppId::ReadLater, "/api/v1/readlater/articles", "cache"},
    {AppId::Flashcards, "/api/v1/flashcards/decks", "cache"},
    {AppId::Puzzles, "/api/v1/puzzles/daily", "cache-daily"},
    {AppId::Wallet, "/api/v1/wallet/codes", "cache"},
};

struct Job {
  AppId app;         // AppId::Count = everything
  std::string path;  // set for a single fetch
  std::string cache;
};

std::mutex jobsMutex;
std::deque<Job> jobs;
TaskHandle_t worker = nullptr;
std::atomic<bool> running{false};
std::atomic<uint32_t> gen{0};
std::atomic<bool> online{false};
std::atomic<uint32_t> lastOk{0};
Pusher pushers[APP_COUNT] = {};
Fetcher fetchers[APP_COUNT] = {};
std::atomic<bool> appOk[APP_COUNT];

// Kept apart from /fos/settings.json so the worker never writes the settings
// object that screens edit on the main task.
constexpr char STATE_FILE[] = "/fos/sync/state.json";

void loadState() {
  JsonDocument doc;
  if (loadJson(STATE_FILE, doc)) lastOk = doc["lastSync"] | 0u;
}

void saveState() {
  JsonDocument doc;
  doc["lastSync"] = lastOk.load();
  if (!saveJson(STATE_FILE, doc)) LOG_ERR("SYNC", "Cannot write %s", STATE_FILE);
}

bool pushAll(const AppId only, const bool backend) {
  bool all = true;
  for (int i = 0; i < APP_COUNT; ++i) {
    if (!pushers[i]) continue;
    if (only != AppId::Count && static_cast<int>(only) != i) continue;
    all = pushers[i](backend) && all;
  }
  return all;
}

void runJob(const Job& job) {
  if (!net::ensureWifi()) {
    LOG_INF("SYNC", "No Wi-Fi; staying offline");
    online = false;
    for (auto& ok : appOk) ok = false;
    return;
  }
  const bool backend = net::backendOnline();
  if (!backend) LOG_INF("SYNC", "Backend unreachable or not set; using direct sources");
  if (!job.path.empty()) {
    online = backend && fetchToCache(job.path, job.cache);
    return;
  }
  pushAll(job.app, backend);
  bool any = backend;
  bool all = backend;
  for (int i = 0; i < APP_COUNT; ++i) {
    const AppId app = static_cast<AppId>(i);
    if (job.app != AppId::Count && job.app != app) continue;
    bool ok;
    if (fetchers[i]) {
      ok = fetchers[i](backend);
    } else {
      bool hasPull = false;
      ok = backend;
      for (const Pull& pull : PULLS) {
        if (pull.app != app) continue;
        hasPull = true;
        if (backend) ok = fetchToCache(pull.path, cachePath(pull.app, pull.cacheName)) && ok;
      }
      if (!hasPull) continue;
    }
    appOk[i] = ok;
    any = any || ok;
    all = all && ok;
  }
  online = any;
  if (all && job.app == AppId::Count) {
    lastOk = static_cast<uint32_t>(std::time(nullptr));
    saveState();
  }
}

void workerLoop(void*) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    // Wi-Fi is only for syncing: leave it as it was found (the web server or
    // a live app may be using it).
    const bool wasConnected = WiFi.status() == WL_CONNECTED;
    for (;;) {
      Job job;
      {
        std::lock_guard<std::mutex> lock(jobsMutex);
        if (jobs.empty()) break;
        job = jobs.front();
        jobs.pop_front();
      }
      running = true;
      setSyncState(SyncState::Syncing);
      runJob(job);
      setSyncState(online ? SyncState::Idle : SyncState::Offline);
      running = false;
      gen++;
    }
    if (!wasConnected) net::wifiOff();
  }
}

void enqueue(Job job) {
  {
    std::lock_guard<std::mutex> lock(jobsMutex);
    // A full sync supersedes queued per-app syncs of the same kind.
    for (const Job& queued : jobs) {
      if (queued.path.empty() && job.path.empty() && (queued.app == AppId::Count || queued.app == job.app)) return;
    }
    jobs.push_back(std::move(job));
  }
  if (worker) xTaskNotify(worker, 1, eIncrement);
}

}  // namespace

void begin() {
  loadState();
  if (!worker) xTaskCreate(workerLoop, "fos-sync", 12 * 1024, nullptr, 1, &worker);
  if (settings().autoSync && !settings().batterySaver) request();
}

void request(const AppId app) { enqueue({app, {}, {}}); }

bool syncNow() {
  request();
  return true;
}

bool busy() {
  if (running) return true;
  std::lock_guard<std::mutex> lock(jobsMutex);
  return !jobs.empty();
}

uint32_t generation() { return gen.load(); }

bool lastRunOnline() { return online.load(); }

bool appOnline(const AppId app) {
  const int i = static_cast<int>(app);
  return i >= 0 && i < APP_COUNT && appOk[i].load();
}

void setFetcher(const AppId app, const Fetcher fetcher) {
  const int i = static_cast<int>(app);
  if (i >= 0 && i < APP_COUNT) fetchers[i] = fetcher;
}

uint32_t lastSyncTime() { return lastOk.load(); }

void setPusher(const AppId app, const Pusher pusher) {
  const int i = static_cast<int>(app);
  if (i >= 0 && i < APP_COUNT) pushers[i] = pusher;
}

std::string cachePath(const AppId app, const char* name) { return std::string(appFolder(app)) + "/" + name + ".json"; }

bool fetchToCache(const std::string& path, const std::string& cacheFile) {
  const net::Response r = net::backendGet(path);
  if (!r.ok()) {
    LOG_INF("SYNC", "GET %s -> %d", path.c_str(), r.status);
    return false;
  }
  return writeCache(cacheFile, r.body);
}

bool writeCache(const std::string& cacheFile, const std::string& body) {
  const size_t slash = cacheFile.rfind('/');
  if (slash != std::string::npos) Storage.mkdir(cacheFile.substr(0, slash).c_str());
  // Wrap the body untouched: {"fetched": unix, "data": <body>}.
  std::string doc = "{\"fetched\":" + std::to_string(static_cast<long long>(std::time(nullptr))) + ",\"data\":";
  doc += body;
  doc += "}";
  const std::string tmp = cacheFile + ".tmp";
  if (!Storage.writeFile(tmp.c_str(), String(doc.c_str())) || !Storage.replaceFile(tmp.c_str(), cacheFile.c_str())) {
    LOG_ERR("SYNC", "Cannot write %s", cacheFile.c_str());
    return false;
  }
  return true;
}

void requestFetch(const std::string& path, const std::string& cacheFile) { enqueue({AppId::Count, path, cacheFile}); }

}  // namespace fos::sync
