#include "ReaderHomeActivity.h"

#include <FsHelpers.h>
#include <HalStorage.h>

#include <algorithm>

#include "RecentBooksStore.h"
#include "activities/ActivityManager.h"

namespace fos::reader {

using namespace ui;
using namespace ui::theme;

namespace {

constexpr int CONTINUE_H = 176;
constexpr int ROW = 72;

// "/books/Moby Dick.epub" -> "Moby Dick".
std::string fileTitle(const std::string& path) {
  const size_t slash = path.rfind('/');
  std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
  const size_t dot = name.rfind('.');
  if (dot != std::string::npos && dot > 0) name.resize(dot);
  return name;
}

}  // namespace

ReaderHomeActivity::ReaderHomeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : FosActivity("ReaderHome", renderer, mappedInput) {}

void ReaderHomeActivity::onEnter() {
  books_.clear();
  // Books deleted from the card since they were opened drop out.
  if (RECENT_BOOKS.pruneMissing()) RECENT_BOOKS.saveToFile();
  for (const RecentBook& b : RECENT_BOOKS.getBooks()) {
    Book book;
    book.path = b.path;
    book.title = b.title.empty() ? fileTitle(b.path) : b.title;
    book.detail = b.author;
    books_.push_back(std::move(book));
  }
  recentCount_ = books_.size();
  // Books on the card not opened yet: everything in /books the reader can open.
  std::vector<Book> fresh;
  HalFile dir = Storage.open("/books");
  if (dir && dir.isDirectory()) {
    while (HalFile f = dir.openNextFile()) {
      char name[256];
      f.getName(name, sizeof(name));
      const bool book = !f.isDirectory() && name[0] != '.' &&
                        (FsHelpers::hasReflowableBookExtension(name) || FsHelpers::hasXtcExtension(name));
      f.close();
      if (!book) continue;
      const std::string path = std::string("/books/") + name;
      const bool known =
          std::any_of(books_.begin(), books_.begin() + recentCount_, [&path](const Book& b) { return b.path == path; });
      if (!known) fresh.push_back({path, fileTitle(path), "Not opened yet"});
    }
  }
  if (dir) dir.close();
  std::sort(fresh.begin(), fresh.end(), [](const Book& a, const Book& b) { return a.title < b.title; });
  for (Book& b : fresh) books_.push_back(std::move(b));
  page_ = 0;
  FosActivity::onEnter();
}

// Rows that fit under the Continue card (first page) or on a full page.
int ReaderHomeActivity::listCapacity(const bool firstPage) const {
  int h = bodyRect().h - HEADER_H - BORDER - GAP - (MIN_TARGET + SHADOW + GUTTER) - GAP - 28 - BORDER -
          (MIN_TARGET + SHADOW + GAP);
  if (firstPage && recentCount_ > 0) h -= CONTINUE_H + GAP * 2;
  return std::max(1, h / ROW);
}

bool ReaderHomeActivity::onUp() {
  if (page_ == 0) return false;
  --page_;
  return true;
}

bool ReaderHomeActivity::onDown() {
  const int listed = static_cast<int>(books_.size()) - (recentCount_ > 0 ? 1 : 0);
  if (listCapacity(true) + page_ * listCapacity(false) >= listed) return false;
  ++page_;
  return true;
}

void ReaderHomeActivity::draw(Canvas& c, const Rect& body) {
  Rect area = body;
  header(c, area.takeTop(HEADER_H), "Reader", BACK);
  rule(c, 0, area.y, c.width());
  area.takeTop(BORDER + GAP);

  // Library and Files at the bottom.
  Rect bottom = area.takeBottom(MIN_TARGET + SHADOW + GUTTER).inset(GUTTER, 0);
  const int bw = (bottom.w - GAP) / 2;
  button(c, {bottom.x, bottom.y, bw, MIN_TARGET + SHADOW}, "Library", LIBRARY);
  button(c, {bottom.x + bw + GAP, bottom.y, bw, MIN_TARGET + SHADOW}, "Files", FILES);
  area.takeBottom(GAP);

  if (books_.empty()) {
    emptyState(c, area, "No books yet",
               "Copy EPUB files to the books folder on the microSD card, or send them over Wi-Fi from the CrossPoint "
               "file transfer page.");
    return;
  }

  Rect inner = area.inset(GUTTER, 0);
  // The list below the card: other recent books, then the books not opened yet.
  const size_t firstListed = recentCount_ > 0 ? 1 : 0;
  const int listed = static_cast<int>(books_.size() - firstListed);
  const int cap1 = listCapacity(true);
  const int cap = listCapacity(false);
  const int pages = listed <= cap1 ? 1 : 1 + (listed - cap1 + cap - 1) / cap;
  if (pages > 1) {
    const Rect p = inner.takeBottom(MIN_TARGET + SHADOW + GAP);
    pager(c, {p.x, p.y + GAP, p.w, MIN_TARGET + SHADOW - GAP}, page_, pages, PREV_PAGE, NEXT_PAGE);
  }

  if (page_ == 0 && recentCount_ > 0) {
    // The last book: the one thing that matters here, inverted.
    const Rect card = inner.takeTop(CONTINUE_H);
    const bool down = c.pressed(OPEN, 0);
    c.fill(card, down ? Tone::White : Tone::Black);
    c.frame(card, BORDER, Tone::Black);
    const Tone ink = down ? Tone::Black : Tone::White;
    Rect face = card.inset(18, 16);
    c.textIn(face.takeTop(c.lineHeight(font::SMALL)), font::SMALL, "CONTINUE READING", ink, Align::Left, true);
    face.takeTop(8);
    const Rect footer = face.takeBottom(c.lineHeight(font::BODY));
    face.takeTop(c.textWrapped(face, font::TITLE, books_[0].title.c_str(), ink, 2, true));
    if (!books_[0].detail.empty()) c.textIn(footer, font::BODY, books_[0].detail.c_str(), ink, Align::Left);
    c.hit(card, OPEN, 0);
    inner.takeTop(GAP * 2);
  }

  const int start = static_cast<int>(firstListed) + (page_ == 0 ? 0 : cap1 + (page_ - 1) * cap);
  const int end = std::min(static_cast<int>(books_.size()), start + (page_ == 0 ? cap1 : cap));
  int shownSection = -1;  // 0 recent, 1 on the card
  for (int i = start; i < end; ++i) {
    const int section = i < static_cast<int>(recentCount_) ? 0 : 1;
    if (section != shownSection) {
      shownSection = section;
      sectionLabel(c, inner.takeTop(28).inset(-GUTTER, 0), section == 0 ? "RECENT" : "ON THE CARD");
      rule(c, 0, inner.y, c.width());
      inner.takeTop(BORDER);
    }
    if (inner.h < ROW) break;
    const Rect r = inner.takeTop(ROW);
    listRow(c, {0, r.y, c.width(), r.h}, books_[i].title.c_str(), books_[i].detail.c_str(), nullptr, OPEN,
            static_cast<int16_t>(i));
  }
}

void ReaderHomeActivity::onAction(const uint16_t action, const int16_t value) {
  switch (action) {
    case BACK:
      onBack();
      return;
    case OPEN:
      if (value >= 0 && value < static_cast<int>(books_.size())) {
        // Replaces the whole stack: leaving the book goes to the launcher.
        activityManager.goToReader(books_[value].path);
      }
      return;
    case LIBRARY:
      activityManager.goToLibrary();
      return;
    case PREV_PAGE:
      if (onUp()) requestUpdate();
      return;
    case NEXT_PAGE:
      if (onDown()) requestUpdate();
      return;
    case FILES:
      activityManager.goToFileBrowser(Storage.exists("/books") ? "/books" : "/");
      return;
    default:
      return;
  }
}

}  // namespace fos::reader
