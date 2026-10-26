#pragma once

#include <ArduinoJson.h>

namespace fos {

// FOs data root on the microSD.
constexpr const char* DATA_DIR = "/fos";

// Reads and parses a JSON file. False when it is missing or invalid.
bool loadJson(const char* path, JsonDocument& doc);

// Writes doc atomically: creates the parent folder, writes <path>.tmp, then
// replaces <path>. A crash mid-write leaves the previous file intact.
bool saveJson(const char* path, const JsonDocument& doc);

}  // namespace fos
