#include "Sync.h"

#include <HalStorage.h>
#include <Logging.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <ctime>
#include <deque>
#include <mutex>

#include "Backend.h"
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

// What each app downloads (docs/PRODUCT.md, "Sync and configuration").
// Budgeteer's month and the terminal session are fetched by those apps.
constexpr Pull PULLS[] = {
    {AppId::Budgeteer, "/api/v1/budgeteer/categories", "cache-categories"},
    {AppId::Calendar, "/api/v1/calendar/agenda?days=7", "cache"},
    {AppId::ReadLater, "/api/v1/readlater/articles", "cache"},
    {AppId::Flashcards, "/api/v1/flashcards/decks", "cache"},
    {AppId::Puzzles, "/api/v1/puzzles/daily", "cache-daily"},
    {AppId::Calculator, "/api/v1/calculator/rates", "cache-rates"},
    {AppId::Wallet, "/api/v1/wallet/codes", "cache"},
    {AppId::LockScreen, "/api/v1/lockscreen/weather", "cache-weather"},
};

struct Job {
  AppId app;           // AppId::Count = everything
  std::string path;    // set for a single fetch
  std::string cache;
};

std::mutex jobsMutex;
std::deque<Job> jobs;
TaskHandle_t worker = nullptr;
std::atomic<bool> running{false};
std::atomic<uint32_t> gen{0};
std::atomic<bool> online{false};
Pusher pushers[APP_COUNT] = {};

bool pushAll(const AppId only) {
  for (int i = 0; i < APP_COUNT; ++i) {
    if (!pushers[i]) continue;
    if (only != AppId::Count && static_cast<int>(only) != i) continue;
    if (!pushers[i]()) return false;
  }
  return true;
}

void runJob(const Job& job) {
  if (!net::ensureWifi()) {
    LOG_INF("SYNC", "No Wi-Fi; staying offline");
    online = false;
    return;
  }
  if (!net::backendOnline()) {
    LOG_INF("SYNC", "Backend unreachable; staying offline");
    online = false;
    return;
  }
  online = true;
  if (!job.path.empty()) {
    fetchToCache(job.path, job.cache);
    return;
  }
  pushAll(job.app);
  bool all = true;
  for (const Pull& pull : PULLS) {
    if (job.app != AppId::Count && pull.app != job.app) continue;
    all = fetchToCache(pull.path, cachePath(pull.app, pull.cacheName)) && all;
  }
  if (all && job.app == AppId::Count) {
    settings().lastSync = static_cast<uint32_t>(std::time(nullptr));
    settings().save();
  }
}

void workerLoop(void*) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
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

void setPusher(const AppId app, const Pusher pusher) {
  const int i = static_cast<int>(app);
  if (i >= 0 && i < APP_COUNT) pushers[i] = pusher;
}

std::string cachePath(const AppId app, const char* name) {
  return std::string(appFolder(app)) + "/" + name + ".json";
}

bool fetchToCache(const std::string& path, const std::string& cacheFile) {
  const net::Response r = net::backendGet(path);
  if (!r.ok()) {
    LOG_INF("SYNC", "GET %s -> %d", path.c_str(), r.status);
    return false;
  }
  const size_t slash = cacheFile.rfind('/');
  if (slash != std::string::npos) Storage.mkdir(cacheFile.substr(0, slash).c_str());
  // Wrap the body untouched: {"fetched": unix, "data": <body>}.
  std::string doc = "{\"fetched\":" + std::to_string(static_cast<long long>(std::time(nullptr))) + ",\"data\":";
  doc += r.body;
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
