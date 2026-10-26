#include "Status.h"

#include <HalClock.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <WiFi.h>

#include <atomic>
#include <cstdio>

#include "fos/settings/FosSettings.h"

namespace fos {
namespace {

std::atomic<uint8_t> syncState{static_cast<uint8_t>(SyncState::Idle)};
std::atomic<int> pendingCount{0};

}  // namespace

bool localNow(std::tm& out) {
  if (halClock.localTime(out)) return true;
  // Without an RTC the system clock is valid once NTP or trusted time set it.
  const std::time_t now = std::time(nullptr);
  if (now < 1700000000) return false;
  localtime_r(&now, &out);
  return true;
}

void formatClock(const std::tm& t, char* out, const int size) {
  if (settings().clock24h) {
    snprintf(out, size, "%02d:%02d", t.tm_hour, t.tm_min);
  } else {
    const int h = t.tm_hour % 12 == 0 ? 12 : t.tm_hour % 12;
    snprintf(out, size, "%d:%02d %s", h, t.tm_min, t.tm_hour < 12 ? "AM" : "PM");
  }
}

void formatDate(const std::tm& t, char* out, const int size) {
  switch (settings().dateFormat) {
    case DateFormat::MonthDayYear:
      snprintf(out, size, "%02d/%02d/%04d", t.tm_mon + 1, t.tm_mday, t.tm_year + 1900);
      break;
    case DateFormat::Iso:
      snprintf(out, size, "%04d-%02d-%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
      break;
    case DateFormat::DayMonthYear:
    default:
      snprintf(out, size, "%02d/%02d/%04d", t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);
      break;
  }
}

const char* weekdayName(const int wday) {
  static const char* NAMES[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
  return wday >= 0 && wday < 7 ? NAMES[wday] : "";
}

const char* monthName(const int mon) {
  static const char* NAMES[] = {"January", "February", "March",     "April",   "May",      "June",
                                "July",    "August",   "September", "October", "November", "December"};
  return mon >= 0 && mon < 12 ? NAMES[mon] : "";
}

StatusInfo currentStatus() {
  StatusInfo info;
  std::tm t{};
  if (localNow(t)) formatClock(t, info.time, sizeof(info.time));
  info.battery = powerManager.getBatteryPercentage();
  info.charging = gpio.isUsbConnected();
  info.wifi = WiFi.status() == WL_CONNECTED;
  info.sync = info.wifi ? static_cast<SyncState>(syncState.load()) : SyncState::Offline;
  info.pending = pendingCount.load();
  return info;
}

void setSyncState(const SyncState state) { syncState.store(static_cast<uint8_t>(state)); }

void setPendingCount(const int pending) { pendingCount.store(pending); }

}  // namespace fos
