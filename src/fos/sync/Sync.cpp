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

#include "fos/core/Storage.h"
#include "fos/net/Net.h"
#include "fos/settings/FosSettings.h"

namespace fos::sync {
namespace {

std::mutex jobsMutex;
std::deque<AppId> jobs;  // AppId::Count = everything
TaskHandle_t worker = nullptr;
std::atomic<bool> running{false};
std::atomic<uint32_t> gen{0};
Fetcher fetchers[APP_COUNT] = {};
std::atomic<bool> appOk[APP_COUNT];

void runJob(const AppId app) {
  for (int i = 0; i < APP_COUNT; ++i) {
    if (!fetchers[i] || (app != AppId::Count && static_cast<int>(app) != i)) continue;
    appOk[i] = fetchers[i]();
  }
}

void workerLoop(void*) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    // Leave Wi-Fi as it was found (the web server or a live app may be using it).
    const bool wasConnected = WiFi.status() == WL_CONNECTED;
    for (;;) {
      AppId app;
      {
        std::lock_guard<std::mutex> lock(jobsMutex);
        if (jobs.empty()) break;
        app = jobs.front();
        jobs.pop_front();
      }
      running = true;
      runJob(app);
      running = false;
      gen++;
    }
    if (!wasConnected && WiFi.status() == WL_CONNECTED) net::wifiOff();
  }
}

}  // namespace

void begin() {
  if (!worker) xTaskCreate(workerLoop, "fos-sync", 12 * 1024, nullptr, 1, &worker);
  if (!settings().batterySaver) request();
}

void request(const AppId app) {
  {
    std::lock_guard<std::mutex> lock(jobsMutex);
    // An "everything" job already queued covers this one.
    for (const AppId queued : jobs) {
      if (queued == AppId::Count || queued == app) return;
    }
    jobs.push_back(app);
  }
  if (worker) xTaskNotify(worker, 1, eIncrement);
}

bool busy() {
  if (running) return true;
  std::lock_guard<std::mutex> lock(jobsMutex);
  return !jobs.empty();
}

uint32_t generation() { return gen.load(); }

bool appOnline(const AppId app) {
  const int i = static_cast<int>(app);
  return i >= 0 && i < APP_COUNT && appOk[i].load();
}

void setFetcher(const AppId app, const Fetcher fetcher) {
  const int i = static_cast<int>(app);
  if (i >= 0 && i < APP_COUNT) fetchers[i] = fetcher;
}

std::string cachePath(const AppId app, const char* name) { return std::string(appFolder(app)) + "/" + name + ".json"; }

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

}  // namespace fos::sync
