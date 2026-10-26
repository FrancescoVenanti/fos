#pragma once

#include <cstdint>
#include <string>

#include "fos/apps/AppRegistry.h"

namespace fos::sync {

// Sync runs on a background task. Each run first sends what is queued
// (Budgeteer entries, read-later status), then downloads fresh data and keeps
// it on the microSD as /fos/<app>/cache*.json for offline use.

// Starts the worker; with auto-sync on, also queues a full sync (boot).
void begin();

// Queues a sync of one app (when it opens) or of everything (AppId::Count).
void request(AppId app = AppId::Count);
// Settings > Sync now.
bool syncNow();

bool busy();
// Increments after every finished run, so open screens can reload caches.
uint32_t generation();
// Outcome of the last run: true if the backend answered.
bool lastRunOnline();
// Unix time of the last complete sync, 0 = never.
uint32_t lastSyncTime();

// Apps that send data register a pusher; it runs before any download and
// returns false if the backend could not be reached.
using Pusher = bool (*)();
void setPusher(AppId app, Pusher pusher);

// Cache file paths, e.g. cachePath(AppId::Calendar) -> /fos/calendar/cache.json.
std::string cachePath(AppId app, const char* name = "cache");
// Downloads a backend path into a cache file (worker task only, or a screen
// that accepts blocking). The file holds {"fetched": unix, "data": <body>}.
bool fetchToCache(const std::string& path, const std::string& cacheFile);
// Queues fetchToCache on the worker.
void requestFetch(const std::string& path, const std::string& cacheFile);

}  // namespace fos::sync
