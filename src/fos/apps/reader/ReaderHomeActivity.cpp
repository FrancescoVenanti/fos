#include "ReaderHomeActivity.h"

#include <HalStorage.h>

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
  FosActivity::onEnter();
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

  // The last book: the one thing that matters here, inverted.
  Rect inner = area.inset(GUTTER, 0);
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

  if (books_.size() > 1) {
    sectionLabel(c, inner.takeTop(28).inset(-GUTTER, 0), "RECENT");
    rule(c, 0, inner.y, c.width());
    inner.takeTop(BORDER);
    for (size_t i = 1; i < books_.size() && inner.h >= ROW; ++i) {
      const Rect r = inner.takeTop(ROW);
      listRow(c, {0, r.y, c.width(), r.h}, books_[i].title.c_str(), books_[i].detail.c_str(), nullptr, OPEN,
              static_cast<int16_t>(i));
    }
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
    case FILES:
      activityManager.goToFileBrowser(Storage.exists("/books") ? "/books" : "/");
      return;
    default:
      return;
  }
}

}  // namespace fos::reader
