#include "NewTransactionActivity.h"

#include <cstdio>

#include "fos/core/Status.h"
#include "fos/ui/TextEntryActivity.h"

namespace fos::budget {

using namespace ui;
using namespace ui::theme;

namespace {

constexpr const char* STEP_NAMES[] = {"Type", "Amount", "Category", "Description", "Date", "Save"};
constexpr int OLDEST_DAY = -60;
constexpr size_t MAX_DESCRIPTION = 60;

}  // namespace

void drawMoney(Canvas& c, const Rect& r, const char* digits, const Tone tone) {
  constexpr const char* EURO = "\xE2\x82\xAC";
  constexpr int SPACE = 10;
  const int ew = c.textWidth(font::LARGE, EURO, true);
  int fontId = font::JUMBO;
  int dw = c.textWidth(fontId, digits, true);
  if (dw + SPACE + ew > r.w) {
    fontId = font::LARGE;
    dw = c.textWidth(fontId, digits, true);
  }
  const int baseline = r.y + (r.h - c.lineHeight(fontId)) / 2 + c.ascender(fontId);
  const int ex = r.right() - ew;
  c.text(fontId, ex - SPACE - dw, baseline - c.ascender(fontId), digits, tone, true);
  c.text(font::LARGE, ex, baseline - c.ascender(font::LARGE), EURO, tone, true);
}

NewTransactionActivity::NewTransactionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                               std::vector<Category> categories, Month month, const bool online)
    : FosActivity("NewTransaction", renderer, mappedInput),
      categories_(std::move(categories)),
      month_(std::move(month)),
      online_(online),
      keypad_({}, 2, 9) {
  setDay(0);
  updateTexts();
}

const char* NewTransactionActivity::categoryName(const std::string& id) const {
  for (const Category& cat : categories_) {
    if (cat.id == id) return cat.name.c_str();
  }
  return id.c_str();
}

void NewTransactionActivity::go(const Step step) {
  step_ = step;
  if (step == Step::Category) {
    shown_.clear();
    for (int i = 0; i < static_cast<int>(categories_.size()); ++i) {
      if (categories_[i].income == entry_.income) shown_.push_back(i);
    }
  } else if (step == Step::Description) {
    recent_ = recentDescriptions(entry_.category, &month_);
  }
  updateTexts();
}

void NewTransactionActivity::setDay(const int offsetFromToday) {
  dayOffset_ = std::max(OLDEST_DAY, std::min(0, offsetFromToday));
  std::tm t{};
  if (!localNow(t)) {
    // No clock yet: the backend dates entries without a date.
    entry_.date.clear();
    snprintf(dateText_, sizeof(dateText_), "Date unknown");
    return;
  }
  t.tm_mday += dayOffset_;
  t.tm_hour = 12;  // away from DST edges
  std::mktime(&t);
  char iso[16];
  snprintf(iso, sizeof(iso), "%04d-%02d-%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
  entry_.date = iso;
  snprintf(dateText_, sizeof(dateText_), "%.3s %d %s", weekdayName(t.tm_wday), t.tm_mday, monthName(t.tm_mon));
}

void NewTransactionActivity::updateTexts() {
  const int index = static_cast<int>(step_);
  snprintf(stepText_, sizeof(stepText_), "STEP %d OF %d  \xC2\xB7  %s", index + 1, STEPS, STEP_NAMES[index]);
  entry_.amount = keypad_.scaled(2);
  if (step_ == Step::Review) {
    amountText_ = (entry_.income ? "+" : "\xE2\x88\x92") + formatDecimal(entry_.amount);
    return;
  }
  // Typed amount as entered (no thousands grouping while typing).
  amountText_ = keypad_.text().empty() ? "0" : keypad_.text();
  for (char& ch : amountText_) {
    if (ch == '.') ch = decimalSeparator();
  }
}

void NewTransactionActivity::draw(Canvas& c, const Rect& body) {
  Rect area = body;
  header(c, area.takeTop(HEADER_H), entry_.income ? "New income" : "New expense", BACK);
  rule(c, 0, area.y, c.width());
  area.takeTop(BORDER + 10);
  area = area.inset(GUTTER, 0);

  // Step strip: label and a thin progress bar.
  c.textIn(area.takeTop(26), font::SMALL, stepText_, Tone::Dark, Align::Left, true);
  progressBar(c, area.takeTop(14), static_cast<int>(step_) + 1, STEPS, Tone::Black);
  area.takeTop(GAP + 4);
  area.takeBottom(GUTTER);

  switch (step_) {
    case Step::Type:
      drawType(c, area);
      break;
    case Step::Amount:
      drawAmount(c, area);
      break;
    case Step::Category:
      drawCategory(c, area);
      break;
    case Step::Description:
      drawDescription(c, area);
      break;
    case Step::Date:
      drawDate(c, area);
      break;
    case Step::Review:
      drawReview(c, area);
      break;
  }
}

void NewTransactionActivity::drawType(Canvas& c, Rect area) {
  const int h = (area.h - GAP) / 2;
  const struct {
    const char* label;
    const char* detail;
    Glyph glyph;
  } options[] = {{"Expense", "Money going out", Glyph::Minus}, {"Income", "Money coming in", Glyph::Plus}};
  for (int i = 0; i < 2; ++i) {
    const Rect r = area.takeTop(h);
    area.takeTop(GAP);
    const Rect face = block(c, r, i == 0 ? Tone::White : Tone::Light, TYPE, static_cast<int16_t>(i));
    const int glyphSize = 72;
    const Rect glyph{face.x + (face.w - glyphSize) / 2, face.y + face.h / 2 - glyphSize - 8, glyphSize, glyphSize};
    c.fill(glyph, Tone::Black);
    drawGlyph(c, glyph.inset(14), options[i].glyph, Tone::White);
    const int th = c.lineHeight(font::LARGE);
    c.textIn({face.x, face.y + face.h / 2 + 4, face.w, th}, font::LARGE, options[i].label, Tone::Black, Align::Center,
             true);
    c.textIn({face.x, face.y + face.h / 2 + 4 + th, face.w, c.lineHeight(font::SMALL)}, font::SMALL, options[i].detail,
             Tone::Dark, Align::Center, true);
  }
}

void NewTransactionActivity::drawAmount(Canvas& c, Rect area) {
  const Rect figure = area.takeTop(112);
  c.fill(figure, Tone::Black);
  Rect inner = figure.inset(BORDER + 12, 8);
  c.textIn(inner.takeTop(c.lineHeight(font::SMALL)), font::SMALL, entry_.income ? "INCOME" : "EXPENSE", Tone::White,
           Align::Left, true);
  drawMoney(c, inner, amountText_.c_str(), Tone::White);
  area.takeTop(GAP + 4);

  const Rect next = area.takeBottom(ACTION_H);
  area.takeBottom(GAP + 4);
  keypad_.draw(c, area, KEY);
  if (entry_.amount > 0) {
    button(c, next, "Next", NEXT, ButtonStyle::Primary, 0, font::TITLE);
  } else {
    button(c, next, "Type an amount", 0, ButtonStyle::Quiet, 0, font::BODY);
  }
}

void NewTransactionActivity::drawCategory(Canvas& c, Rect area) {
  if (shown_.empty()) {
    emptyState(c, area, "No categories yet", "Connect once so Budgeteer can download your categories.");
    return;
  }
  constexpr int ROW = 68;
  const int cols = 2;
  const int w = (area.w - GAP) / cols;
  for (size_t i = 0; i < shown_.size(); ++i) {
    const Rect r{area.x + static_cast<int>(i % cols) * (w + GAP), area.y + static_cast<int>(i / cols) * (ROW + GAP), w,
                 ROW};
    if (r.bottom() > area.bottom()) break;
    const Category& cat = categories_[shown_[i]];
    const bool chosen = cat.id == entry_.category;
    button(c, r, cat.name.c_str(), CATEGORY, chosen ? ButtonStyle::Primary : ButtonStyle::Secondary,
           static_cast<int16_t>(i));
  }
}

void NewTransactionActivity::drawDescription(Canvas& c, Rect area) {
  char label[64];
  snprintf(label, sizeof(label), "RECENT IN %s", categoryName(entry_.category));
  sectionLabel(c, area.takeTop(32), label);
  if (recent_.empty()) {
    c.textIn(area.takeTop(40), font::BODY, "Nothing yet for this category.", Tone::Dark);
  } else {
    int x = area.x;
    int y = area.y;
    for (size_t i = 0; i < recent_.size(); ++i) {
      const int w = std::min(area.w, chipWidth(c, recent_[i].c_str()));
      if (x > area.x && x + w > area.right()) {
        x = area.x;
        y += CHIP_H + GAP;
      }
      chip(c, x, y, recent_[i].c_str(), recent_[i] == entry_.description, CHIP, static_cast<int16_t>(i));
      x += w + GAP;
    }
    area.takeTop(y + CHIP_H - area.y);
  }
  area.takeTop(GAP * 2);
  button(c, area.takeTop(MIN_TARGET + SHADOW), "Type a description", TYPE_TEXT);
  area.takeTop(GAP);
  button(c, area.takeTop(MIN_TARGET), "Skip", SKIP, ButtonStyle::Quiet);
}

void NewTransactionActivity::drawDate(Canvas& c, Rect area) {
  const Rect card = area.takeTop(150);
  const Rect face = block(c, card, Tone::White);
  const int size = MIN_TARGET + SHADOW;
  const int y = face.y + (face.h - size) / 2;
  glyphButton(c, {face.x + 12, y, size, size}, Glyph::Back, DAY_PREV);
  if (dayOffset_ < 0) glyphButton(c, {face.right() - 12 - size, y, size, size}, Glyph::Forward, DAY_NEXT);
  const Rect mid{face.x + size + 24, face.y + 16, face.w - 2 * (size + 24), face.h - 32};
  const char* rel = dayOffset_ == 0 ? "Today" : dayOffset_ == -1 ? "Yesterday" : "Earlier";
  const int th = c.lineHeight(font::TITLE);
  c.textIn({mid.x, mid.y + mid.h / 2 - th, mid.w, th}, font::TITLE, rel, Tone::Black, Align::Center, true);
  c.textIn({mid.x, mid.y + mid.h / 2, mid.w, c.lineHeight(font::BODY)}, font::BODY, dateText_, Tone::Dark,
           Align::Center, true);
  area.takeTop(GAP * 2);

  const int x = area.x;
  const int w = chip(c, x, area.y, "Today", dayOffset_ == 0, TODAY);
  chip(c, x + w + GAP, area.y, "Yesterday", dayOffset_ == -1, YESTERDAY);

  button(c, area.takeBottom(ACTION_H), "Next", NEXT, ButtonStyle::Primary, 0, font::TITLE);
}

void NewTransactionActivity::drawReview(Canvas& c, Rect area) {
  const Rect amount = area.takeTop(112);
  c.fill(amount, Tone::Black);
  Rect inner = amount.inset(BORDER + 12, 8);
  c.textIn(inner.takeTop(c.lineHeight(font::SMALL)), font::SMALL, entry_.income ? "INCOME" : "EXPENSE", Tone::White,
           Align::Left, true);
  drawMoney(c, inner, amountText_.c_str(), Tone::White);
  area.takeTop(GAP);

  const struct {
    const char* label;
    const char* value;
  } rows[] = {{"Category", categoryName(entry_.category)},
              {"Description", entry_.description.empty() ? "None" : entry_.description.c_str()},
              {"Date", dateText_}};
  for (const auto& row : rows) {
    const Rect r = area.takeTop(ROW_H - 8);
    listRow(c, {0, r.y, c.width(), r.h}, row.label, nullptr, row.value, 0, 0, false);
  }

  const Rect save = area.takeBottom(ACTION_H);
  area.takeBottom(GAP);
  const char* note = online_ ? "Sent to the backend now. If that fails it waits in the queue."
                             : "You are offline. It waits in the queue and goes out on the next sync.";
  c.textWrapped(area.takeBottom(c.lineHeight(font::BODY) * 2 + 4), font::BODY, note, Tone::Dark, 2);
  button(c, save, "Save", SAVE, ButtonStyle::Primary, 0, font::TITLE);
}

void NewTransactionActivity::onAction(const uint16_t action, const int16_t value) {
  switch (action) {
    case BACK:
      onBack();
      return;
    case TYPE:
      entry_.income = value == 1;
      entry_.category.clear();
      go(Step::Amount);
      return;
    case KEY:
      if (keypad_.press(value) == Keypad::Event::Changed) updateTexts();
      return;
    case NEXT:
      if (step_ == Step::Amount && entry_.amount > 0) go(Step::Category);
      if (step_ == Step::Date) go(Step::Review);
      return;
    case CATEGORY:
      if (value >= 0 && value < static_cast<int>(shown_.size())) {
        const std::string id = categories_[shown_[value]].id;
        if (id != entry_.category) entry_.description.clear();
        entry_.category = id;
        go(Step::Description);
      }
      return;
    case CHIP:
      if (value >= 0 && value < static_cast<int>(recent_.size())) {
        entry_.description = recent_[value];
        go(Step::Date);
      }
      return;
    case SKIP:
      entry_.description.clear();
      go(Step::Date);
      return;
    case TYPE_TEXT:
      startActivityForResult(std::make_unique<TextEntryActivity>(renderer, mappedInput, "Description",
                                                                 entry_.description, false, MAX_DESCRIPTION),
                             [this](const ActivityResult& result) {
                               if (result.isCancelled) return;
                               const auto* text = std::get_if<KeyboardResult>(&result.data);
                               if (!text) return;
                               entry_.description = text->text;
                               go(Step::Date);
                             });
      return;
    case DAY_PREV:
      setDay(dayOffset_ - 1);
      return;
    case DAY_NEXT:
      setDay(dayOffset_ + 1);
      return;
    case TODAY:
      setDay(0);
      return;
    case YESTERDAY:
      setDay(-1);
      return;
    case SAVE:
      if (enqueue(entry_)) {
        setResult(ActivityResult{});
        finish();
      }
      return;
    default:
      return;
  }
}

void NewTransactionActivity::onBack() {
  if (step_ == Step::Type) {
    ActivityResult cancelled;
    cancelled.isCancelled = true;
    setResult(std::move(cancelled));
    finish();
    return;
  }
  go(static_cast<Step>(static_cast<int>(step_) - 1));
  requestUpdate();
}

}  // namespace fos::budget
