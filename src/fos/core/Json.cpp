#include "Json.h"

#include <HalStorage.h>
#include <Logging.h>
#include <PersistableStore.h>

#include <string>

namespace fos {

bool loadJson(const char* path, JsonDocument& doc) { return PersistableStoreBase::readDocFromFile(path, doc); }

bool saveJson(const char* path, const JsonDocument& doc) {
  const std::string target(path);
  const size_t slash = target.rfind('/');
  if (slash != std::string::npos && slash > 0) Storage.mkdir(target.substr(0, slash).c_str());
  const std::string tmp = target + ".tmp";
  if (!PersistableStoreBase::writeDocToFile(tmp.c_str(), doc)) return false;
  if (!Storage.replaceFile(tmp.c_str(), path)) {
    LOG_ERR("FOS", "Failed to replace %s", path);
    return false;
  }
  return true;
}

}  // namespace fos
