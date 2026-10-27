#pragma once

#include <cstdint>
#include <string>

#include "fos/apps/AppRegistry.h"

namespace fos::sync {

// Sync runs on a background task. Each run first sends what is queued
// (Budgeteer entries, read-later status), then downloads fresh data and keeps
// it on the microSD as /fos/<app>/cache*.json for offline use.
//
// The backend is optional. An app with a direct source (Budgeteer's Google
// Sheet, Open-Meteo weather, Frankfurter rates, an iCal calendar) registers a
// fetcher that uses the backend when it answers and the direct source
// otherwise; the cache files look the same either way.

// Starts the worker; with auto-sync on, also queues a full sync (boot).
void begin();

// Queues a sync of one app (when it opens) or of everything (AppId::Count).
void request(AppId app = AppId::Count);
// Settings > Sync now.
bool syncNow();

bool busy();
// Increments after every finished run, so open screens can reload caches.
uint32_t generation();
// Outcome of the last run: true if the backend or any direct source answered.
bool lastRunOnline();
// Whether an app's last fetch succeeded (its data is fresh).
bool appOnline(AppId app);
// Unix time of the last complete sync, 0 = never.
uint32_t lastSyncTime();

// Apps that send data register a pusher; it runs before any download and
// returns false if its destination could not be reached. backend = the
// backend answered the health check in this run.
using Pusher = bool (*)(bool backend);
void setPusher(AppId app, Pusher pusher);

// Apps with a direct source register a fetcher, which replaces the backend
// downloads for that app. Returns true when the app's data was refreshed.
using Fetcher = bool (*)(bool backend);
void setFetcher(AppId app, Fetcher fetcher);

// Cache file paths, e.g. cachePath(AppId::Calendar) -> /fos/calendar/cache.json.
std::string cachePath(AppId app, const char* name = "cache");
// Downloads a backend path into a cache file (worker task only, or a screen
// that accepts blocking). The file holds {"fetched": unix, "data": <body>}.
bool fetchToCache(const std::string& path, const std::string& cacheFile);
// Queues fetchToCache on the worker.
void requestFetch(const std::string& path, const std::string& cacheFile);
// Writes {"fetched": now, "data": <body>} atomically (direct fetchers).
bool writeCache(const std::string& cacheFile, const std::string& body);

}  // namespace fos::sync
