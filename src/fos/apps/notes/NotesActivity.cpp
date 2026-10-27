#include "NotesActivity.h"

#include <GfxRenderer.h>
#include <MappedInputManager.h>

#include <algorithm>
#include <cstdio>

#include "fos/ui/TextEntryActivity.h"

namespace fos::notes {

using namespace ui;
using namespace ui::theme;

namespace {

constexpr int ROW = 80;
constexpr int PEN_W = 3;
constexpr int ERASER_W = 18;
constexpr unsigned long FRAME_MS = 120;  // repaint rate while drawing

// "First words of the note" for the list.
std::string snippet(const std::string& text) {
  const size_t nl = text.find('\n');
  if (nl == std::string::npos) return {};
  std::string rest = text.substr(nl + 1);
  for (char& ch : rest) {
    if (ch == '\n') ch = ' ';
  }
  return rest;
}

}  // namespace

// --- list -----------------------------------------------------------------------

NotesActivity::NotesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : FosActivity("Notes", renderer, mappedInput) {}

void NotesActivity::onEnter() {
  rebuild();
  FosActivity::onEnter();
}

void NotesActivity::rebuild() {
  // Empty typed notes (keyboard cancelled) are dropped.
  auto& list = notes();
  const size_t before = list.size();
  list.erase(
      std::remove_if(list.begin(), list.end(), [](const Note& n) { return n.kind == Kind::Text && n.text.empty(); }),
      list.end());
  if (list.size() != before) save();

  rows_.clear();
  rows_.reserve(list.size());
  for (const Note& n : list) {
    Row r;
    r.id = n.id;
    r.title = n.kind == Kind::Sketch ? "Sketch" : n.title;
    const std::string when = describeTime(n.updated);
    if (n.kind == Kind::Sketch) {
      r.detail = when;
    } else {
      const std::string more = snippet(n.text);
      r.detail = more.empty() ? when : when + "  \xC2\xB7  " + more;
    }
    rows_.push_back(std::move(r));
  }
  const int pages = std::max(1, (static_cast<int>(rows_.size()) + perPage() - 1) / perPage());
  if (page_ >= pages) page_ = pages - 1;
}

int NotesActivity::perPage() const {
  const int h = bodyRect().h - HEADER_H - BORDER - (MIN_TARGET + SHADOW + GUTTER * 2) - (MIN_TARGET + SHADOW + GAP);
  return std::max(1, h / ROW);
}

void NotesActivity::draw(Canvas& c, const Rect& body) {
  Rect area = body;
  header(c, area.takeTop(HEADER_H), "Notes", BACK);
  rule(c, 0, area.y, c.width());
  area.takeTop(BORDER);

  const Rect actions = area.takeBottom(MIN_TARGET + SHADOW + GUTTER * 2).inset(GUTTER, GUTTER);
  const int bw = (actions.w - GAP) / 2;
  button(c, {actions.x, actions.y, bw, actions.h}, "+ Note", NEW_NOTE, ButtonStyle::Primary);
  button(c, {actions.x + bw + GAP, actions.y, bw, actions.h}, "+ Sketch", NEW_SKETCH);

  if (rows_.empty()) {
    emptyState(c, area, "No notes yet", "Type a note or draw a sketch. Notes stay on this device.");
    return;
  }
  const int per = perPage();
  const int pages = (static_cast<int>(rows_.size()) + per - 1) / per;
  if (pages > 1) {
    const Rect p = area.takeBottom(MIN_TARGET + SHADOW + GAP);
    pager(c, {p.x + GUTTER, p.y + GAP / 2, p.w - 2 * GUTTER, MIN_TARGET + SHADOW - GAP / 2}, page_, pages, PREV_PAGE,
          NEXT_PAGE);
  }
  for (int i = page_ * per; i < std::min(static_cast<int>(rows_.size()), (page_ + 1) * per); ++i) {
    listRow(c, area.takeTop(ROW), rows_[i].title.c_str(), rows_[i].detail.c_str(), nullptr, OPEN,
            static_cast<int16_t>(i));
  }
}

void NotesActivity::open(const uint32_t id) {
  const Note* n = find(id);
  if (!n) return;
  std::unique_ptr<Activity> next;
  if (n->kind == Kind::Sketch) {
    next = std::make_unique<SketchActivity>(renderer, mappedInput, id);
  } else {
    next = std::make_unique<NoteViewActivity>(renderer, mappedInput, id, n->text.empty());
  }
  startActivityForResult(std::move(next), [this](const ActivityResult&) {
    page_ = 0;
    rebuild();
  });
}

void NotesActivity::onAction(const uint16_t action, const int16_t value) {
  switch (action) {
    case BACK:
      onBack();
      return;
    case NEW_NOTE:
      open(create(Kind::Text));
      return;
    case NEW_SKETCH: {
      const uint32_t id = create(Kind::Sketch);
      save();
      open(id);
      return;
    }
    case OPEN:
      if (value >= 0 && value < static_cast<int>(rows_.size())) open(rows_[value].id);
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

bool NotesActivity::onUp() {
  if (page_ == 0) return false;
  --page_;
  return true;
}

bool NotesActivity::onDown() {
  if ((page_ + 1) * perPage() >= static_cast<int>(rows_.size())) return false;
  ++page_;
  return true;
}

// --- typed note -----------------------------------------------------------------

NoteViewActivity::NoteViewActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const uint32_t id,
                                   const bool editNow)
    : FosActivity("NoteView", renderer, mappedInput), id_(id), editNow_(editNow) {}

void NoteViewActivity::onEnter() {
  FosActivity::onEnter();
  if (editNow_) {
    editNow_ = false;
    edit();
  }
}

void NoteViewActivity::edit() {
  const Note* n = find(id_);
  if (!n) return;
  auto entry = std::make_unique<TextEntryActivity>(renderer, mappedInput, "Note", n->text, false, MAX_TEXT,
                                                   "The first line is the title. Done saves.");
  entry->multiline();
  startActivityForResult(std::move(entry), [this](const ActivityResult& result) {
    Note* note = find(id_);
    if (!note) return;
    const auto* text = std::get_if<KeyboardResult>(&result.data);
    if (!result.isCancelled && text) {
      note->text = text->text;
      touch(*note);
      save();
      wrapped_ = false;
      top_ = 0;
    }
    if (note->text.empty()) {
      // Nothing was typed: no empty note is left behind.
      remove(id_);
      save();
      finish();
    }
  });
}

void NoteViewActivity::paginate(Canvas& c, const Rect& area) {
  lines_.clear();
  const Note* n = find(id_);
  if (!n) return;
  // Word-wrap each paragraph to the area width.
  size_t start = 0;
  const std::string& text = n->text;
  while (start <= text.size()) {
    size_t nl = text.find('\n', start);
    if (nl == std::string::npos) nl = text.size();
    std::string para = text.substr(start, nl - start);
    if (para.empty()) lines_.emplace_back();
    while (!para.empty()) {
      size_t fit = para.size();
      while (fit > 0 && c.textWidth(font::BODY, para.substr(0, fit).c_str()) > area.w) {
        const size_t space = para.rfind(' ', fit - 1);
        fit = space == std::string::npos || space == 0 ? fit - 1 : space;
      }
      if (fit == 0) fit = 1;
      lines_.push_back(para.substr(0, fit));
      para.erase(0, fit);
      while (!para.empty() && para[0] == ' ') para.erase(0, 1);
    }
    start = nl + 1;
  }
  linesPerPage_ = std::max(1, area.h / (c.lineHeight(font::BODY) + 4));
  wrapped_ = true;
}

void NoteViewActivity::draw(Canvas& c, const Rect& body) {
  const Note* n = find(id_);
  Rect area = body;
  header(c, area.takeTop(HEADER_H), n && !n->title.empty() ? n->title.c_str() : "Note", BACK, "Edit", EDIT);
  rule(c, 0, area.y, c.width());
  area.takeTop(BORDER + GAP);
  const Rect actions = area.takeBottom(MIN_TARGET + SHADOW + GUTTER).inset(GUTTER, 0);
  button(c, {actions.x, actions.y, 160, MIN_TARGET + SHADOW}, "Delete", DELETE_NOTE, ButtonStyle::Quiet);
  area = area.inset(GUTTER, 0);
  area.takeBottom(GAP);
  if (!n) return;
  if (!wrapped_) paginate(c, area);
  const int pages = (static_cast<int>(lines_.size()) + linesPerPage_ - 1) / linesPerPage_;
  if (pages > 1) {
    char p[16];
    snprintf(p, sizeof(p), "%d / %d", top_ / linesPerPage_ + 1, pages);
    c.textIn({actions.x + 170, actions.y, actions.w - 170, MIN_TARGET}, font::BODY, p, Tone::Dark, Align::Right);
  }
  const int lh = c.lineHeight(font::BODY) + 4;
  for (int i = top_; i < std::min(static_cast<int>(lines_.size()), top_ + linesPerPage_); ++i) {
    // The title line is bold.
    c.text(font::BODY, area.x, area.y + (i - top_) * lh, lines_[i].c_str(), Tone::Black, i == 0);
  }
  if (confirmDelete_)
    dialog(c, "Delete note?", "It is removed from this device.", "Delete", DIALOG_OK, "Cancel", DIALOG_CANCEL);
}

void NoteViewActivity::onAction(const uint16_t action, int16_t) {
  if (confirmDelete_) {
    confirmDelete_ = false;
    if (action == DIALOG_OK) {
      remove(id_);
      save();
      finish();
    }
    return;
  }
  switch (action) {
    case BACK:
      onBack();
      return;
    case EDIT:
      edit();
      return;
    case DELETE_NOTE:
      confirmDelete_ = true;
      return;
    default:
      return;
  }
}

bool NoteViewActivity::onUp() {
  if (top_ == 0) return false;
  top_ = std::max(0, top_ - linesPerPage_);
  return true;
}

bool NoteViewActivity::onDown() {
  if (top_ + linesPerPage_ >= static_cast<int>(lines_.size())) return false;
  top_ += linesPerPage_;
  return true;
}

// --- sketch ---------------------------------------------------------------------

SketchActivity::SketchActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const uint32_t id)
    : FosActivity("Sketch", renderer, mappedInput), id_(id) {}

void SketchActivity::onEnter() {
  loadSketch(id_, strokes_);
  FosActivity::onEnter();
}

void SketchActivity::onExit() {
  if (dirty_ && find(id_)) {
    saveSketch(id_, strokes_);
    if (Note* n = find(id_)) touch(*n);
    save();
  }
  FosActivity::onExit();
}

Rect SketchActivity::canvasRect() const {
  const Rect body = bodyRect();
  return {(renderer.getScreenWidth() - CANVAS_W) / 2, body.y + HEADER_H + BORDER + GAP, CANVAS_W, CANVAS_H};
}

int SketchActivity::pointCount() const {
  int n = 0;
  for (const Stroke& s : strokes_) n += static_cast<int>(s.points.size());
  return n;
}

void SketchActivity::onTick() {
  // Strokes are built from the finger position while it is down.
  strokeEnded_ = false;
  int x = 0;
  int y = 0;
  const Rect r = canvasRect();
  if (mappedInput.isScreenTouchHeld(x, y) && r.contains(x, y) && confirm_ == 0) {
    const Point p{static_cast<int16_t>(x - r.x), static_cast<int16_t>(y - r.y)};
    if (!drawing_) {
      if (pointCount() >= static_cast<int>(MAX_POINTS)) return;
      strokes_.push_back({eraser_, {}});
      drawing_ = true;
    }
    auto& pts = strokes_.back().points;
    if (pts.empty() || pts.back().x != p.x || pts.back().y != p.y) {
      pts.push_back(p);
      dirty_ = true;
      if (millis() - lastFrameAt_ >= FRAME_MS) {
        lastFrameAt_ = millis();
        refresh();
      }
    }
    return;
  }
  if (drawing_) {
    drawing_ = false;
    strokeEnded_ = true;
    refresh();
  }
}

void SketchActivity::draw(Canvas& c, const Rect& body) {
  Rect area = body;
  header(c, area.takeTop(HEADER_H), "Sketch", BACK, "Delete", DELETE_SKETCH);
  rule(c, 0, area.y, c.width());

  const Rect r = canvasRect();
  c.fill(r, Tone::White);
  c.frame(r.inset(-BORDER), BORDER, Tone::Black);
  for (const Stroke& s : strokes_) {
    const int w = s.eraser ? ERASER_W : PEN_W;
    const Tone tone = s.eraser ? Tone::White : Tone::Black;
    if (s.points.size() == 1) {
      c.fill({r.x + s.points[0].x - w / 2, r.y + s.points[0].y - w / 2, w, w}, tone);
    }
    for (size_t i = 1; i < s.points.size(); ++i) {
      c.line(r.x + s.points[i - 1].x, r.y + s.points[i - 1].y, r.x + s.points[i].x, r.y + s.points[i].y, w, tone);
    }
  }
  // Pen strokes may not paint outside the frame.
  c.frame(r.inset(-BORDER), BORDER, Tone::Black);
  c.hit(r, CANVAS);

  const int y = r.bottom() + BORDER + GAP;
  const Rect tools{GUTTER, y, c.width() - 2 * GUTTER, body.bottom() - y - GUTTER / 2};
  const int bw = (tools.w - 3 * GAP) / 4;
  button(c, {tools.x, tools.y, bw, tools.h}, "Pen", PEN, eraser_ ? ButtonStyle::Secondary : ButtonStyle::Primary);
  button(c, {tools.x + (bw + GAP), tools.y, bw, tools.h}, "Eraser", ERASER,
         eraser_ ? ButtonStyle::Primary : ButtonStyle::Secondary);
  button(c, {tools.x + 2 * (bw + GAP), tools.y, bw, tools.h}, "Undo", UNDO, ButtonStyle::Quiet);
  button(c, {tools.x + 3 * (bw + GAP), tools.y, bw, tools.h}, "Clear", CLEAR, ButtonStyle::Quiet);

  if (confirm_ == 1)
    dialog(c, "Clear sketch?", "Everything on the canvas is erased.", "Clear", DIALOG_OK, "Cancel", DIALOG_CANCEL);
  if (confirm_ == 2)
    dialog(c, "Delete sketch?", "It is removed from this device.", "Delete", DIALOG_OK, "Cancel", DIALOG_CANCEL);
}

void SketchActivity::onAction(const uint16_t action, int16_t) {
  if (confirm_) {
    const int what = confirm_;
    confirm_ = 0;
    if (action == DIALOG_OK && what == 1) {
      strokes_.clear();
      dirty_ = true;
    } else if (action == DIALOG_OK && what == 2) {
      remove(id_);
      save();
      dirty_ = false;
      finish();
      return;
    }
    refresh(true);
    return;
  }
  switch (action) {
    case BACK:
      onBack();
      return;
    case PEN:
      eraser_ = false;
      return;
    case ERASER:
      eraser_ = true;
      return;
    case UNDO:
      if (!strokes_.empty()) {
        strokes_.pop_back();
        dirty_ = true;
      }
      return;
    case CLEAR:
      if (!strokes_.empty()) confirm_ = 1;
      return;
    case DELETE_SKETCH:
      confirm_ = 2;
      return;
    default:
      return;
  }
}

}  // namespace fos::notes
