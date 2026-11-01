#pragma once

#include <cstdint>
#include <string>

#include "fos/apps/AppRegistry.h"

namespace fos::sync {

// Background downloads of public data, on a worker task so screens never
// block on the network. FOs has no backend: each app that needs data
// registers a fetcher that calls its source directly (today only the lock
// screen weather, from Open-Meteo) and keeps the last copy on the microSD as
// /fos/<app>/cache*.json for offline use. A fetcher joins Wi-Fi itself, so a
// fetcher with nothing to do never turns the radio on.

// Starts the worker and, unless battery saver is on, fetches everything once
// (boot).
void begin();

// Queues a fetch of one app (when it opens) or of everything (AppId::Count).
void request(AppId app = AppId::Count);

bool busy();
// Increments after every finished run, so open screens can reload caches.
uint32_t generation();
// Whether an app's last fetch succeeded (its data is fresh).
bool appOnline(AppId app);

// Returns true when the app's data was refreshed.
using Fetcher = bool (*)();
void setFetcher(AppId app, Fetcher fetcher);

// Cache file paths, e.g. cachePath(AppId::LockScreen, "cache-weather") ->
// /fos/lockscreen/cache-weather.json.
std::string cachePath(AppId app, const char* name = "cache");
// Writes {"fetched": unix, "data": <body>} atomically.
bool writeCache(const std::string& cacheFile, const std::string& body);

}  // namespace fos::sync
