#include "CalendarActivity.h"

#include <cctype>
#include <cstdio>
#include <ctime>

#include "fos/core/Status.h"
#include "fos/sync/Sync.h"

namespace fos::calendar {

using namespace ui;
using namespace ui::theme;

namespace {

constexpr int STATUS_H = 34;
constexpr int PAGER_H = MIN_TARGET + SHADOW + GAP * 2;
constexpr int TIME_W = 96;

// "09:30" in the user's clock format.
std::string clockText(const std::string& hhmm) {
  int h = 0;
  int m = 0;
  if (sscanf(hhmm.c_str(), "%d:%d", &h, &m) != 2) return hhmm;
  std::tm t{};
  t.tm_hour = h % 24;
  t.tm_min = m;
  char out[12];
  formatClock(t, out, sizeof(out));
  return out;
}

}  // namespace

CalendarActivity::CalendarActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : FosActivity("Calendar", renderer, mappedInput) {}

void CalendarActivity::onEnter() {
  seenGen_ = sync::generation();
  reload();
  sync::request(AppId::Calendar);
  rebuild();
  FosActivity::onEnter();
}

void CalendarActivity::reload() { loadAgenda(agenda_); }

int CalendarActivity::itemHeight(const Kind kind) {
  switch (kind) {
    case Kind::Day:
      return 44;
    case Kind::Empty:
      return 48;
    case Kind::Event:
      break;
  }
  return 76;
}

Rect CalendarActivity::listRect() const {
  Rect area = bodyRect();
  area.takeTop(HEADER_H + BORDER);
  area.takeTop(STATUS_H);
  return area;
}

void CalendarActivity::rebuild() {
  stamp_ = nowStamp();

  // Status line: how fresh the copy is.
  char when[40] = "";
  if (agenda_.fetched != 0) {
    const std::time_t t = agenda_.fetched;
    std::tm tm{};
    localtime_r(&t, &tm);
    char date[16];
    char clock[12];
    formatDate(tm, date, sizeof(date));
    formatClock(tm, clock, sizeof(clock));
    snprintf(when, sizeof(when), "%s %s", date, clock);
  }
  if (sync::busy()) {
    status_ = "Updating...";
  } else if (agenda_.fetched == 0) {
    status_ = checked_ && !online_ ? "Offline" : "";
  } else if (checked_ && !online_) {
    status_ = std::string("Offline. Last copy ") + when;
  } else {
    status_ = std::string("Updated ") + when;
  }

  items_.clear();
  if (agenda_.loaded && stamp_.size() >= 16) {
    const std::string time = stamp_.substr(11, 5);
    const Event* next = nextEvent(agenda_, stamp_);
    std::tm day{};
    localNow(day);
    day.tm_hour = 12;
    for (int d = 0; d < DAYS; ++d) {
      std::tm t = day;
      t.tm_mday += d;
      std::mktime(&t);
      char iso[16];
      snprintf(iso, sizeof(iso), "%04d-%02d-%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
      char label[24];
      snprintf(label, sizeof(label), "%.3s %d %.3s", weekdayName(t.tm_wday), t.tm_mday, monthName(t.tm_mon));

      Item header;
      header.kind = Kind::Day;
      // "TODAY  Mon 26 Oct", then "WEDNESDAY  28 Oct".
      if (d < 2) {
        header.time = d == 0 ? "TODAY" : "TOMORROW";
        header.title = label;
      } else {
        header.time = weekdayName(t.tm_wday);
        for (char& ch : header.time) ch = static_cast<char>(toupper(static_cast<unsigned char>(ch)));
        header.title = label + 4;
      }
      items_.push_back(std::move(header));

      bool any = false;
      for (const Event& e : agenda_.events) {
        if (e.date != iso) continue;
        any = true;
        Item item;
        item.kind = Kind::Event;
        item.title = e.title;
        item.detail = e.location;
        if (e.allDay) {
          item.time = "All day";
        } else {
          item.time = clockText(e.start);
          if (!e.end.empty()) item.time2 = clockText(e.end) + (e.overnight ? " +1" : "");
        }
        if (d == 0 && !e.allDay) {
          const std::string end = endOnDay(e);
          item.past = end <= time;
          item.now = e.start <= time && end > time;
        }
        item.next = &e == next;
        items_.push_back(std::move(item));
      }
      if (!any) {
        Item empty;
        empty.kind = Kind::Empty;
        empty.title = "Nothing planned";
        items_.push_back(std::move(empty));
      }
    }
  }

  // Pages: fill each with whole items; a day header never ends a page.
  pageStarts_.clear();
  const int full = listRect().h;
  int total = 0;
  for (const Item& item : items_) total += itemHeight(item.kind);
  const int avail = total > full ? full - PAGER_H : full;
  int used = 0;
  for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
    int h = itemHeight(items_[i].kind);
    if (items_[i].kind == Kind::Day && i + 1 < static_cast<int>(items_.size())) h += itemHeight(items_[i + 1].kind);
    if (pageStarts_.empty() || used + h > avail) {
      pageStarts_.push_back(i);
      used = 0;
    }
    used += itemHeight(items_[i].kind);
  }
  if (pageStarts_.empty()) pageStarts_.push_back(0);
  if (page_ >= static_cast<int>(pageStarts_.size())) page_ = 0;
}

void CalendarActivity::draw(Canvas& c, const Rect& body) {
  Rect area = body;
  header(c, area.takeTop(HEADER_H), "Calendar", BACK, "Sync", REFRESH);
  rule(c, 0, area.y, c.width());
  area.takeTop(BORDER);
  c.textIn(area.takeTop(STATUS_H).inset(GUTTER, 0), font::SMALL, status_.c_str(), Tone::Dark, Align::Left, true);

  if (items_.empty()) {
    emptyState(c, area, sync::busy() ? "Loading..." : "No agenda yet",
               sync::busy() ? "Downloading the next 7 days." : "Connect to Wi-Fi once to download your calendar.");
    return;
  }

  const int pages = static_cast<int>(pageStarts_.size());
  if (pages > 1) {
    const Rect footer = area.takeBottom(PAGER_H);
    pager(c, {footer.x + GUTTER, footer.y + GAP, footer.w - 2 * GUTTER, MIN_TARGET + SHADOW}, page_, pages, PREV_PAGE,
          NEXT_PAGE);
  }
  const int start = pageStarts_[page_];
  const int end = page_ + 1 < pages ? pageStarts_[page_ + 1] : static_cast<int>(items_.size());
  for (int i = start; i < end; ++i) drawItem(c, area.takeTop(itemHeight(items_[i].kind)), items_[i]);
}

void CalendarActivity::drawItem(Canvas& c, const Rect& r, const Item& item) const {
  if (item.kind == Kind::Day) {
    c.fill(r, Tone::Light);
    rule(c, r.x, r.y, r.w);
    rule(c, r.x, r.bottom() - BORDER, r.w);
    const Rect inner{r.x + GUTTER, r.y + BORDER, r.w - 2 * GUTTER, r.h - 2 * BORDER};
    c.textIn(inner, font::SMALL, item.time.c_str(), Tone::Black, Align::Left, true);
    c.textIn(inner, font::SMALL, item.title.c_str(), Tone::Black, Align::Right, true);
    return;
  }
  if (item.kind == Kind::Empty) {
    rule(c, r.x, r.bottom() - BORDER, r.w);
    c.textIn({r.x + GUTTER + TIME_W, r.y, r.w - 2 * GUTTER - TIME_W, r.h - BORDER}, font::BODY, item.title.c_str(),
             Tone::Dark);
    return;
  }

  // Event: time column, then title and place. The next event is inverted.
  const bool inverted = item.next;
  c.fill(r, inverted ? Tone::Black : Tone::White);
  rule(c, r.x, r.bottom() - BORDER, r.w);
  const Tone ink = inverted ? Tone::White : item.past ? Tone::Dark : Tone::Black;
  const Tone soft = inverted ? Tone::White : Tone::Dark;
  Rect content{r.x + GUTTER, r.y, r.w - 2 * GUTTER, r.h - BORDER};
  const Rect timeCol = content.takeLeft(TIME_W);

  const int th = c.lineHeight(font::BODY);
  const int dh = c.lineHeight(font::SMALL);
  const int top = content.y + (content.h - th - dh) / 2;
  if (item.time2.empty()) {
    c.textIn(timeCol, font::BODY, item.time.c_str(), ink, Align::Left, true);
  } else {
    c.textIn({timeCol.x, top, timeCol.w, th}, font::BODY, item.time.c_str(), ink, Align::Left, true);
    c.textIn({timeCol.x, top + th, timeCol.w, dh}, font::SMALL, item.time2.c_str(), soft, Align::Left, false);
  }

  if (item.next || item.now) {
    const char* tag = item.now ? "NOW" : "NEXT";
    const int w = c.textWidth(font::SMALL, tag, true) + 16;
    const Rect t = content.takeRight(w);
    const Rect box{t.x, t.y + (t.h - 24) / 2, t.w, 24};
    c.fill(box, inverted ? Tone::White : Tone::Black);
    c.textIn(box, font::SMALL, tag, inverted ? Tone::Black : Tone::White, Align::Center, true);
    content.takeRight(GAP);
  }
  if (item.detail.empty()) {
    c.textIn(content, font::BODY, item.title.c_str(), ink, Align::Left, true);
  } else {
    c.textIn({content.x, top, content.w, th}, font::BODY, item.title.c_str(), ink, Align::Left, true);
    c.textIn({content.x, top + th, content.w, dh}, font::SMALL, item.detail.c_str(), soft);
  }
}

void CalendarActivity::onAction(const uint16_t action, int16_t) {
  switch (action) {
    case BACK:
      onBack();
      return;
    case REFRESH:
      sync::request(AppId::Calendar);
      rebuild();
      return;
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

bool CalendarActivity::onUp() {
  if (page_ == 0) return false;
  --page_;
  return true;
}

bool CalendarActivity::onDown() {
  if (page_ + 1 >= static_cast<int>(pageStarts_.size())) return false;
  ++page_;
  return true;
}

void CalendarActivity::onTick() {
  const uint32_t gen = sync::generation();
  if (gen != seenGen_ && !sync::busy()) {
    seenGen_ = gen;
    checked_ = true;
    online_ = sync::lastRunOnline();
    reload();
    rebuild();
    refresh(true);
    return;
  }
  // The status line follows the worker; past, now and next follow the clock.
  const bool busy = sync::busy();
  const std::string stamp = nowStamp();
  if (busy != busy_ || stamp != stamp_) {
    busy_ = busy;
    rebuild();
    refresh();
  }
}

}  // namespace fos::calendar
