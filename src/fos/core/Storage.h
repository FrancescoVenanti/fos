#pragma once

#include <cstddef>
#include <cstdint>

#include "fos/apps/AppRegistry.h"

namespace fos {

// "/fos/<app>" folder of an app, e.g. "/fos/budgeteer".
const char* appFolder(AppId id);

// Total size of the files under path (recursive).
uint64_t folderBytes(const char* path);
uint64_t appDataBytes(AppId id);
void formatBytes(uint64_t bytes, char* out, size_t size);

// Removes every app's synced copy (files named cache*.json and the
// read-later article folder). User data and the Budgeteer queue stay.
void clearSyncedCaches();
// Erases /fos completely (settings and app data). Books are untouched.
void factoryReset();

}  // namespace fos
