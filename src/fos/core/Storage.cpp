#include "Storage.h"

#include <HalStorage.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace fos {
namespace {

constexpr const char* APP_FOLDERS[APP_COUNT] = {
    "/fos/budgeteer", "/fos/todo",   "/fos/calendar",   "/fos/notes",  "/fos/reader",  "/fos/readlater",
    "/fos/flashcards", "/fos/chess", "/fos/puzzles",    "/fos/timers", "/fos/calculator", "/fos/wallet",
    "/fos/habits",    "/fos/terminal", "/fos/lockscreen", "/fos/settings"};

uint64_t sumTree(HalFile& dir, const int depth) {
  uint64_t total = 0;
  if (depth > 6) return 0;
  for (HalFile entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    if (entry.isDirectory()) {
      total += sumTree(entry, depth + 1);
    } else {
      total += entry.fileSize();
    }
  }
  return total;
}

}  // namespace

const char* appFolder(const AppId id) {
  const int i = static_cast<int>(id);
  return i >= 0 && i < APP_COUNT ? APP_FOLDERS[i] : "/fos";
}

uint64_t folderBytes(const char* path) {
  if (!Storage.exists(path)) return 0;
  HalFile dir = Storage.open(path);
  if (!dir || !dir.isDirectory()) return dir ? dir.fileSize() : 0;
  return sumTree(dir, 0);
}

uint64_t appDataBytes(const AppId id) {
  // Settings' own data is the settings file next to the app folders.
  if (id == AppId::Settings) return folderBytes("/fos/settings.json");
  return folderBytes(appFolder(id));
}

void formatBytes(const uint64_t bytes, char* out, const size_t size) {
  if (bytes < 1024) {
    snprintf(out, size, "%u B", static_cast<unsigned>(bytes));
  } else if (bytes < 1024 * 1024) {
    snprintf(out, size, "%.1f KB", bytes / 1024.0);
  } else {
    snprintf(out, size, "%.1f MB", bytes / (1024.0 * 1024.0));
  }
}

void clearSyncedCaches() {
  for (int i = 0; i < APP_COUNT; ++i) {
    const std::string folder = APP_FOLDERS[i];
    if (!Storage.exists(folder.c_str())) continue;
    HalFile dir = Storage.open(folder.c_str());
    if (!dir || !dir.isDirectory()) continue;
    char name[96];
    std::string victims;
    for (HalFile entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
      entry.getName(name, sizeof(name));
      if (!entry.isDirectory() && strncmp(name, "cache", 5) == 0) victims += folder + "/" + name + "\n";
    }
    dir.close();
    size_t start = 0;
    while (start < victims.size()) {
      const size_t end = victims.find('\n', start);
      Storage.remove(victims.substr(start, end - start).c_str());
      start = end + 1;
    }
  }
  Storage.removeDir("/fos/readlater/articles");
  LOG_INF("FOS", "Synced caches cleared");
}

void factoryReset() {
  Storage.removeDir("/fos");
  LOG_INF("FOS", "Factory reset: /fos erased");
}

}  // namespace fos
