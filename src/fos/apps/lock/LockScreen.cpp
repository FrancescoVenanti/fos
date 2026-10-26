#include "LockScreen.h"

#include <ArduinoJson.h>
#include <GfxRenderer.h>
#include <HalClimate.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#if __has_include(<esp_attr.h>)
#include <esp_attr.h>
#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "fos/apps/calendar/CalendarStore.h"
#include "fos/apps/timers/TimerStore.h"
#include "fos/core/Json.h"
#include "fos/core/Status.h"
#include "fos/settings/FosSettings.h"
#include "fos/sync/Sync.h"
#include "fos/ui/Components.h"
#include "fos/ui/Keyboard.h"

#ifndef RTC_DATA_ATTR
#define RTC_DATA_ATTR
#endif

namespace fos::lock {

using namespace ui;
using namespace ui::theme;

namespace {

constexpr const char* DEGREE = "\xC2\xB0";
constexpr const char* DOT = "  \xC2\xB7  ";
constexpr int CARD_H = 172;
constexpr int EVENT_H = 124;
constexpr int MAX_CLOCK_SCALE = 3 * 256;
constexpr uint32_t STALE_WEATHER_S = 3 * 3600;

// Timed wakes since the last cleaning refresh; survives deep sleep.
RTC_DATA_ATTR int wakesSinceClean = 0;

float toUserUnits(const float celsius) { return settings().metricUnits ? celsius : celsius * 9.0f / 5.0f + 32.0f; }

std::string temperature(const float celsius) {
  return formatDecimal(std::lround(toUserUnits(celsius) * 10.0f), 1, false) + DEGREE;
}

std::string wholeDegrees(const float celsius) {
  char out[12];
  snprintf(out, sizeof(out), "%ld%s", std::lround(toUserUnits(celsius)), DEGREE);
  return out;
}

void captureWeather(Snapshot& s, const std::time_t now) {
  JsonDocument doc;
  if (!loadJson(sync::cachePath(AppId::LockScreen, "cache-weather").c_str(), doc)) return;
  JsonObjectConst w = doc["data"];
  if (w["temp"].isNull()) return;
  s.hasWeather = true;
  s.place = w["location"] | "";
  s.temp = temperature(w["temp"] | 0.0f);
  s.condition = w["condition"] | "";
  if (!w["high"].isNull() && !w["low"].isNull()) {
    s.range = "H " + wholeDegrees(w["high"] | 0.0f) + "  L " + wholeDegrees(w["low"] | 0.0f);
  }
  const uint32_t fetched = doc["fetched"] | 0u;
  if (fetched != 0 && now > static_cast<std::time_t>(fetched) + STALE_WEATHER_S) {
    const long hours = static_cast<long>((now - fetched) / 3600);
    char age[24];
    if (hours < 48) {
      snprintf(age, sizeof(age), "%ld h ago", hours);
    } else {
      snprintf(age, sizeof(age), "%ld days ago", hours / 24);
    }
    s.weatherAge = age;
  }
}

void captureEvent(Snapshot& s) {
  calendar::Agenda agenda;
  if (!calendar::loadAgenda(agenda)) return;
  const std::string now = calendar::nowStamp();
  const calendar::Event* e = calendar::nextEvent(agenda, now);
  if (!e) return;
  s.hasEvent = true;
  s.eventTitle = e->title;
  s.eventPlace = e->location;
  std::string day;
  const std::string today = now.substr(0, 10);
  std::tm t{};
  int y = 0;
  int m = 0;
  int d = 0;
  if (e->date == today) {
    day = "TODAY";
  } else if (sscanf(e->date.c_str(), "%d-%d-%d", &y, &m, &d) == 3) {
    t.tm_year = y - 1900;
    t.tm_mon = m - 1;
    t.tm_mday = d;
    t.tm_hour = 12;
    std::mktime(&t);
    char label[24];
    snprintf(label, sizeof(label), "%.3s %d %.3s", weekdayName(t.tm_wday), t.tm_mday, monthName(t.tm_mon));
    day = label;
    for (char& ch : day) ch = static_cast<char>(toupper(static_cast<unsigned char>(ch)));
  }
  if (e->allDay) {
    s.eventWhen = day + ", ALL DAY";
  } else {
    std::tm start{};
    sscanf(e->start.c_str(), "%d:%d", &start.tm_hour, &start.tm_min);
    char clock[12];
    formatClock(start, clock, sizeof(clock));
    s.eventWhen = day + " " + clock;
  }
}

// A straight bar of the given thickness from (x1, y1) to (x2, y2), as two
// triangles so rotated hands keep square ends.
void bar(Canvas& c, const float x1, const float y1, const float x2, const float y2, const float thickness) {
  const float dx = x2 - x1;
  const float dy = y2 - y1;
  const float len = std::sqrt(dx * dx + dy * dy);
  if (len < 1.0f) return;
  const float nx = -dy / len * thickness / 2.0f;
  const float ny = dx / len * thickness / 2.0f;
  const auto px = [](const float v) { return static_cast<int>(std::lround(v)); };
  c.triangle(px(x1 + nx), px(y1 + ny), px(x2 + nx), px(y2 + ny), px(x2 - nx), px(y2 - ny), Tone::Black);
  c.triangle(px(x1 + nx), px(y1 + ny), px(x2 - nx), px(y2 - ny), px(x1 - nx), px(y1 - ny), Tone::Black);
}

std::string clockAt(const int64_t ms) {
  const std::time_t t = static_cast<std::time_t>(ms / 1000);
  std::tm tm{};
  localtime_r(&t, &tm);
  char out[12];
  formatClock(tm, out, sizeof(out));
  return out;
}

std::string minutesLeft(const int64_t ms) {
  const int64_t minutes = (ms + 59999) / 60000;
  if (minutes < 60) return std::to_string(minutes) + " min left";
  char out[24];
  snprintf(out, sizeof(out), "%lld h %02lld min left", static_cast<long long>(minutes / 60),
           static_cast<long long>(minutes % 60));
  return out;
}

void captureTimer(Snapshot& s) {
  const int64_t now = timers::nowMs();
  timers::update(now);
  const timers::State& t = timers::state();
  const timers::Span& cd = t.countdown;
  const timers::Pomodoro& p = t.pomodoro;
  std::string phase = timers::phaseName(p.phase);
  for (char& ch : phase) ch = static_cast<char>(toupper(static_cast<unsigned char>(ch)));
  if (t.alert != timers::Alert::None) {
    s.hasTimer = true;
    s.timerDone = true;
    s.timerTitle = "Time's up";
    const std::string at = clockAt(t.alertAtMs);
    if (t.alert == timers::Alert::Countdown) {
      s.timerLabel = "COUNTDOWN";
      s.timerNote = timers::format(cd.durationMs) + " countdown ended at " + at;
    } else {
      s.timerLabel = std::string("POMODORO") + DOT + phase;
      s.timerNote = std::string(timers::phaseName(p.phase)) + " ended at " + at;
    }
    return;
  }
  // The running span that ends first.
  const timers::Span* span = nullptr;
  if (cd.run == timers::Run::Running) span = &cd;
  if (p.span.run == timers::Run::Running && (!span || p.span.endMs < span->endMs)) span = &p.span;
  if (span) {
    s.hasTimer = true;
    s.timerLabel = (span == &cd ? std::string("COUNTDOWN") : std::string("POMODORO") + DOT + phase) + DOT + "ENDS " +
                   clockAt(span->endMs);
    s.timerTitle = minutesLeft(span->endMs - now);
    s.timerNote = "Updated " + clockAt(now);
    return;
  }
  if (t.stopwatch.running) {
    s.hasTimer = true;
    s.timerLabel = "STOPWATCH";
    s.timerTitle = "Running";
    s.timerNote = "Started " + clockAt(t.stopwatch.startMs);
  }
}

// Square analog face with hour ticks and two hands.
void analogClock(Canvas& c, const Rect& r, const std::tm& t) {
  const Rect face = block(c, r, Tone::White);
  const int cx = face.x + face.w / 2;
  const int cy = face.y + face.h / 2;
  const float radius = static_cast<float>(std::min(face.w, face.h)) / 2.0f - 10.0f;
  constexpr float HALF_TURN = 3.14159265f;  // not PI: Arduino defines it as a macro
  for (int i = 0; i < 12; ++i) {
    const float a = static_cast<float>(i) * HALF_TURN / 6.0f;
    const bool major = i % 3 == 0;
    const float inner = radius - (major ? 30.0f : 16.0f);
    bar(c, cx + std::sin(a) * inner, cy - std::cos(a) * inner, cx + std::sin(a) * radius, cy - std::cos(a) * radius,
        major ? 8.0f : 4.0f);
  }
  const float minute = static_cast<float>(t.tm_min) * HALF_TURN / 30.0f;
  const float hour = (static_cast<float>(t.tm_hour % 12) + static_cast<float>(t.tm_min) / 60.0f) * HALF_TURN / 6.0f;
  bar(c, cx, cy, cx + std::sin(hour) * radius * 0.5f, cy - std::cos(hour) * radius * 0.5f, 14.0f);
  bar(c, cx, cy, cx + std::sin(minute) * radius * 0.82f, cy - std::cos(minute) * radius * 0.82f, 8.0f);
  c.fill({cx - 12, cy - 12, 24, 24}, Tone::Black);
}

// Big digital clock centered in r; with seconds, ":SS" follows at body size.
void digitalClock(Canvas& c, const Rect& r, const std::tm& t, const bool seconds) {
  char text[12];
  formatClock(t, text, sizeof(text));
  // "7:56 PM": digits big, the suffix small.
  const char* suffix = nullptr;
  char* space = strchr(text, ' ');
  if (space) {
    *space = '\0';
    suffix = space + 1;
  }
  char secs[4] = "";
  if (seconds) snprintf(secs, sizeof(secs), ":%02d", t.tm_sec);
  const int extra = (suffix ? c.textWidth(font::TITLE, suffix, true) + 12 : 0) +
                    (seconds ? c.textWidth(font::JUMBO, secs, true) + 6 : 0);
  const int base = c.scaledWidth(font::JUMBO, text, 256);
  const int ascender = std::max(1, c.ascender(font::JUMBO));
  // As wide as the zone allows, as tall as 90% of it, at most 3x.
  int scale = base > 0 ? (r.w - extra - 8) * 256 / base : 256;
  scale = std::min({scale, MAX_CLOCK_SCALE, r.h * 230 / ascender});
  scale = std::max(256, scale);
  const int w = c.scaledWidth(font::JUMBO, text, scale) + extra;
  const int baseline = r.y + (r.h + ascender * scale / 256) / 2;
  int x = r.x + (r.w - w) / 2;
  x += c.scaledText(font::JUMBO, x, baseline, text, scale);
  if (seconds) {
    x += 6;
    c.text(font::JUMBO, x, baseline - c.ascender(font::JUMBO), secs, Tone::Black, true);
    x += c.textWidth(font::JUMBO, secs, true);
  }
  if (suffix) c.text(font::TITLE, x + 12, baseline - c.ascender(font::TITLE), suffix, Tone::Black, true);
}

void card(Canvas& c, const Rect& r, const char* label, const std::string& big, const std::string& line1,
          const std::string& line2) {
  Rect face = block(c, r, Tone::White).inset(14, 10);
  c.textIn(face.takeTop(c.lineHeight(font::SMALL)), font::SMALL, label, Tone::Black, Align::Left, true);
  c.textIn(face.takeTop(c.lineHeight(font::LARGE) + 4), font::LARGE, big.c_str(), Tone::Black, Align::Left, true);
  if (!line1.empty()) {
    c.textIn(face.takeTop(c.lineHeight(font::SMALL)), font::SMALL, line1.c_str(), Tone::Black, Align::Left, true);
  }
  if (!line2.empty()) {
    c.textIn(face.takeTop(c.lineHeight(font::SMALL)), font::SMALL, line2.c_str(), Tone::Black, Align::Left);
  }
}

}  // namespace

void capture(Snapshot& s) {
  s = Snapshot();
  s.hasTime = localNow(s.time);
  s.battery = powerManager.getBatteryPercentage();
  s.charging = gpio.isUsbConnected();
  const FosSettings& cfg = settings();
  if (cfg.lockShowWeather) captureWeather(s, std::time(nullptr));
  if (cfg.lockShowClimate) {
    float temp = 0.0f;
    float humidity = 0.0f;
    if (halClimate.read(temp, humidity)) {
      s.hasClimate = true;
      s.roomTemp = temperature(temp);
      char text[24];
      snprintf(text, sizeof(text), "%ld%% humidity", std::lround(humidity));
      s.roomHumidity = text;
    }
  }
  captureTimer(s);
  if (cfg.lockShowNextEvent && !s.hasTimer) captureEvent(s);
}

void draw(Canvas& c, const Rect& area, const Snapshot& s, const bool asleep) {
  const FosSettings& cfg = settings();
  Rect inner = area.inset(GUTTER, 0);

  if (asleep) {
    // Top line: wordmark and battery, standing in for the status bar.
    Rect top = inner.takeTop(STATUS_BAR_H);
    c.textIn(top, font::SMALL, "FOs", Tone::Black, Align::Left, true);
    if (s.battery >= 0) {
      char battery[24];
      snprintf(battery, sizeof(battery), "%s%d%%", s.charging ? "CHARGING  " : "", s.battery);
      c.textIn(top, font::SMALL, battery, Tone::Black, Align::Right, true);
    }
    rule(c, 0, inner.y - BORDER, c.width());
  }
  inner.takeTop(GAP);

  // Footer first so the middle gets what is left.
  const Rect footer = inner.takeBottom(asleep ? 44 : 36);

  // Clock.
  const bool analog = cfg.clockStyle == ClockStyle::Analog;
  if (s.hasTime && analog) {
    const int size = std::min(inner.w - 80, 240);
    const Rect zone = inner.takeTop(size + GAP);
    analogClock(c, {zone.x + (zone.w - size) / 2, zone.y, size, size}, s.time);
  } else if (s.hasTime) {
    const bool seconds = !asleep && cfg.clockStyle == ClockStyle::WithSeconds;
    digitalClock(c, inner.takeTop(180), s.time, seconds);
  } else {
    c.textIn(inner.takeTop(120), font::TITLE, "Clock not set", Tone::Black, Align::Center, true);
  }

  // Date.
  if (s.hasTime) {
    c.textIn(inner.takeTop(c.lineHeight(font::TITLE)), font::TITLE, weekdayName(s.time.tm_wday), Tone::Black,
             Align::Center, true);
    char date[32];
    snprintf(date, sizeof(date), "%d %s %d", s.time.tm_mday, monthName(s.time.tm_mon), s.time.tm_year + 1900);
    c.textIn(inner.takeTop(c.lineHeight(font::BODY) + 4), font::BODY, date, Tone::Black, Align::Center, true);
  }
  inner.takeTop(GAP * 2);

  // Weather and room climate side by side (or one full width).
  const bool weatherCard = cfg.lockShowWeather;
  const bool climateCard = cfg.lockShowClimate;
  if ((weatherCard || climateCard) && inner.h >= CARD_H) {
    Rect row = inner.takeTop(CARD_H);
    inner.takeTop(GAP * 2);
    const int count = (weatherCard ? 1 : 0) + (climateCard ? 1 : 0);
    const int w = count == 2 ? (row.w - GAP) / 2 : row.w;
    if (weatherCard) {
      const Rect r = row.takeLeft(w);
      row.takeLeft(GAP);
      if (s.hasWeather) {
        std::string label = s.place.empty() ? "WEATHER" : s.place;
        for (char& ch : label) ch = static_cast<char>(toupper(static_cast<unsigned char>(ch)));
        card(c, r, label.c_str(), s.temp, s.condition, s.weatherAge.empty() ? s.range : s.weatherAge);
      } else {
        card(c, r, "WEATHER", "--", "Not synced yet", "");
      }
    }
    if (climateCard) {
      if (s.hasClimate) {
        card(c, row, "ROOM", s.roomTemp, s.roomHumidity, "");
      } else {
        card(c, row, "ROOM", "--", "No sensor", "");
      }
    }
  }

  // A timer, inverted, takes the next event's place.
  if (s.hasTimer && inner.h >= EVENT_H) {
    const Rect r = inner.takeTop(EVENT_H);
    c.fill(r, Tone::Black);
    if (s.timerDone) c.frame(r.inset(6), BORDER, Tone::White);  // double frame: needs attention
    Rect face = r.inset(16, 12);
    c.textIn(face.takeTop(c.lineHeight(font::SMALL)), font::SMALL, s.timerLabel.c_str(), Tone::White, Align::Left,
             true);
    face.takeTop(4);
    c.textIn(face.takeTop(c.lineHeight(font::TITLE)), font::TITLE, s.timerTitle.c_str(), Tone::White, Align::Left,
             true);
    c.textIn(face.takeTop(c.lineHeight(font::BODY)), font::BODY, s.timerNote.c_str(), Tone::White, Align::Left);
  } else if (cfg.lockShowNextEvent && inner.h >= EVENT_H) {
    // Next event, inverted: the one thing to notice.
    const Rect r = inner.takeTop(EVENT_H);
    c.fill(r, Tone::Black);
    Rect face = r.inset(16, 12);
    if (s.hasEvent) {
      const std::string label = "NEXT" + std::string(DOT) + s.eventWhen;
      c.textIn(face.takeTop(c.lineHeight(font::SMALL)), font::SMALL, label.c_str(), Tone::White, Align::Left, true);
      face.takeTop(4);
      c.textIn(face.takeTop(c.lineHeight(font::TITLE)), font::TITLE, s.eventTitle.c_str(), Tone::White, Align::Left,
               true);
      if (!s.eventPlace.empty()) {
        c.textIn(face.takeTop(c.lineHeight(font::BODY)), font::BODY, s.eventPlace.c_str(), Tone::White, Align::Left);
      }
    } else {
      c.textIn(face.takeTop(c.lineHeight(font::SMALL)), font::SMALL, "NEXT", Tone::White, Align::Left, true);
      c.textIn(face, font::TITLE, "Nothing planned", Tone::White, Align::Left, true);
    }
  }

  // Footer.
  if (asleep) {
    char text[64];
    char clock[12] = "";
    if (s.hasTime) formatClock(s.time, clock, sizeof(clock));
    snprintf(text, sizeof(text), "Press OK to wake%sUpdated %s, every %d min", DOT, clock,
             cfg.batterySaver ? 60 : cfg.lockRefreshMinutes);
    c.textIn(footer, font::SMALL, text, Tone::Black, Align::Center, true);
  } else {
    c.textIn(footer, font::SMALL, "Tap anywhere to close", Tone::Black, Align::Center, true);
  }
}

void drawSleepFrame(const GfxRenderer& renderer) {
  Snapshot s;
  capture(s);
  renderer.clearScreen();
  Canvas c(renderer, Pass::Bw, settings().invertColors, nullptr, 0, 0);
  c.fill({0, 0, c.width(), c.height()}, Tone::White);
  draw(c, {0, 0, c.width(), c.height()}, s, true);
}

uint32_t secondsToNextRefresh() {
  // Battery saver stretches the refresh to once an hour.
  const uint8_t minutes = settings().batterySaver ? 60 : std::max<uint8_t>(1, settings().lockRefreshMinutes);
  const uint32_t interval = static_cast<uint32_t>(minutes) * 60;
  std::tm t{};
  if (!localNow(t)) return interval;
  const uint32_t intoDay = static_cast<uint32_t>(t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec);
  // Land a couple of seconds after the boundary so the minute has turned.
  const uint32_t boundary = interval - intoDay % interval + 2;
  // A timer that ends sooner wakes the device to show it.
  const uint32_t timer = timers::secondsToNextEnd();
  return timer > 0 ? std::min(boundary, timer + 1) : boundary;
}

void refreshWhileAsleep(const GfxRenderer& renderer) {
  drawSleepFrame(renderer);
  const int every = settings().fullRefreshEvery;
  HalDisplay::RefreshMode mode = HalDisplay::FAST_REFRESH;
  if (every > 0 && ++wakesSinceClean >= every) {
    wakesSinceClean = 0;
    mode = HalDisplay::HALF_REFRESH;
  }
  renderer.displayBuffer(mode);
}

}  // namespace fos::lock
