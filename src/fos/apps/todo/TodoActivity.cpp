#include "TodoActivity.h"

#include <GfxRenderer.h>

#include <algorithm>

#include "TaskEditActivity.h"
#include "fos/ui/TextEntryActivity.h"

namespace fos::todo {

using namespace ui;
using namespace ui::theme;

namespace {

constexpr int ROW = 72;
constexpr int CHECK_W = 76;  // tap area of the check box at the row's left
constexpr int BOX = 36;
constexpr int PAGER_H = MIN_TARGET + SHADOW + GAP * 2;
constexpr const char* DOT = "  \xC2\xB7  ";

}  // namespace

TodoActivity::TodoActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : FosActivity("Todo", renderer, mappedInput) {}

void TodoActivity::onEnter() {
  rebuild();
  FosActivity::onEnter();
}

void TodoActivity::layoutChips() {
  chips_.clear();
  const Data& d = data();
  chips_.push_back({"All", 0, false});
  for (const List& l : d.lists) chips_.push_back({l.name, l.id, false});
  if (d.lists.size() < MAX_LISTS) chips_.push_back({"+ List", 0, true});

  // Same width as ui::chip(); wraps onto a second row when needed.
  const int width = renderer.getScreenWidth() - 2 * GUTTER;
  int x = 0;
  int row = 0;
  for (Chip& chip : chips_) {
    const int w = renderer.getTextWidth(font::SMALL, chip.label.c_str(), EpdFontFamily::BOLD) + 28;
    if (x > 0 && x + w > width) {
      x = 0;
      ++row;
    }
    chip.x = GUTTER + x;
    chip.row = row;
    x += w + GAP;
  }
  chipRows_ = row + 1;
}

void TodoActivity::rebuild() {
  Data& d = data();
  if (filter_ != 0 && !d.list(filter_)) filter_ = 0;
  layoutChips();

  std::vector<const Task*> open;
  std::vector<const Task*> done;
  for (const Task& t : d.tasks) {
    if (filter_ != 0 && t.list != filter_) continue;
    (t.done ? done : open).push_back(&t);
  }
  // Dated tasks first, soonest first; then undated in the order they were added.
  std::stable_sort(open.begin(), open.end(), [](const Task* a, const Task* b) {
    if (a->due.empty() != b->due.empty()) return !a->due.empty();
    return a->due < b->due;
  });
  std::stable_sort(done.begin(), done.end(), [](const Task* a, const Task* b) { return a->doneOn > b->doneOn; });

  const std::string now = today();
  rows_.clear();
  rows_.reserve(open.size() + done.size() + 1);
  const auto add = [&](const Task& t) {
    Row row;
    row.task = t.id;
    row.title = t.title;
    row.done = t.done;
    row.overdue = !t.done && !t.due.empty() && !now.empty() && t.due < now;
    std::string detail;
    if (t.done) {
      detail = "Done";
      if (!t.doneOn.empty()) detail += " " + describeDate(t.doneOn);
    } else if (!t.due.empty()) {
      detail = describeDate(t.due);
    }
    if (t.repeat != Repeat::None) {
      if (!detail.empty()) detail += DOT;
      detail += repeatName(t.repeat);
    }
    if (filter_ == 0) {
      const List* l = d.list(t.list);
      if (l) detail = detail.empty() ? l->name : l->name + DOT + detail;
    }
    row.detail = std::move(detail);
    rows_.push_back(std::move(row));
  };
  for (const Task* t : open) add(*t);
  for (const Task* t : done) add(*t);
  if (!done.empty()) {
    Row clear;
    clear.title = "Clear " + std::to_string(done.size()) + " done";
    rows_.push_back(std::move(clear));
  }
  if (page_ >= pageCount()) page_ = std::max(0, pageCount() - 1);
}

Rect TodoActivity::listRect() const {
  Rect area = bodyRect();
  area.takeTop(HEADER_H + BORDER + GAP);
  area.takeTop(chipRows_ * (CHIP_H + GAP) + GAP);
  return area;
}

int TodoActivity::perPage(bool& paged) const {
  const int h = listRect().h;
  int per = std::max(1, h / ROW);
  paged = static_cast<int>(rows_.size()) > per;
  if (paged) per = std::max(1, (h - PAGER_H) / ROW);
  return per;
}

int TodoActivity::pageCount() const {
  bool paged = false;
  const int per = perPage(paged);
  return std::max(1, (static_cast<int>(rows_.size()) + per - 1) / per);
}

void TodoActivity::draw(Canvas& c, const Rect& body) {
  Rect area = body;
  header(c, area.takeTop(HEADER_H), "To-do", BACK, nullptr, ADD, Glyph::Plus);
  rule(c, 0, area.y, c.width());
  area.takeTop(BORDER + GAP);

  for (size_t i = 0; i < chips_.size(); ++i) {
    const Chip& chip = chips_[i];
    const bool selected = !chip.add && chip.list == filter_;
    ui::chip(c, chip.x, area.y + chip.row * (CHIP_H + GAP), chip.label.c_str(), selected, FILTER,
             static_cast<int16_t>(i));
  }

  Rect list = listRect();
  rule(c, 0, list.y - BORDER, c.width());
  if (rows_.empty()) {
    emptyState(c, list, "Nothing to do", "Tap + to add a task.");
  } else {
    bool paged = false;
    const int per = perPage(paged);
    if (paged) {
      const Rect footer = list.takeBottom(PAGER_H);
      pager(c, {footer.x + GUTTER, footer.y + GAP, footer.w - 2 * GUTTER, MIN_TARGET + SHADOW}, page_, pageCount(),
            PREV_PAGE, NEXT_PAGE);
    }
    const int start = page_ * per;
    for (int i = start; i < start + per && i < static_cast<int>(rows_.size()); ++i) {
      drawRow(c, list.takeTop(ROW), i);
    }
  }

  if (confirmList_ != 0) {
    dialog(c, "Delete list?", confirmText_.c_str(), "Delete", DIALOG_OK, "Cancel", DIALOG_CANCEL);
  }
}

void TodoActivity::drawRow(Canvas& c, const Rect& r, const int index) const {
  const Row& row = rows_[index];
  if (row.task == 0) {
    // "Clear N done": a quiet action row.
    const bool down = c.pressed(CLEAR_DONE);
    c.fill(r, down ? Tone::Black : Tone::Light);
    rule(c, r.x, r.bottom() - BORDER, r.w);
    c.textIn({r.x + GUTTER, r.y, r.w - 2 * GUTTER, r.h - BORDER}, font::BODY, row.title.c_str(),
             down ? Tone::White : Tone::Black, Align::Center, true);
    c.hit(r, CLEAR_DONE);
    return;
  }

  const bool down = c.pressed(OPEN, static_cast<int16_t>(index));
  c.fill(r, down ? Tone::Black : Tone::White);
  rule(c, r.x, r.bottom() - BORDER, r.w);
  const Tone ink = down ? Tone::White : Tone::Black;

  // Check box: outlined square, filled black with a white tick when done.
  const Rect box{r.x + GUTTER, r.y + (r.h - BORDER - BOX) / 2, BOX, BOX};
  const bool boxDown = c.pressed(CHECK, static_cast<int16_t>(index));
  if (row.done || boxDown) {
    c.fill(box, down ? Tone::White : Tone::Black);
    drawGlyph(c, box.inset(6), Glyph::Check, down ? Tone::Black : Tone::White);
  } else {
    c.fill(box, Tone::White);
    c.frame(box, BORDER, Tone::Black);
  }
  c.hit({r.x, r.y, CHECK_W, r.h}, CHECK, static_cast<int16_t>(index));

  Rect content{r.x + CHECK_W, r.y, r.w - CHECK_W - GUTTER, r.h - BORDER};
  c.hit({content.x, r.y, r.w - CHECK_W, r.h}, OPEN, static_cast<int16_t>(index));
  const Tone titleInk = row.done && !down ? Tone::Dark : ink;
  if (row.overdue) {
    // Overdue: a black tag on the right, the one thing to notice.
    const char* tag = "LATE";
    const int w = c.textWidth(font::SMALL, tag, true) + 16;
    const Rect t = content.takeRight(w);
    c.fill({t.x, t.y + (t.h - 24) / 2, t.w, 24}, down ? Tone::White : Tone::Black);
    c.textIn({t.x, t.y + (t.h - 24) / 2, t.w, 24}, font::SMALL, tag, down ? Tone::Black : Tone::White, Align::Center,
             true);
    content.takeRight(GAP);
  }
  if (!row.detail.empty()) {
    const int th = c.lineHeight(font::BODY);
    const int dh = c.lineHeight(font::SMALL);
    const int top = content.y + (content.h - th - dh) / 2;
    c.textIn({content.x, top, content.w, th}, font::BODY, row.title.c_str(), titleInk, Align::Left, true);
    c.textIn({content.x, top + th, content.w, dh}, font::SMALL, row.detail.c_str(), down ? Tone::White : Tone::Dark);
  } else {
    c.textIn(content, font::BODY, row.title.c_str(), titleInk, Align::Left, true);
  }
}

void TodoActivity::addTask() {
  startActivityForResult(std::make_unique<TextEntryActivity>(renderer, mappedInput, "New task", "", false, MAX_TITLE),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) return;
                           const auto* text = std::get_if<KeyboardResult>(&result.data);
                           if (!text || text->text.empty()) return;
                           Data& d = data();
                           d.addTask(filter_ != 0 ? filter_ : d.lists.front().id, text->text);
                           save();
                           rebuild();
                         });
}

void TodoActivity::addList() {
  startActivityForResult(
      std::make_unique<TextEntryActivity>(renderer, mappedInput, "New list", "", false, MAX_LIST_NAME,
                                          "Up to 6 lists. Long-press a list to delete it."),
      [this](const ActivityResult& result) {
        if (result.isCancelled) return;
        const auto* text = std::get_if<KeyboardResult>(&result.data);
        if (!text || !data().addList(text->text)) return;
        filter_ = data().lists.back().id;
        page_ = 0;
        save();
        rebuild();
      });
}

void TodoActivity::onAction(const uint16_t action, const int16_t value) {
  if (confirmList_ != 0) {
    if (action == DIALOG_OK) {
      data().removeList(confirmList_);
      save();
      if (filter_ == confirmList_) filter_ = 0;
    }
    if (action == DIALOG_OK || action == DIALOG_CANCEL) {
      confirmList_ = 0;
      rebuild();
    }
    return;
  }
  switch (action) {
    case BACK:
      onBack();
      return;
    case ADD:
      addTask();
      return;
    case FILTER:
      if (value < 0 || value >= static_cast<int>(chips_.size())) return;
      if (chips_[value].add) {
        addList();
        return;
      }
      filter_ = chips_[value].list;
      page_ = 0;
      rebuild();
      return;
    case CHECK:
      if (value >= 0 && value < static_cast<int>(rows_.size()) && rows_[value].task != 0) {
        data().toggleDone(rows_[value].task);
        save();
        rebuild();
      }
      return;
    case OPEN:
      if (value >= 0 && value < static_cast<int>(rows_.size()) && rows_[value].task != 0) {
        startActivityForResult(std::make_unique<TaskEditActivity>(renderer, mappedInput, rows_[value].task),
                               [this](const ActivityResult&) { rebuild(); });
      }
      return;
    case CLEAR_DONE:
      data().clearDone(filter_);
      save();
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

bool TodoActivity::onLongPress(const uint16_t action, const int16_t value) {
  if (action != FILTER || value < 0 || value >= static_cast<int>(chips_.size())) return false;
  const Chip& chip = chips_[value];
  if (chip.add || chip.list == 0 || data().lists.size() <= 1) return false;
  int count = 0;
  for (const Task& t : data().tasks) count += t.list == chip.list ? 1 : 0;
  confirmList_ = chip.list;
  confirmText_ = "\"" + chip.label + "\" and its " + std::to_string(count) + (count == 1 ? " task" : " tasks") +
                 " will be removed from this device.";
  return true;
}

bool TodoActivity::onUp() {
  if (page_ == 0) return false;
  --page_;
  return true;
}

bool TodoActivity::onDown() {
  if (page_ + 1 >= pageCount()) return false;
  ++page_;
  return true;
}

void TodoActivity::onBack() {
  if (confirmList_ != 0) {
    confirmList_ = 0;
    requestUpdate();
    return;
  }
  finish();
}

}  // namespace fos::todo
