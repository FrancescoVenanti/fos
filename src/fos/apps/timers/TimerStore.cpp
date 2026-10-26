#include "TimerStore.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Logging.h>
#include <sys/time.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "fos/core/Json.h"
#include "fos/core/Status.h"

namespace fos::timers {
namespace {

constexpr char STATE_FILE[] = "/fos/timers/state.json";
constexpr unsigned long POLL_MS = 250;

State store;
bool loaded = false;
bool shownAlert = false;
unsigned long lastPollAt = 0;

void spanToJson(const Span& s, JsonObject o) {
  o["duration"] = s.durationMs;
  o["run"] = static_cast<int>(s.run);
  if (s.run == Run::Running) o["end"] = s.endMs;
  if (s.run == Run::Paused) o["left"] = s.leftMs;
}

void spanFromJson(JsonObjectConst o, Span& s) {
  s.durationMs = o["duration"] | s.durationMs;
  const int run = o["run"] | 0;
  s.run = run >= 0 && run <= static_cast<int>(Run::Done) ? static_cast<Run>(run) : Run::Idle;
  s.endMs = o["end"] | 0LL;
  s.leftMs = o["left"] | 0LL;
}

void load() {
  loaded = true;
  store = State();
  store.countdown.durationMs = 5 * 60000LL;
  store.pomodoro.span.durationMs = store.pomodoro.phaseMs(Phase::Work);
  JsonDocument doc;
  if (loadJson(STATE_FILE, doc)) {
    spanFromJson(doc["countdown"], store.countdown);
    JsonObjectConst p = doc["pomodoro"];
    Pomodoro& pomo = store.pomodoro;
    pomo.workMin = p["work"] | pomo.workMin;
    pomo.shortMin = p["short"] | pomo.shortMin;
    pomo.longMin = p["long"] | pomo.longMin;
    pomo.rounds = p["rounds"] | pomo.rounds;
    const int phase = p["phase"] | 0;
    pomo.phase = phase >= 0 && phase <= 2 ? static_cast<Phase>(phase) : Phase::Work;
    pomo.round = std::max<uint8_t>(1, p["round"] | 1);
    spanFromJson(p["span"], pomo.span);
    JsonObjectConst sw = doc["stopwatch"];
    store.stopwatch.running = sw["running"] | false;
    store.stopwatch.startMs = sw["start"] | 0LL;
    store.stopwatch.accumMs = sw["accum"] | 0LL;
    for (JsonVariantConst lap : sw["laps"].as<JsonArrayConst>()) store.stopwatch.laps.push_back(lap | 0LL);
    const int alert = doc["alert"] | 0;
    store.alert = alert >= 0 && alert <= 2 ? static_cast<Alert>(alert) : Alert::None;
    store.alertAtMs = doc["alertAt"] | 0LL;
    store.tab = doc["tab"] | 0;
  }
  shownAlert = store.alert != Alert::None;
  setTimerAlert(shownAlert);
}

}  // namespace

int64_t nowMs() {
  timeval tv{};
  gettimeofday(&tv, nullptr);
  return static_cast<int64_t>(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
}

int64_t Span::remaining(const int64_t now) const {
  switch (run) {
    case Run::Running:
      return std::max<int64_t>(0, endMs - now);
    case Run::Paused:
      return leftMs;
    case Run::Done:
      return 0;
    case Run::Idle:
      break;
  }
  return durationMs;
}

void Span::start(const int64_t now) {
  const int64_t left = run == Run::Paused ? leftMs : durationMs;
  if (left <= 0) return;
  endMs = now + left;
  run = Run::Running;
}

void Span::pause(const int64_t now) {
  if (run != Run::Running) return;
  leftMs = std::max<int64_t>(0, endMs - now);
  run = Run::Paused;
}

void Span::reset() {
  run = Run::Idle;
  endMs = 0;
  leftMs = 0;
}

int64_t Pomodoro::phaseMs(const Phase p) const {
  const int minutes = p == Phase::Work ? workMin : p == Phase::ShortBreak ? shortMin : longMin;
  return minutes * 60000LL;
}

Phase Pomodoro::nextPhase() const {
  if (phase != Phase::Work) return Phase::Work;
  return round % std::max<uint8_t>(1, rounds) == 0 ? Phase::LongBreak : Phase::ShortBreak;
}

void Pomodoro::advance() {
  const Phase next = nextPhase();
  if (next == Phase::Work) round = phase == Phase::LongBreak ? 1 : round + 1;
  phase = next;
  span.reset();
  span.durationMs = phaseMs(phase);
}

void Pomodoro::syncLength() {
  if (span.run == Run::Idle) span.durationMs = phaseMs(phase);
}

int64_t Stopwatch::elapsed(const int64_t now) const {
  return accumMs + (running ? std::max<int64_t>(0, now - startMs) : 0);
}

State& state() {
  if (!loaded) load();
  return store;
}

bool save() {
  const State& s = state();
  JsonDocument doc;
  spanToJson(s.countdown, doc["countdown"].to<JsonObject>());
  JsonObject p = doc["pomodoro"].to<JsonObject>();
  p["work"] = s.pomodoro.workMin;
  p["short"] = s.pomodoro.shortMin;
  p["long"] = s.pomodoro.longMin;
  p["rounds"] = s.pomodoro.rounds;
  p["phase"] = static_cast<int>(s.pomodoro.phase);
  p["round"] = s.pomodoro.round;
  spanToJson(s.pomodoro.span, p["span"].to<JsonObject>());
  JsonObject sw = doc["stopwatch"].to<JsonObject>();
  sw["running"] = s.stopwatch.running;
  sw["start"] = s.stopwatch.startMs;
  sw["accum"] = s.stopwatch.accumMs;
  JsonArray laps = sw["laps"].to<JsonArray>();
  for (const int64_t lap : s.stopwatch.laps) laps.add(lap);
  doc["alert"] = static_cast<int>(s.alert);
  doc["alertAt"] = s.alertAtMs;
  doc["tab"] = s.tab;
  const bool ok = saveJson(STATE_FILE, doc);
  if (!ok) LOG_ERR("TIMER", "Cannot write %s", STATE_FILE);
  return ok;
}

bool update(const int64_t now) {
  State& s = state();
  bool ended = false;
  if (s.countdown.run == Run::Running && now >= s.countdown.endMs) {
    s.countdown.run = Run::Done;
    s.alert = Alert::Countdown;
    s.alertAtMs = s.countdown.endMs;
    ended = true;
  }
  if (s.pomodoro.span.run == Run::Running && now >= s.pomodoro.span.endMs) {
    s.pomodoro.span.run = Run::Done;
    s.alert = Alert::Pomodoro;
    s.alertAtMs = s.pomodoro.span.endMs;
    ended = true;
  }
  if (ended) {
    LOG_INF("TIMER", "Timer ended");
    save();
  }
  setTimerAlert(s.alert != Alert::None);
  return ended;
}

bool poll() {
  if (lastPollAt != 0 && millis() - lastPollAt < POLL_MS) return false;
  lastPollAt = millis();
  update();
  const bool pending = state().alert != Alert::None;
  const bool changed = pending != shownAlert;
  shownAlert = pending;
  return changed;
}

bool alertPending() { return state().alert != Alert::None; }

void dismiss() {
  State& s = state();
  if (s.countdown.run == Run::Done) s.countdown.reset();
  if (s.pomodoro.span.run == Run::Done) s.pomodoro.advance();
  s.alert = Alert::None;
  s.alertAtMs = 0;
  setTimerAlert(false);
  save();
}

uint32_t secondsToNextEnd() {
  const State& s = state();
  const int64_t now = nowMs();
  int64_t next = 0;
  for (const Span* span : {&s.countdown, &s.pomodoro.span}) {
    if (span->run != Run::Running) continue;
    const int64_t left = std::max<int64_t>(0, span->endMs - now);
    if (next == 0 || left < next) next = std::max<int64_t>(1, left);
  }
  return next == 0 ? 0 : static_cast<uint32_t>((next + 999) / 1000);
}

std::string format(const int64_t ms, const bool tenths, const bool countUp) {
  const int64_t total = std::max<int64_t>(0, ms);
  const int64_t seconds = tenths || countUp ? total / 1000 : (total + 999) / 1000;
  const long h = static_cast<long>(seconds / 3600);
  const int m = static_cast<int>(seconds / 60 % 60);
  const int sec = static_cast<int>(seconds % 60);
  char out[24];
  if (h > 0) {
    snprintf(out, sizeof(out), "%ld:%02d:%02d", h, m, sec);
  } else {
    snprintf(out, sizeof(out), "%d:%02d", m, sec);
  }
  if (tenths) {
    const size_t len = strlen(out);
    snprintf(out + len, sizeof(out) - len, ".%d", static_cast<int>(total / 100 % 10));
  }
  return out;
}

const char* phaseName(const Phase phase) {
  switch (phase) {
    case Phase::ShortBreak:
      return "Short break";
    case Phase::LongBreak:
      return "Long break";
    case Phase::Work:
      break;
  }
  return "Work";
}

}  // namespace fos::timers
