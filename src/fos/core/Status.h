#pragma once

#include <cstdint>
#include <ctime>

namespace fos {

enum class SyncState : uint8_t { Offline, Idle, Syncing, Error };

// What the status bar shows on every screen.
struct StatusInfo {
  char time[8] = "--:--";
  int battery = -1;  // percent, -1 = unknown
  bool charging = false;
  bool wifi = false;
  SyncState sync = SyncState::Offline;
  int pending = 0;         // queued entries waiting to sync
  bool timerDone = false;  // a timer ended and was not dismissed
};

StatusInfo currentStatus();

// Called by the sync service.
void setSyncState(SyncState state);
void setPendingCount(int pending);
// Called by the Timers app.
void setTimerAlert(bool done);

// Local time now. False if the clock has never been set.
bool localNow(std::tm& out);
// "14:05" or "2:05 PM" per Settings > Region.
void formatClock(const std::tm& t, char* out, int size);
// Date per Settings > Region (e.g. "26/10/2026").
void formatDate(const std::tm& t, char* out, int size);
const char* weekdayName(int wday);  // 0 = Sunday
const char* monthName(int mon);     // 0 = January

}  // namespace fos
