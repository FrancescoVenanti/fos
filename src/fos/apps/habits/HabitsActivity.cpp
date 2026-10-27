#include "HabitsActivity.h"

#include <ArduinoJson.h>

#include <algorithm>
#include <cstdio>
#include <ctime>

#include "fos/core/Json.h"
#include "fos/core/Status.h"
#include "fos/ui/TextEntryActivity.h"

namespace fos::habits {

using namespace ui;
using namespace ui::theme;

namespace {

constexpr char DATA_FILE[] = "/fos/habits/habits.json";
constexpr const char* TABS[] = {"Today", "Last 5 weeks"};
constexpr int ROW = 76;
constexpr int WEEKS = 5;
constexpr size_t MAX_HABITS = 12;
constexpr size_t KEEP_DAYS = 400;  // per habit; older days are dropped

std::string dayString(const int offset) {
  std::tm t{};
  if (!localNow(t)) return {};
  t.tm_mday += offset;
  t.tm_hour = 12;
  std::mktime(&t);
  char out[16];
  snprintf(out, sizeof(out), "%04d-%02d-%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
  return out;
}

}  // namespace

HabitsActivity::HabitsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : FosActivity("Habits", renderer, mappedInput) {}

void HabitsActivity::onEnter() {
  load();
  rebuild();
  FosActivity::onEnter();
}

void HabitsActivity::load() {
  habits_.clear();
  JsonDocument doc;
  if (!loadJson(DATA_FILE, doc)) return;
  for (JsonObjectConst h : doc["habits"].as<JsonArrayConst>()) {
    Habit habit;
    habit.name = h["name"] | "";
    for (JsonVariantConst d : h["done"].as<JsonArrayConst>()) habit.done.emplace_back(d | "");
    std::sort(habit.done.begin(), habit.done.end());
    if (!habit.name.empty()) habits_.push_back(std::move(habit));
  }
}

void HabitsActivity::save() const {
  JsonDocument doc;
  JsonArray arr = doc["habits"].to<JsonArray>();
  for (const Habit& h : habits_) {
    JsonObject o = arr.add<JsonObject>();
    o["name"] = h.name;
    JsonArray done = o["done"].to<JsonArray>();
    const size_t from = h.done.size() > KEEP_DAYS ? h.done.size() - KEEP_DAYS : 0;
    for (size_t i = from; i < h.done.size(); ++i) done.add(h.done[i]);
  }
  saveJson(DATA_FILE, doc);
}

bool HabitsActivity::doneOn(const Habit& h, const std::string& day) const {
  return std::binary_search(h.done.begin(), h.done.end(), day);
}

int HabitsActivity::streak(const Habit& h) const {
  // Days in a row up to today; today not done yet does not break it.
  int n = 0;
  int offset = doneOn(h, today_) ? 0 : -1;
  for (;; --offset) {
    const std::string day = dayString(offset);
    if (day.empty() || !doneOn(h, day)) break;
    ++n;
  }
  return n;
}

void HabitsActivity::rebuild() {
  today_ = dayString(0);
  days_.clear();
  // The grid starts on a Monday, WEEKS weeks back, and ends today.
  std::tm t{};
  int weekday = 1;
  if (localNow(t)) weekday = (t.tm_wday + 6) % 7;  // Monday = 0
  const int span = (WEEKS - 1) * 7 + weekday;
  for (int i = -span; i <= 0; ++i) days_.push_back(dayString(i));

  details_.clear();
  int doneToday = 0;
  for (const Habit& h : habits_) {
    const int s = streak(h);
    char buf[48];
    if (s == 0) {
      snprintf(buf, sizeof(buf), "No streak yet");
    } else {
      snprintf(buf, sizeof(buf), "%d %s in a row", s, s == 1 ? "day" : "days");
    }
    details_.emplace_back(buf);
    doneToday += doneOn(h, today_) ? 1 : 0;
  }
  char sum[48];
  snprintf(sum, sizeof(sum), "%d of %d done today", doneToday, static_cast<int>(habits_.size()));
  summary_ = sum;
  const int pages = std::max(1, (static_cast<int>(habits_.size()) + perPage() - 1) / perPage());
  if (page_ >= pages) page_ = pages - 1;
}

int HabitsActivity::perPage() const {
  const int h = bodyRect().h - HEADER_H - BORDER - GAP - TAB_H - GAP - 40 - (MIN_TARGET + SHADOW + GUTTER * 2) -
                (MIN_TARGET + SHADOW + GAP);
  return std::max(1, h / ROW);
}

void HabitsActivity::draw(Canvas& c, const Rect& body) {
  Rect area = body;
  header(c, area.takeTop(HEADER_H), "Habits", BACK, nullptr, ADD, Glyph::Plus);
  rule(c, 0, area.y, c.width());
  area.takeTop(BORDER + GAP);
  segmented(c, area.takeTop(TAB_H).inset(GUTTER, 0), TABS, 2, tab_, TAB);
  area.takeTop(GAP);
  if (habits_.empty()) {
    emptyState(c, area, "No habits yet", "Tap + to add one, like \"Read 20 minutes\" or \"No sugar\".");
    return;
  }
  if (tab_ == 0) {
    drawToday(c, area);
  } else {
    drawGrid(c, area);
  }
  if (confirmDelete_ >= 0) {
    dialog(c, "Delete habit?", confirmText_.c_str(), "Delete", DIALOG_OK, "Cancel", DIALOG_CANCEL);
  }
}

void HabitsActivity::drawToday(Canvas& c, Rect area) {
  c.textIn(area.takeTop(40).inset(GUTTER, 0), font::BODY, summary_.c_str(), Tone::Dark, Align::Left, true);
  rule(c, 0, area.y, c.width());
  area.takeTop(BORDER);
  const int per = perPage();
  const int pages = (static_cast<int>(habits_.size()) + per - 1) / per;
  if (pages > 1) {
    const Rect p = area.takeBottom(MIN_TARGET + SHADOW + GAP);
    pager(c, {p.x + GUTTER, p.y + GAP / 2, p.w - 2 * GUTTER, MIN_TARGET + SHADOW - GAP / 2}, page_, pages, PREV_PAGE,
          NEXT_PAGE);
  }
  for (int i = page_ * per; i < std::min(static_cast<int>(habits_.size()), (page_ + 1) * per); ++i) {
    const Rect r = area.takeTop(ROW);
    const bool done = doneOn(habits_[i], today_);
    const bool down = c.pressed(CHECK, static_cast<int16_t>(i));
    // Done today: the whole row inverted, the thing to see at a glance.
    const bool dark = done != down;
    c.fill(r, dark ? Tone::Black : Tone::White);
    rule(c, r.x, r.bottom() - BORDER, r.w);
    const Tone ink = dark ? Tone::White : Tone::Black;
    const int box = 40;
    const Rect b{r.x + GUTTER, r.y + (r.h - BORDER - box) / 2, box, box};
    c.fill(b, dark ? Tone::White : Tone::White);
    c.frame(b, BORDER, Tone::Black);
    if (done) drawGlyph(c, b.inset(6), Glyph::Check, Tone::Black);
    Rect text{b.right() + GAP + 4, r.y, r.right() - b.right() - GAP - GUTTER, r.h - BORDER};
    const int th = c.lineHeight(font::BODY);
    const int dh = c.lineHeight(font::SMALL);
    const int top = text.y + (text.h - th - dh) / 2;
    c.textIn({text.x, top, text.w, th}, font::BODY, habits_[i].name.c_str(), ink, Align::Left, true);
    c.textIn({text.x, top + th, text.w, dh}, font::SMALL, details_[i].c_str(), dark ? Tone::White : Tone::Dark);
    c.hit(r, CHECK, static_cast<int16_t>(i));
  }
}

void HabitsActivity::drawGrid(Canvas& c, Rect area) const {
  area = area.inset(GUTTER, 0);
  // Weekday letters above the columns, then one row per habit: name and a
  // strip of the last weeks, Monday-aligned.
  const int cols = static_cast<int>(days_.size());
  const int cell = std::max(8, std::min(12, (area.w - 4) / std::max(1, cols)));
  const int gridW = cell * cols;
  const int rowH = c.lineHeight(font::SMALL) + cell + 14;
  c.textIn(area.takeTop(c.lineHeight(font::SMALL)), font::SMALL, "Each square is a day, the last on the right is today",
           Tone::Dark);
  area.takeTop(GAP);
  const int per = std::max(1, area.h / rowH);
  for (int i = 0; i < std::min(static_cast<int>(habits_.size()), per); ++i) {
    Rect r = area.takeTop(rowH);
    c.textIn(r.takeTop(c.lineHeight(font::SMALL)), font::SMALL, habits_[i].name.c_str(), Tone::Black, Align::Left,
             true);
    r.takeTop(4);
    const int x0 = r.x;
    for (int d = 0; d < cols; ++d) {
      const Rect sq{x0 + d * cell, r.y, cell - 2, cell - 2};
      const bool done = doneOn(habits_[i], days_[d]);
      // Weeks alternate a light band so they read as groups of seven.
      if (!done && (d / 7) % 2 == 1) c.fill(sq, Tone::Light);
      if (done) c.fill(sq, Tone::Black);
      if (d == cols - 1) c.frame(sq.inset(-2), 2, Tone::Black);
    }
    (void)gridW;
  }
}

void HabitsActivity::onAction(const uint16_t action, const int16_t value) {
  if (confirmDelete_ >= 0) {
    if (action == DIALOG_OK && confirmDelete_ < static_cast<int>(habits_.size())) {
      habits_.erase(habits_.begin() + confirmDelete_);
      save();
    }
    confirmDelete_ = -1;
    rebuild();
    refresh(true);
    return;
  }
  switch (action) {
    case BACK:
      onBack();
      return;
    case TAB:
      tab_ = value;
      return;
    case ADD:
      if (habits_.size() >= MAX_HABITS) return;
      startActivityForResult(std::make_unique<TextEntryActivity>(renderer, mappedInput, "New habit", "", false, 40,
                                                                 "Long-press a habit to delete it."),
                             [this](const ActivityResult& result) {
                               const auto* text = std::get_if<KeyboardResult>(&result.data);
                               if (result.isCancelled || !text || text->text.empty()) return;
                               habits_.push_back({text->text, {}});
                               save();
                               rebuild();
                             });
      return;
    case CHECK: {
      if (value < 0 || value >= static_cast<int>(habits_.size()) || today_.empty()) return;
      auto& done = habits_[value].done;
      const auto it = std::lower_bound(done.begin(), done.end(), today_);
      if (it != done.end() && *it == today_) {
        done.erase(it);
      } else {
        done.insert(it, today_);
      }
      save();
      rebuild();
      return;
    }
    case PREV_PAGE:
      onUp();
      return;
    case NEXT_PAGE:
      onDown();
      return;
    default:
      return;
  }
}

bool HabitsActivity::onLongPress(const uint16_t action, const int16_t value) {
  if (action != CHECK || value < 0 || value >= static_cast<int>(habits_.size())) return false;
  confirmDelete_ = value;
  confirmText_ = "\"" + habits_[value].name + "\" and its history are removed.";
  return true;
}

bool HabitsActivity::onUp() {
  if (tab_ == 0 && page_ > 0) {
    --page_;
    return true;
  }
  return false;
}

bool HabitsActivity::onDown() {
  if (tab_ == 0 && (page_ + 1) * perPage() < static_cast<int>(habits_.size())) {
    ++page_;
    return true;
  }
  return false;
}

void HabitsActivity::onTick() {
  // Past midnight the checklist starts a new day.
  if (!today_.empty() && dayString(0) != today_) {
    rebuild();
    refresh(true);
  }
}

}  // namespace fos::habits
