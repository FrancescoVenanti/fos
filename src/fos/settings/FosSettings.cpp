#include "FosSettings.h"

#include <ArduinoJson.h>
#include <Logging.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "fos/core/Json.h"

namespace fos {
namespace {

constexpr const char* SETTINGS_PATH = "/fos/settings.json";
constexpr int SETTINGS_VERSION = 1;

template <typename E>
E enumFrom(JsonVariantConst value, E fallback, int count) {
  if (!value.is<int>()) return fallback;
  const int v = value.as<int>();
  return v >= 0 && v < count ? static_cast<E>(v) : fallback;
}

}  // namespace

FosSettings::FosSettings() { resetLayout(); }

bool FosSettings::isVisible(const AppId id) const {
  // Settings can never be hidden.
  if (id == AppId::Settings) return true;
  const int i = static_cast<int>(id);
  return i >= 0 && i < APP_COUNT && visible[i];
}

void FosSettings::setVisible(const AppId id, const bool show) {
  const int i = static_cast<int>(id);
  if (i < 0 || i >= APP_COUNT) return;
  visible[i] = id == AppId::Settings ? true : show;
}

int FosSettings::visibleApps(AppId* out, const int capacity) const {
  int n = 0;
  for (int i = 0; i < APP_COUNT && n < capacity; ++i) {
    const auto id = static_cast<AppId>(order[i]);
    if (isVisible(id)) out[n++] = id;
  }
  return n;
}

void FosSettings::move(const AppId id, const int delta) {
  int at = -1;
  for (int i = 0; i < APP_COUNT; ++i) {
    if (order[i] == static_cast<uint8_t>(id)) at = i;
  }
  const int to = at + delta;
  if (at < 0 || to < 0 || to >= APP_COUNT) return;
  std::swap(order[at], order[to]);
}

void FosSettings::resetLayout() {
  for (int i = 0; i < APP_COUNT; ++i) {
    order[i] = static_cast<uint8_t>(i);
    visible[i] = true;
  }
  iconSize = IconSize::Medium;
  showLabels = true;
}

bool FosSettings::load() {
  JsonDocument doc;
  if (!loadJson(SETTINGS_PATH, doc)) return false;

  // Launcher order: keep known ids once each, then append any missing ones
  // (an app added by a newer firmware shows up at the end).
  bool seen[APP_COUNT] = {};
  int n = 0;
  for (JsonVariantConst v : doc["layout"]["order"].as<JsonArrayConst>()) {
    const int id = v | -1;
    if (id >= 0 && id < APP_COUNT && !seen[id] && n < APP_COUNT) {
      order[n++] = static_cast<uint8_t>(id);
      seen[id] = true;
    }
  }
  for (int id = 0; id < APP_COUNT && n < APP_COUNT; ++id) {
    if (!seen[id]) order[n++] = static_cast<uint8_t>(id);
  }
  for (int i = 0; i < APP_COUNT; ++i) visible[i] = true;
  for (JsonVariantConst v : doc["layout"]["hidden"].as<JsonArrayConst>()) {
    const int id = v | -1;
    if (id >= 0 && id < APP_COUNT) setVisible(static_cast<AppId>(id), false);
  }
  iconSize = enumFrom(doc["layout"]["iconSize"], IconSize::Medium, 3);
  showLabels = doc["layout"]["labels"] | true;

  fontSize = enumFrom(doc["display"]["fontSize"], FontSize::Medium, 3);
  fullRefreshEvery = doc["display"]["fullRefreshEvery"] | 10;
  invertColors = doc["display"]["invert"] | false;

  lockRefreshMinutes = doc["lock"]["refreshMinutes"] | 15;
  clockStyle = enumFrom(doc["lock"]["clockStyle"], ClockStyle::Large, 3);
  lockShowWeather = doc["lock"]["weather"] | true;
  lockShowClimate = doc["lock"]["climate"] | true;
  lockShowNextEvent = doc["lock"]["nextEvent"] | true;

  autoSync = doc["sync"]["auto"] | true;

  lichessToken = doc["accounts"]["lichessToken"] | "";
  backendUrl = doc["accounts"]["backendUrl"] | "http://127.0.0.1:8787";
  backendKey = doc["accounts"]["backendKey"] | "";

  swapUpDown = doc["buttons"]["swapUpDown"] | false;
  okAction = enumFrom(doc["buttons"]["ok"], OkAction::Back, 3);
  keyboardLayout = enumFrom(doc["buttons"]["keyboard"], KeyboardLayout::Qwerty, 3);
  accentedLetters = doc["buttons"]["accents"] | true;

  sleepMinutes = doc["power"]["sleepMinutes"] | 5;
  batterySaver = doc["power"]["saver"] | false;

  clock24h = doc["region"]["clock24h"] | true;
  dateFormat = enumFrom(doc["region"]["dateFormat"], DateFormat::DayMonthYear, 3);
  decimalComma = doc["region"]["decimalComma"] | true;
  metricUnits = doc["region"]["metric"] | true;

  LOG_DBG("FOS", "Settings loaded");
  return true;
}

bool FosSettings::save() const {
  JsonDocument doc;
  doc["version"] = SETTINGS_VERSION;
  JsonObject layout = doc["layout"].to<JsonObject>();
  JsonArray orderArr = layout["order"].to<JsonArray>();
  for (int i = 0; i < APP_COUNT; ++i) orderArr.add(order[i]);
  JsonArray hidden = layout["hidden"].to<JsonArray>();
  for (int i = 0; i < APP_COUNT; ++i) {
    if (!isVisible(static_cast<AppId>(i))) hidden.add(i);
  }
  layout["iconSize"] = static_cast<int>(iconSize);
  layout["labels"] = showLabels;

  JsonObject display = doc["display"].to<JsonObject>();
  display["fontSize"] = static_cast<int>(fontSize);
  display["fullRefreshEvery"] = fullRefreshEvery;
  display["invert"] = invertColors;

  JsonObject lock = doc["lock"].to<JsonObject>();
  lock["refreshMinutes"] = lockRefreshMinutes;
  lock["clockStyle"] = static_cast<int>(clockStyle);
  lock["weather"] = lockShowWeather;
  lock["climate"] = lockShowClimate;
  lock["nextEvent"] = lockShowNextEvent;

  JsonObject sync = doc["sync"].to<JsonObject>();
  sync["auto"] = autoSync;

  JsonObject accounts = doc["accounts"].to<JsonObject>();
  accounts["lichessToken"] = lichessToken.c_str();
  accounts["backendUrl"] = backendUrl.c_str();
  accounts["backendKey"] = backendKey.c_str();

  JsonObject buttons = doc["buttons"].to<JsonObject>();
  buttons["swapUpDown"] = swapUpDown;
  buttons["ok"] = static_cast<int>(okAction);
  buttons["keyboard"] = static_cast<int>(keyboardLayout);
  buttons["accents"] = accentedLetters;

  JsonObject power = doc["power"].to<JsonObject>();
  power["sleepMinutes"] = sleepMinutes;
  power["saver"] = batterySaver;

  JsonObject region = doc["region"].to<JsonObject>();
  region["clock24h"] = clock24h;
  region["dateFormat"] = static_cast<int>(dateFormat);
  region["decimalComma"] = decimalComma;
  region["metric"] = metricUnits;

  return saveJson(SETTINGS_PATH, doc);
}

FosSettings& settings() {
  static FosSettings instance;
  return instance;
}

void applySettings() {
  // FOs owns the sleep timeout; CrossPoint's power loop reads SETTINGS.
  const uint8_t minutes = settings().sleepMinutes;
  SETTINGS.sleepTimeoutMinutes = minutes == 0
                                     ? CrossPointSettings::SLEEP_TIMEOUT_NEVER_MINUTES
                                     : std::min<uint8_t>(minutes, CrossPointSettings::MAX_SLEEP_TIMEOUT_MINUTES - 1);
}

}  // namespace fos
