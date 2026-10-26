#pragma once

#include <cstdint>
#include <ctime>
#include <string>

#include "fos/ui/Canvas.h"

class GfxRenderer;

namespace fos::lock {

// The lock screen (docs/PRODUCT.md): clock, date, synced weather, room
// climate from the on-board sensor and the next calendar event. It is not a
// lock: there is no PIN. It is the sleep screen too; while the device sleeps a
// timed wake redraws it every Settings > Lock screen > refresh interval.

// Everything the screen shows, read once per refresh (never while drawing).
struct Snapshot {
  bool hasTime = false;
  std::tm time{};
  int battery = -1;
  bool charging = false;

  bool hasWeather = false;
  std::string place;
  std::string temp;  // "14,5°"
  std::string condition;
  std::string range;       // "H 17°  L 9°"
  std::string weatherAge;  // "3 h ago" when the copy is old, else empty

  bool hasClimate = false;
  std::string roomTemp;
  std::string roomHumidity;  // "47% humidity"

  // A running or ended timer replaces the next event: it is more urgent.
  bool hasTimer = false;
  bool timerDone = false;
  std::string timerLabel;  // "COUNTDOWN  ·  ENDS 20:45"
  std::string timerTitle;  // "Time's up" / "12 min left"
  std::string timerNote;   // "10:00 countdown ended at 20:45"

  bool hasEvent = false;
  std::string eventWhen;  // "TODAY 23:40"
  std::string eventTitle;
  std::string eventPlace;
};

void capture(Snapshot& out);

// Draws the lock screen into area. asleep = the sleep frame: no seconds and a
// "press OK" footer with the refresh time.
void draw(ui::Canvas& c, const ui::Rect& area, const Snapshot& s, bool asleep);

// Sleep screen: captures and draws a B/W frame into the renderer's buffer.
// The caller sends it to the panel.
void drawSleepFrame(const GfxRenderer& renderer);

// Seconds until the next refresh boundary (e.g. the next quarter hour with a
// 15-minute interval) or the end of a running timer, whichever is first, for
// HalPowerManager::setWakeTimer().
uint32_t secondsToNextRefresh();

// Timed wake while asleep: redraws the sleep frame and refreshes the panel
// (fast, with a cleaning refresh every Settings > Display interval).
void refreshWhileAsleep(const GfxRenderer& renderer);

}  // namespace fos::lock
