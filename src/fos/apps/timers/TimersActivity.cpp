#include "TimersActivity.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>

#include "fos/core/Status.h"

namespace fos::timers {

using namespace ui;
using namespace ui::theme;

namespace {

constexpr const char* TABS[] = {"Pomodoro", "Stopwatch", "Countdown"};
constexpr int TAB_COUNT = 3;
constexpr int DISPLAY_H = 196;
constexpr int LAP_ROWS = 5;
constexpr int LAP_H = 44;
constexpr int STEP_H = 64;
constexpr int MAX_SCALE = 512;

struct Preset {
  const char* label;
  int seconds;
};
constexpr Preset PRESETS[] = {{"1 min", 60},   {"3 min", 180},   {"5 min", 300}, {"10 min", 600},
                              {"15 min", 900}, {"30 min", 1800}, {"1 h", 3600}};
constexpr int PRESET_COUNT = sizeof(PRESETS) / sizeof(PRESETS[0]);
constexpr struct {
  const char* label;
  int seconds;
} ADJUSTS[] = {{"-1 min", -60}, {"+1 min", 60}, {"-10 s", -10}, {"+10 s", 10}};
constexpr int64_t MAX_COUNTDOWN_MS = 24 * 3600 * 1000LL - 1000;

constexpr const char* POMO_ROWS[] = {"Work", "Short break", "Long break", "Rounds"};

// "20:45" for an epoch-ms time.
std::string clockAt(const int64_t ms) {
  const std::time_t t = static_cast<std::time_t>(ms / 1000);
  std::tm tm{};
  localtime_r(&t, &tm);
  char out[12];
  formatClock(tm, out, sizeof(out));
  return out;
}

// Big figure centered in r: the jumbo font scaled up to fit.
void bigFigure(Canvas& c, const Rect& r, const char* text, const Tone tone) {
  const int base = std::max(1, c.scaledWidth(font::JUMBO, text, 256));
  const int ascender = std::max(1, c.ascender(font::JUMBO));
  const int scale = std::max(256, std::min({MAX_SCALE, r.w * 256 / base, r.h * 256 / ascender}));
  const int w = c.scaledWidth(font::JUMBO, text, scale);
  c.scaledText(font::JUMBO, r.x + (r.w - w) / 2, r.y + (r.h + ascender * scale / 256) / 2, text, scale, tone);
}

}  // namespace

TimersActivity::TimersActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : FosActivity("Timers", renderer, mappedInput) {}

void TimersActivity::onEnter() {
  State& s = state();
  update();
  // An ended timer opens on its own tab.
  if (s.alert == Alert::Countdown) s.tab = static_cast<uint8_t>(Tab::Countdown);
  if (s.alert == Alert::Pomodoro) s.tab = static_cast<uint8_t>(Tab::Pomodoro);
  if (s.tab >= TAB_COUNT) s.tab = 0;
  updateTexts(nowMs());
  FosActivity::onEnter();
}

const Span* TimersActivity::activeSpan() const {
  switch (tab()) {
    case Tab::Pomodoro:
      return &state().pomodoro.span;
    case Tab::Countdown:
      return &state().countdown;
    case Tab::Stopwatch:
      break;
  }
  return nullptr;
}

bool TimersActivity::running() const {
  const Span* span = activeSpan();
  return span ? span->run == Run::Running : state().stopwatch.running;
}

void TimersActivity::updateTexts(const int64_t now) {
  const State& s = state();
  shownKey_ = now / 1000;
  note_.clear();
  secondary_ = nullptr;
  done_ = false;

  if (tab() == Tab::Stopwatch) {
    const Stopwatch& sw = s.stopwatch;
    const int64_t elapsed = sw.elapsed(now);
    big_ = format(elapsed, false, true);
    inverted_ = sw.running;
    label_ = sw.running ? "RUNNING" : elapsed > 0 ? "PAUSED" : "STOPWATCH";
    if (sw.running) {
      primary_ = "Pause";
      secondary_ = sw.laps.size() < MAX_LAPS ? "Lap" : nullptr;
    } else {
      primary_ = elapsed > 0 ? "Resume" : "Start";
      secondary_ = elapsed > 0 ? "Reset" : nullptr;
    }
    // Newest lap first: number, lap time, total.
    lapCount_ = std::min<int>(LAP_ROWS, static_cast<int>(sw.laps.size()));
    for (int i = 0; i < lapCount_; ++i) {
      const size_t index = sw.laps.size() - 1 - i;
      const int64_t total = sw.laps[index];
      const int64_t lap = total - (index > 0 ? sw.laps[index - 1] : 0);
      lapRows_[i][0] = "Lap " + std::to_string(index + 1);
      lapRows_[i][1] = format(lap, true);
      lapRows_[i][2] = format(total, true);
    }
    return;
  }

  if (tab() == Tab::Countdown) {
    const Span& cd = s.countdown;
    done_ = cd.run == Run::Done;
    inverted_ = cd.run == Run::Running || done_;
    if (done_) {
      big_ = "TIME'S UP";
      label_ = "COUNTDOWN";
      note_ = format(cd.durationMs) + " ended at " + clockAt(s.alertAtMs ? s.alertAtMs : now);
      primary_ = "Dismiss";
      return;
    }
    big_ = format(cd.remaining(now));
    label_ = cd.run == Run::Running ? "ENDS " + clockAt(cd.endMs) : cd.run == Run::Paused ? "PAUSED" : "COUNTDOWN";
    primary_ = cd.run == Run::Running ? "Pause" : cd.run == Run::Paused ? "Resume" : "Start";
    if (cd.run != Run::Idle) secondary_ = "Reset";
    return;
  }

  const Pomodoro& p = s.pomodoro;
  done_ = p.span.run == Run::Done;
  inverted_ = p.span.run == Run::Running || done_;
  char label[48];
  std::string phase = phaseName(p.phase);
  for (char& ch : phase) ch = static_cast<char>(toupper(static_cast<unsigned char>(ch)));
  snprintf(label, sizeof(label), "%s  \xC2\xB7  ROUND %d OF %d", phase.c_str(), p.round, p.rounds);
  label_ = label;
  if (done_) {
    big_ = p.phase == Phase::Work ? "BREAK TIME" : "BACK TO WORK";
    const Phase next = p.nextPhase();
    note_ = std::string(phaseName(p.phase)) + " ended at " + clockAt(s.alertAtMs ? s.alertAtMs : now);
    primary_ = next == Phase::Work ? "Start work" : next == Phase::LongBreak ? "Long break" : "Start break";
    secondary_ = "Later";
  } else {
    big_ = format(p.span.remaining(now));
    if (p.span.run == Run::Running) note_ = "Ends " + clockAt(p.span.endMs);
    primary_ = p.span.run == Run::Running ? "Pause" : p.span.run == Run::Paused ? "Resume" : "Start";
    secondary_ = "Skip";
  }
  const int values[] = {p.workMin, p.shortMin, p.longMin, p.rounds};
  for (int i = 0; i < 4; ++i) pomoValues_[i] = std::to_string(values[i]) + (i < 3 ? " min" : "");
}

void TimersActivity::draw(Canvas& c, const Rect& body) {
  Rect area = body;
  header(c, area.takeTop(HEADER_H), "Timers", BACK);
  rule(c, 0, area.y, c.width());
  area.takeTop(BORDER + GAP);
  area = area.inset(GUTTER, 0);

  segmented(c, area.takeTop(TAB_H), TABS, TAB_COUNT, state().tab, TAB);
  area.takeTop(GAP + 4);
  drawDisplay(c, area.takeTop(DISPLAY_H));
  area.takeTop(GAP + 4);

  const Rect actions = area.takeBottom(ACTION_H + GUTTER);
  drawActions(c, {actions.x, actions.y, actions.w, ACTION_H});
  area.takeBottom(GAP);

  if (done_) return;
  switch (tab()) {
    case Tab::Pomodoro:
      drawPomodoro(c, area);
      break;
    case Tab::Stopwatch:
      drawStopwatch(c, area);
      break;
    case Tab::Countdown:
      drawCountdown(c, area);
      break;
  }
}

void TimersActivity::drawDisplay(Canvas& c, const Rect& r) const {
  Rect face;
  if (inverted_) {
    c.fill(r, Tone::Black);
    face = r.inset(BORDER + 12, 10);
  } else {
    face = block(c, r, Tone::White).inset(12, 8);
  }
  const Tone ink = inverted_ ? Tone::White : Tone::Black;
  c.textIn(face.takeTop(c.lineHeight(font::SMALL)), font::SMALL, label_.c_str(), ink, Align::Left, true);
  if (!note_.empty()) {
    c.textIn(face.takeBottom(c.lineHeight(font::BODY)), font::BODY, note_.c_str(), ink, Align::Center, true);
  }
  if (done_) {
    c.textIn(face, font::LARGE, big_.c_str(), ink, Align::Center, true);
  } else {
    bigFigure(c, face.inset(8, 4), big_.c_str(), ink);
  }
}

void TimersActivity::drawActions(Canvas& c, const Rect& r) const {
  if (!secondary_) {
    button(c, r, primary_, PRIMARY, ButtonStyle::Primary, 0, font::TITLE);
    return;
  }
  const int w = (r.w - GAP) * 2 / 5;
  button(c, {r.x, r.y, w, r.h}, secondary_, SECONDARY, ButtonStyle::Secondary, 0, font::TITLE);
  button(c, {r.x + w + GAP, r.y, r.w - w - GAP, r.h}, primary_, PRIMARY, ButtonStyle::Primary, 0, font::TITLE);
}

void TimersActivity::drawCountdown(Canvas& c, Rect area) {
  if (state().countdown.run != Run::Idle) {
    c.textWrapped(area.inset(0, 8), font::BODY,
                  "Keeps running while the device sleeps. The lock screen shows when it ends.", Tone::Dark, 3);
    return;
  }
  // Presets, wrapping.
  int x = area.x;
  int y = area.y;
  for (int i = 0; i < PRESET_COUNT; ++i) {
    const int w = chipWidth(c, PRESETS[i].label);
    if (x > area.x && x + w > area.right()) {
      x = area.x;
      y += CHIP_H + GAP;
    }
    const bool selected = state().countdown.durationMs == PRESETS[i].seconds * 1000LL;
    chip(c, x, y, PRESETS[i].label, selected, PRESET, static_cast<int16_t>(i));
    x += w + GAP;
  }
  area.takeTop(y + CHIP_H + GAP * 2 - area.y);
  const int count = sizeof(ADJUSTS) / sizeof(ADJUSTS[0]);
  const int w = (area.w - GAP * (count - 1)) / count;
  for (int i = 0; i < count; ++i) {
    button(c, {area.x + i * (w + GAP), area.y, w, MIN_TARGET + SHADOW}, ADJUSTS[i].label, ADJUST,
           ButtonStyle::Secondary, static_cast<int16_t>(ADJUSTS[i].seconds), font::SMALL);
  }
}

void TimersActivity::drawStopwatch(Canvas& c, Rect area) const {
  if (lapCount_ == 0) {
    c.textWrapped(area.inset(0, 8), font::BODY, "Lap marks the time of each split. Laps keep their tenths.", Tone::Dark,
                  3);
    return;
  }
  sectionLabel(c, area.takeTop(28).inset(-GUTTER, 0), "LAPS, NEWEST FIRST");
  rule(c, area.x, area.y, area.w);
  area.takeTop(BORDER);
  for (int i = 0; i < lapCount_ && area.h >= LAP_H; ++i) {
    Rect row = area.takeTop(LAP_H);
    rule(c, row.x, row.bottom() - 2, row.w, 2, Tone::Black);
    row.h -= 2;
    const int third = row.w / 3;
    c.textIn(row.takeLeft(third), font::BODY, lapRows_[i][0].c_str(), Tone::Black, Align::Left, true);
    c.textIn(row.takeLeft(third), font::BODY, lapRows_[i][1].c_str(), Tone::Black, Align::Right, true);
    c.textIn(row, font::BODY, lapRows_[i][2].c_str(), Tone::Dark, Align::Right);
  }
}

void TimersActivity::drawPomodoro(Canvas& c, Rect area) const {
  const Pomodoro& p = state().pomodoro;
  if (p.span.run != Run::Idle) {
    // Rounds as squares: filled = finished work rounds.
    const int size = 36;
    const int count = std::max<int>(1, p.rounds);
    const int total = count * size + (count - 1) * GAP;
    int x = area.x + (area.w - total) / 2;
    const int y = area.y + 24;
    for (int i = 0; i < count; ++i) {
      const Rect sq{x, y, size, size};
      const bool finished = i + 1 < p.round || (i + 1 == p.round && p.phase != Phase::Work);
      c.fill(sq, finished ? Tone::Black : Tone::White);
      c.frame(sq, BORDER, Tone::Black);
      if (i + 1 == p.round && p.phase == Phase::Work) c.fill(sq.inset(10), Tone::Black);
      x += size + GAP;
    }
    c.textIn({area.x, y + size + 16, area.w, c.lineHeight(font::BODY)}, font::BODY,
             "Keeps running while the device sleeps.", Tone::Dark, Align::Center);
    return;
  }
  // Lengths, only while stopped.
  for (int row = 0; row < 4 && area.h >= STEP_H; ++row) {
    Rect r = area.takeTop(STEP_H);
    area.takeTop(4);
    rule(c, r.x, r.bottom() - 2, r.w, 2, Tone::Black);
    const int size = STEP_H - 12;
    glyphButton(c, r.takeRight(size).inset(0, 4), Glyph::Plus, POMO_STEP, ButtonStyle::Secondary,
                static_cast<int16_t>(row * 2 + 1));
    r.takeRight(GAP);
    const Rect value = r.takeRight(96);
    glyphButton(c, r.takeRight(size).inset(0, 4), Glyph::Minus, POMO_STEP, ButtonStyle::Secondary,
                static_cast<int16_t>(row * 2));
    c.textIn(value, font::BODY, pomoValues_[row].c_str(), Tone::Black, Align::Center, true);
    c.textIn(r, font::BODY, POMO_ROWS[row], Tone::Black, Align::Left, true);
  }
}

void TimersActivity::onAction(const uint16_t action, const int16_t value) {
  State& s = state();
  const int64_t now = nowMs();
  switch (action) {
    case BACK:
      onBack();
      return;
    case TAB:
      if (value >= 0 && value < TAB_COUNT) s.tab = static_cast<uint8_t>(value);
      save();
      break;
    case PRESET:
      if (value >= 0 && value < PRESET_COUNT) s.countdown.durationMs = PRESETS[value].seconds * 1000LL;
      save();
      break;
    case ADJUST:
      s.countdown.durationMs = std::clamp<int64_t>(s.countdown.durationMs + value * 1000LL, 10000, MAX_COUNTDOWN_MS);
      save();
      break;
    case POMO_STEP: {
      Pomodoro& p = s.pomodoro;
      const int delta = value % 2 == 0 ? -1 : 1;
      switch (value / 2) {
        case 0:
          p.workMin = static_cast<uint8_t>(std::clamp(p.workMin + delta * 5, 5, 90));
          break;
        case 1:
          p.shortMin = static_cast<uint8_t>(std::clamp(p.shortMin + delta, 1, 30));
          break;
        case 2:
          p.longMin = static_cast<uint8_t>(std::clamp(p.longMin + delta * 5, 5, 60));
          break;
        default:
          p.rounds = static_cast<uint8_t>(std::clamp(p.rounds + delta, 1, 8));
          break;
      }
      p.syncLength();
      save();
      break;
    }
    case PRIMARY:
      if (tab() == Tab::Stopwatch) {
        Stopwatch& sw = s.stopwatch;
        if (sw.running) {
          sw.accumMs = sw.elapsed(now);
          sw.running = false;
        } else {
          sw.startMs = now;
          sw.running = true;
        }
      } else if (done_) {
        // Dismiss; for a pomodoro also start the next phase.
        const bool pomodoro = tab() == Tab::Pomodoro;
        dismiss();
        if (pomodoro) s.pomodoro.span.start(now);
      } else {
        Span& span = tab() == Tab::Pomodoro ? s.pomodoro.span : s.countdown;
        if (span.run == Run::Running) {
          span.pause(now);
        } else {
          span.start(now);
        }
      }
      save();
      refresh(true);
      break;
    case SECONDARY:
      if (tab() == Tab::Stopwatch) {
        Stopwatch& sw = s.stopwatch;
        if (sw.running) {
          if (sw.laps.size() < MAX_LAPS) sw.laps.push_back(sw.elapsed(now));
        } else {
          sw = Stopwatch();
        }
      } else if (tab() == Tab::Countdown) {
        s.countdown.reset();
      } else if (done_) {
        dismiss();  // "Later": next phase, stopped
      } else {
        s.pomodoro.advance();  // Skip
      }
      save();
      break;
    default:
      return;
  }
  updateTexts(now);
}

bool TimersActivity::onUp() {
  State& s = state();
  s.tab = static_cast<uint8_t>((s.tab + TAB_COUNT - 1) % TAB_COUNT);
  updateTexts(nowMs());
  return true;
}

bool TimersActivity::onDown() {
  State& s = state();
  s.tab = static_cast<uint8_t>((s.tab + 1) % TAB_COUNT);
  updateTexts(nowMs());
  return true;
}

void TimersActivity::onTick() {
  const int64_t now = nowMs();
  if (update(now)) {
    updateTexts(now);
    refresh(true);
    return;
  }
  // Running figures change once a second; repaint only then.
  if (running() && now / 1000 != shownKey_) {
    updateTexts(now);
    refresh();
  }
}

}  // namespace fos::timers
