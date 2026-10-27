#include "Direct.h"

#include <ArduinoJson.h>
#include <Logging.h>

#include <cstdio>
#include <ctime>

#include "Backend.h"
#include "Sync.h"
#include "fos/apps/calendar/Ics.h"
#include "fos/core/Json.h"
#include "fos/core/Status.h"
#include "fos/settings/FosSettings.h"

namespace fos::sync {
namespace {

constexpr char PLACE_FILE[] = "/fos/lockscreen/place.json";

// Open-Meteo weather codes (WMO) in words.
const char* condition(const int code) {
  if (code == 0) return "Clear";
  if (code <= 2) return "Partly cloudy";
  if (code == 3) return "Overcast";
  if (code == 45 || code == 48) return "Fog";
  if (code >= 51 && code <= 57) return "Drizzle";
  if (code >= 61 && code <= 67) return "Rain";
  if (code >= 71 && code <= 77) return "Snow";
  if (code >= 80 && code <= 82) return "Showers";
  if (code == 85 || code == 86) return "Snow showers";
  if (code >= 95) return "Thunderstorm";
  return "";
}

// Coordinates for the weather place, looked up once per place name.
bool placeCoordinates(const std::string& place, float& lat, float& lon, std::string& name) {
  JsonDocument cached;
  if (loadJson(PLACE_FILE, cached) && place == (cached["query"] | "")) {
    lat = cached["lat"] | 0.0f;
    lon = cached["lon"] | 0.0f;
    name = cached["name"] | place.c_str();
    return true;
  }
  const net::Response r = net::request(
      "GET", net::directUrl("https://geocoding-api.open-meteo.com/v1/search?count=1&name=" + net::urlEncode(place)), "",
      "");
  JsonDocument doc;
  if (!r.ok() || deserializeJson(doc, r.body) || doc["results"][0].isNull()) {
    LOG_INF("SYNC", "Weather place '%s' not found (%d)", place.c_str(), r.status);
    return false;
  }
  JsonObjectConst hit = doc["results"][0];
  lat = hit["latitude"] | 0.0f;
  lon = hit["longitude"] | 0.0f;
  name = hit["name"] | place.c_str();
  JsonDocument out;
  out["query"] = place;
  out["lat"] = lat;
  out["lon"] = lon;
  out["name"] = name;
  saveJson(PLACE_FILE, out);
  return true;
}

bool fetchWeather(const bool backend) {
  const std::string cache = cachePath(AppId::LockScreen, "cache-weather");
  const std::string place = settings().weatherPlace;
  if (place.empty()) return backend && fetchToCache("/api/v1/lockscreen/weather", cache);

  float lat = 0.0f;
  float lon = 0.0f;
  std::string name;
  if (!placeCoordinates(place, lat, lon, name)) return false;
  char url[256];
  snprintf(
      url, sizeof(url),
      "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f&current=temperature_2m,"
      "relative_humidity_2m,weather_code&daily=temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=1",
      lat, lon);
  const net::Response r = net::request("GET", net::directUrl(url), "", "");
  JsonDocument doc;
  if (!r.ok() || deserializeJson(doc, r.body)) {
    LOG_INF("SYNC", "Open-Meteo -> %d", r.status);
    return false;
  }
  // Same shape as the backend's /api/v1/lockscreen/weather.
  JsonDocument out;
  out["location"] = name;
  out["temp"] = doc["current"]["temperature_2m"] | 0.0f;
  out["humidity"] = doc["current"]["relative_humidity_2m"] | 0;
  out["condition"] = condition(doc["current"]["weather_code"] | -1);
  out["high"] = doc["daily"]["temperature_2m_max"][0] | 0.0f;
  out["low"] = doc["daily"]["temperature_2m_min"][0] | 0.0f;
  out["source"] = "open-meteo";
  std::string body;
  serializeJson(out, body);
  return writeCache(cache, body);
}

bool fetchRates(const bool backend) {
  const std::string cache = cachePath(AppId::Calculator, "cache-rates");
  if (backend && fetchToCache("/api/v1/calculator/rates", cache)) return true;
  // Frankfurter (European Central Bank rates) answers in the backend's shape.
  const net::Response r = net::request("GET", net::directUrl("https://api.frankfurter.app/latest?from=EUR"), "", "");
  JsonDocument doc;
  if (!r.ok() || deserializeJson(doc, r.body) || doc["rates"].isNull()) {
    LOG_INF("SYNC", "Frankfurter -> %d", r.status);
    return false;
  }
  return writeCache(cache, r.body);
}

bool fetchCalendar(const bool backend) {
  const std::string cache = cachePath(AppId::Calendar);
  const std::string url = settings().calendarUrl;
  if (url.empty()) return backend && fetchToCache("/api/v1/calendar/agenda?days=7", cache);

  std::tm t{};
  if (!localNow(t)) return false;  // the window needs today's date
  char today[16];
  snprintf(today, sizeof(today), "%04d-%02d-%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
  calendar::IcsReader reader(today, 7);
  // webcal:// is https:// for a download.
  std::string address = url;
  if (address.compare(0, 9, "webcal://") == 0) address = "https://" + address.substr(9);
  const int status = net::stream(address, [&reader](const uint8_t* data, const size_t len) {
    reader.feed(data, len);
    return true;
  });
  if (status < 200 || status >= 300) {
    LOG_INF("SYNC", "Calendar link -> %d", status);
    return false;
  }
  reader.finish();
  LOG_DBG("SYNC", "Calendar: %d events this week", reader.eventCount());
  return writeCache(cache, reader.agendaJson());
}

}  // namespace

void registerDirectSources() {
  setFetcher(AppId::LockScreen, fetchWeather);
  setFetcher(AppId::Calculator, fetchRates);
  setFetcher(AppId::Calendar, fetchCalendar);
}

}  // namespace fos::sync
