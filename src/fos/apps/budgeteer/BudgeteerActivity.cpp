#include "BudgeteerActivity.h"

#include <cstdio>
#include <cstring>

#include "NewTransactionActivity.h"
#include "fos/core/Status.h"
#include "fos/sync/Sync.h"
#include "fos/ui/Keyboard.h"

namespace fos::budget {

using namespace ui;
using namespace ui::theme;

namespace {

constexpr const char* TABS[] = {"Summary", "Budgets", "List"};
constexpr int BUDGET_ROW_H = 60;
constexpr int LIST_ROW_H = 64;
constexpr int PAGER_H = MIN_TARGET + SHADOW + GAP;
constexpr const char* MINUS = "\xE2\x88\x92";  // U+2212, in the jumbo font
constexpr const char* EURO = " \xE2\x82\xAC";

std::string money(const long long cents, const char* sign = "") { return sign + formatDecimal(cents) + EURO; }

// "2026-10-26" -> "26 Oct".
std::string shortDate(const std::string& iso) {
  int y = 0;
  int m = 0;
  int d = 0;
  if (sscanf(iso.c_str(), "%d-%d-%d", &y, &m, &d) != 3 || m < 1 || m > 12) return iso;
  char out[16];
  snprintf(out, sizeof(out), "%d %.3s", d, monthName(m - 1));
  return out;
}

}  // namespace

BudgeteerActivity::BudgeteerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : FosActivity("Budgeteer", renderer, mappedInput) {}

void BudgeteerActivity::onEnter() {
  std::tm t{};
  if (localNow(t)) {
    year_ = t.tm_year + 1900;
    mon_ = t.tm_mon;
  } else {
    year_ = 2026;
    mon_ = 0;
  }
  formatYm(year_, mon_, ym_, sizeof(ym_));
  loadCategories(categories_);
  queued_ = queuedCount();
  buildRows();
  connect();
  FosActivity::onEnter();
}

void BudgeteerActivity::connect() {
  state_ = State::Checking;
  seenGen_ = sync::generation();
  // Sends the queue first, then refreshes categories; the month comes after.
  sync::request(AppId::Budgeteer);
  requestMonth(ym_);
}

bool BudgeteerActivity::isCurrentMonth() const {
  std::tm t{};
  if (!localNow(t)) return true;
  return year_ == t.tm_year + 1900 && mon_ == t.tm_mon;
}

const char* BudgeteerActivity::categoryName(const std::string& id) const {
  for (const Category& cat : categories_) {
    if (cat.id == id) return cat.name.c_str();
  }
  return id.c_str();
}

void BudgeteerActivity::reload() {
  loadCategories(categories_);
  loadMonth(ym_, month_);
  queued_ = queuedCount();
  buildRows();
}

void BudgeteerActivity::buildRows() {
  snprintf(monthTitle_, sizeof(monthTitle_), "%s %d", monthName(mon_), year_);
  if (queued_ == 0) {
    snprintf(queuedText_, sizeof(queuedText_), "Nothing waiting to sync");
  } else {
    snprintf(queuedText_, sizeof(queuedText_), "%d to sync", queued_);
  }
  balanceText_ =
      (month_.balance < 0 ? MINUS : "") + formatDecimal(month_.balance < 0 ? -month_.balance : month_.balance);
  incomeText_ = money(month_.income, "+");
  expenseText_ = money(month_.expense, MINUS);

  budgetRows_.clear();
  budgetRows_.reserve(month_.budgets.size());
  for (const BudgetLine& b : month_.budgets) {
    Row row;
    row.title = categoryName(b.category);
    row.value = formatDecimal(b.spent) + " / " + formatDecimal(b.budget, 0);
    row.spent = b.spent;
    row.budget = b.budget;
    budgetRows_.push_back(std::move(row));
  }

  // Entries still in the queue come first, marked as such.
  const std::vector<Entry> pending = queued();
  listRows_.clear();
  listRows_.reserve(pending.size() + month_.transactions.size());
  const auto add = [&](const Entry& e, const bool isQueued) {
    Row row;
    row.title = e.description.empty() ? categoryName(e.category) : e.description;
    row.detail = std::string(categoryName(e.category)) + "  \xC2\xB7  " + shortDate(e.date);
    if (isQueued) row.detail += "  \xC2\xB7  queued";
    row.value = (e.income ? "+" : "-") + formatDecimal(e.amount);
    listRows_.push_back(std::move(row));
  };
  for (auto it = pending.rbegin(); it != pending.rend(); ++it) {
    if (it->date.compare(0, 7, ym_) == 0) add(*it, true);
  }
  for (const Entry& e : month_.transactions) add(e, false);
}

Rect BudgeteerActivity::contentRect() const {
  Rect area = bodyRect();
  area.takeTop(HEADER_H + BORDER + GAP);
  area.takeTop(MIN_TARGET + SHADOW + GAP);  // month row
  area.takeTop(TAB_H + GAP);
  area.takeBottom(ACTION_H + GUTTER + GAP);
  return area.inset(GUTTER, 0);
}

int BudgeteerActivity::perPage(bool& paged) const {
  const int h = contentRect().h;
  const int rowH = tab_ == Tab::Budgets ? BUDGET_ROW_H : LIST_ROW_H;
  const int count = static_cast<int>(tab_ == Tab::Budgets ? budgetRows_.size() : listRows_.size());
  int per = std::max(1, h / rowH);
  paged = count > per;
  if (paged) per = std::max(1, (h - PAGER_H) / rowH);
  return per;
}

int BudgeteerActivity::pageCount() const {
  if (tab_ == Tab::Summary) return 1;
  bool paged = false;
  const int per = perPage(paged);
  const int count = static_cast<int>(tab_ == Tab::Budgets ? budgetRows_.size() : listRows_.size());
  return std::max(1, (count + per - 1) / per);
}

void BudgeteerActivity::draw(Canvas& c, const Rect& body) {
  Rect area = body;
  header(c, area.takeTop(HEADER_H), "Budgeteer", BACK);
  rule(c, 0, area.y, c.width());
  area.takeTop(BORDER + GAP);

  if (state_ != State::Online) {
    drawOffline(c, area);
    return;
  }

  Rect inner = area.inset(GUTTER, 0);
  // Month row: arrows around the month name.
  const int size = MIN_TARGET + SHADOW;
  Rect monthRow = inner.takeTop(size);
  inner.takeTop(GAP);
  glyphButton(c, {monthRow.x, monthRow.y, size, size}, Glyph::Back, PREV_MONTH);
  if (!isCurrentMonth()) glyphButton(c, {monthRow.right() - size, monthRow.y, size, size}, Glyph::Forward, NEXT_MONTH);
  c.textIn({monthRow.x + size, monthRow.y, monthRow.w - 2 * size, size - SHADOW}, font::TITLE, monthTitle_, Tone::Black,
           Align::Center, true);

  segmented(c, inner.takeTop(TAB_H), TABS, 3, static_cast<int>(tab_), TAB);

  const Rect content = contentRect();
  if (!month_.loaded) {
    emptyState(c, content, sync::busy() ? "Loading..." : "No data",
               sync::busy() ? "Downloading this month." : "This month could not be downloaded.");
  } else if (tab_ == Tab::Summary) {
    drawSummary(c, content);
  } else if (tab_ == Tab::Budgets) {
    drawBudgets(c, content);
  } else {
    drawList(c, content);
  }

  Rect action = body;
  action = action.takeBottom(ACTION_H + GUTTER).inset(GUTTER, 0);
  button(c, {action.x, action.y, action.w, ACTION_H}, "+ New transaction", ADD, ButtonStyle::Primary, 0, font::TITLE);
}

void BudgeteerActivity::drawOffline(Canvas& c, Rect area) {
  area = area.inset(GUTTER, 0);
  const Rect action = area.takeBottom(ACTION_H + GUTTER);
  button(c, {action.x, action.y, action.w, ACTION_H}, "+ New transaction", ADD, ButtonStyle::Primary, 0, font::TITLE);

  // Queue counter: the one number that matters offline, inverted.
  const Rect counter = area.takeTop(220);
  c.fill(counter, Tone::Black);
  Rect inner = counter.inset(BORDER + 16);
  c.textIn(inner.takeTop(c.lineHeight(font::SMALL)), font::SMALL, "QUEUED ENTRIES", Tone::White, Align::Left, true);
  char count[12];
  snprintf(count, sizeof(count), "%d", queued_);
  const int th = c.lineHeight(font::JUMBO);
  c.textIn({inner.x, inner.y + (inner.h - th) / 2 - 12, inner.w, th}, font::JUMBO, count, Tone::White, Align::Center,
           true);
  c.textIn(inner.takeBottom(c.lineHeight(font::BODY)), font::BODY, queued_ == 0 ? "Nothing to sync" : "waiting to sync",
           Tone::White, Align::Center, true);
  area.takeTop(GAP * 2);

  const bool checking = state_ == State::Checking;
  c.textIn(area.takeTop(c.lineHeight(font::TITLE)), font::TITLE, checking ? "Connecting..." : "Offline", Tone::Black,
           Align::Left, true);
  area.takeTop(6);
  const char* text = checking ? "Sending queued entries and loading this month."
                              : "New entries are saved on the device and sent on the next sync.";
  area.takeTop(c.textWrapped(area, font::BODY, text, Tone::Dark, 3));
  if (!checking) {
    area.takeTop(GAP * 2);
    button(c, area.takeTop(MIN_TARGET + SHADOW), "Try again", RETRY);
  }
}

void BudgeteerActivity::drawSummary(Canvas& c, Rect area) {
  // Balance, inverted: the one thing that matters on this screen.
  const Rect balance = area.takeTop(132);
  c.fill(balance, Tone::Black);
  Rect inner = balance.inset(BORDER + 14, 12);
  c.textIn(inner.takeTop(c.lineHeight(font::SMALL)), font::SMALL, "BALANCE", Tone::White, Align::Left, true);
  drawMoney(c, inner, balanceText_.c_str(), Tone::White);
  area.takeTop(GAP + 4);

  const int w = (area.w - GAP) / 2;
  const struct {
    const char* label;
    const std::string& value;
  } cards[] = {{"INCOME", incomeText_}, {"EXPENSES", expenseText_}};
  Rect row = area.takeTop(96);
  for (int i = 0; i < 2; ++i) {
    Rect face = block(c, {row.x + i * (w + GAP), row.y, w, row.h}, Tone::Light);
    face = face.inset(12, 10);
    c.textIn(face.takeTop(c.lineHeight(font::SMALL)), font::SMALL, cards[i].label, Tone::Black, Align::Left, true);
    c.textIn(face, font::TITLE, cards[i].value.c_str(), Tone::Black, Align::Left, true);
  }
  area.takeTop(GAP + 4);

  sectionLabel(c, area.takeTop(28), queued_ > 0 ? queuedText_ : "RECENT");
  rule(c, area.x - GUTTER, area.y, c.width());
  area.takeTop(BORDER);
  for (size_t i = 0; i < listRows_.size() && area.h >= LIST_ROW_H; ++i) {
    const Row& r = listRows_[i];
    listRow(c, {0, area.y, c.width(), LIST_ROW_H}, r.title.c_str(), r.detail.c_str(), r.value.c_str(), 0, 0, false);
    area.takeTop(LIST_ROW_H);
  }
}

void BudgeteerActivity::drawBudgets(Canvas& c, Rect area) {
  if (budgetRows_.empty()) {
    emptyState(c, area, "No budgets", "Budgets come from the backend with the month.");
    return;
  }
  bool paged = false;
  const int per = perPage(paged);
  if (paged) drawPager(c, area);
  const int start = page_ * per;
  for (int i = start; i < start + per && i < static_cast<int>(budgetRows_.size()); ++i) {
    const Row& r = budgetRows_[i];
    Rect row = area.takeTop(BUDGET_ROW_H);
    Rect line = row.takeTop(28);
    const int vw = c.textWidth(font::SMALL, r.value.c_str(), true);
    c.textIn(line.takeRight(vw), font::SMALL, r.value.c_str(), Tone::Black, Align::Right, true);
    c.textIn(line, font::BODY, r.title.c_str(), Tone::Black, Align::Left, true);
    const bool over = r.budget > 0 && r.spent > r.budget;
    const int max = r.budget > 0 ? static_cast<int>(r.budget) : 1;
    const int value = r.budget > 0 ? static_cast<int>(std::min(r.spent, r.budget)) : (r.spent > 0 ? 1 : 0);
    progressBar(c, {row.x, row.y + 2, row.w, 18}, value, max, over ? Tone::Black : Tone::Dark);
  }
}

void BudgeteerActivity::drawList(Canvas& c, Rect area) {
  if (listRows_.empty()) {
    emptyState(c, area, "No transactions", "Nothing recorded in this month.");
    return;
  }
  bool paged = false;
  const int per = perPage(paged);
  if (paged) drawPager(c, area);
  rule(c, 0, area.y - BORDER, c.width());
  const int start = page_ * per;
  for (int i = start; i < start + per && i < static_cast<int>(listRows_.size()); ++i) {
    const Row& r = listRows_[i];
    listRow(c, {0, area.y, c.width(), LIST_ROW_H}, r.title.c_str(), r.detail.c_str(), r.value.c_str(), 0, 0, false);
    area.takeTop(LIST_ROW_H);
  }
}

void BudgeteerActivity::drawPager(Canvas& c, const Rect& area) {
  const Rect r{area.x, area.bottom() - (MIN_TARGET + SHADOW), area.w, MIN_TARGET + SHADOW};
  pager(c, r, page_, pageCount(), PREV_PAGE, NEXT_PAGE);
}

void BudgeteerActivity::onAction(const uint16_t action, const int16_t value) {
  switch (action) {
    case BACK:
      onBack();
      return;
    case RETRY:
      connect();
      return;
    case TAB:
      if (value >= 0 && value < 3) {
        tab_ = static_cast<Tab>(value);
        page_ = 0;
      }
      return;
    case PREV_PAGE:
      onUp();
      return;
    case NEXT_PAGE:
      onDown();
      return;
    case PREV_MONTH:
    case NEXT_MONTH: {
      std::tm t{};
      t.tm_year = year_ - 1900;
      t.tm_mon = mon_ + (action == PREV_MONTH ? -1 : 1);
      t.tm_mday = 1;
      t.tm_hour = 12;
      std::mktime(&t);
      year_ = t.tm_year + 1900;
      mon_ = t.tm_mon;
      formatYm(year_, mon_, ym_, sizeof(ym_));
      page_ = 0;
      // Show the last copy at once, then the fresh one when it arrives.
      reload();
      requestMonth(ym_);
      return;
    }
    case ADD: {
      const bool online = state_ == State::Online;
      startActivityForResult(
          std::make_unique<NewTransactionActivity>(renderer, mappedInput, categories_, month_, online),
          [this, online](const ActivityResult& result) {
            if (result.isCancelled) return;
            queued_ = queuedCount();
            buildRows();
            if (online) {
              sync::request(AppId::Budgeteer);
              char current[8];
              std::tm t{};
              if (localNow(t)) {
                formatYm(t.tm_year + 1900, t.tm_mon, current, sizeof(current));
                requestMonth(current);
              }
            }
          });
      return;
    }
    default:
      return;
  }
}

bool BudgeteerActivity::onUp() {
  if (page_ == 0) return false;
  --page_;
  return true;
}

bool BudgeteerActivity::onDown() {
  if (page_ + 1 >= pageCount()) return false;
  ++page_;
  return true;
}

void BudgeteerActivity::onTick() {
  const uint32_t gen = sync::generation();
  if (gen == seenGen_ || sync::busy()) return;
  seenGen_ = gen;
  if (state_ == State::Checking) state_ = sync::lastRunOnline() ? State::Online : State::Offline;
  reload();
  if (page_ >= pageCount()) page_ = 0;
  refresh(true);
}

}  // namespace fos::budget
