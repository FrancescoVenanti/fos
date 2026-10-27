#include "ReadLaterActivity.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "fos/core/Json.h"
#include "fos/sync/Backend.h"
#include "fos/sync/Sync.h"

namespace fos::readlater {

using namespace ui;
using namespace ui::theme;

namespace {

constexpr char FOLDER[] = "/fos/readlater";
constexpr char QUEUE_FILE[] = "/fos/readlater/queue.json";  // ids to send as read
constexpr char READ_FILE[] = "/fos/readlater/read.json";    // ids read on this device
constexpr const char* TABS[] = {"Unread", "Read"};
constexpr int ROW = 84;
constexpr size_t MAX_LOCAL_BYTES = 64 * 1024;

std::mutex queueMutex;

std::vector<std::string> readIds(const char* path) {
  std::vector<std::string> out;
  JsonDocument doc;
  if (loadJson(path, doc)) {
    for (JsonVariantConst v : doc["ids"].as<JsonArrayConst>()) out.emplace_back(v | "");
  }
  return out;
}

void writeIds(const char* path, const std::vector<std::string>& ids) {
  JsonDocument doc;
  JsonArray arr = doc["ids"].to<JsonArray>();
  for (const std::string& id : ids) arr.add(id);
  saveJson(path, doc);
}

// Sends queued read marks; each leaves the queue once the backend confirms.
bool push(const bool backend) {
  if (!backend) return false;
  std::vector<std::string> pending;
  {
    std::lock_guard<std::mutex> lock(queueMutex);
    pending = readIds(QUEUE_FILE);
  }
  if (pending.empty()) return true;
  std::vector<std::string> sent;
  for (const std::string& id : pending) {
    const net::Response r = net::backendPost("/api/v1/readlater/articles/" + id + "/read", "{}");
    if (!r.ok()) break;
    sent.push_back(id);
  }
  std::lock_guard<std::mutex> lock(queueMutex);
  std::vector<std::string> left = readIds(QUEUE_FILE);
  left.erase(
      std::remove_if(left.begin(), left.end(),
                     [&](const std::string& id) { return std::find(sent.begin(), sent.end(), id) != sent.end(); }),
      left.end());
  writeIds(QUEUE_FILE, left);
  return left.empty();
}

int countWords(const std::string& text) {
  int n = 0;
  bool inWord = false;
  for (const char ch : text) {
    const bool space = ch == ' ' || ch == '\n' || ch == '\t';
    if (!space && !inWord) ++n;
    inWord = !space;
  }
  return n;
}

void loadLocal(std::vector<Article>& out) {
  HalFile dir = Storage.open(FOLDER);
  if (!dir || !dir.isDirectory()) return;
  while (HalFile f = dir.openNextFile()) {
    char name[128];
    f.getName(name, sizeof(name));
    const size_t len = strlen(name);
    if (!f.isDirectory() && len > 4 && (strcmp(name + len - 4, ".txt") == 0 || strcmp(name + len - 4, ".TXT") == 0)) {
      std::string text;
      char buf[512];
      int n = 0;
      while ((n = f.read(buf, sizeof(buf))) > 0 && text.size() < MAX_LOCAL_BYTES) text.append(buf, n);
      text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
      Article a;
      a.id = std::string("file-") + name;
      a.local = true;
      const size_t nl = text.find('\n');
      a.title = text.substr(0, std::min(nl, static_cast<size_t>(120)));
      a.text = nl == std::string::npos ? std::string() : text.substr(nl + 1);
      a.source = "On the card";
      a.words = countWords(a.text);
      if (!a.title.empty()) out.push_back(std::move(a));
    }
    f.close();
  }
  dir.close();
}

}  // namespace

void init() { sync::setPusher(AppId::ReadLater, push); }

void markRead(const Article& article) {
  std::lock_guard<std::mutex> lock(queueMutex);
  std::vector<std::string> read = readIds(READ_FILE);
  if (std::find(read.begin(), read.end(), article.id) == read.end()) {
    read.push_back(article.id);
    writeIds(READ_FILE, read);
  }
  if (!article.local) {
    std::vector<std::string> queue = readIds(QUEUE_FILE);
    if (std::find(queue.begin(), queue.end(), article.id) == queue.end()) {
      queue.push_back(article.id);
      writeIds(QUEUE_FILE, queue);
    }
  }
}

bool isMarkedRead(const std::string& id) {
  std::lock_guard<std::mutex> lock(queueMutex);
  const std::vector<std::string> read = readIds(READ_FILE);
  return std::find(read.begin(), read.end(), id) != read.end();
}

// --- list -----------------------------------------------------------------------

ReadLaterActivity::ReadLaterActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : FosActivity("ReadLater", renderer, mappedInput) {}

void ReadLaterActivity::onEnter() {
  reload();
  seenGen_ = sync::generation();
  sync::request(AppId::ReadLater);  // sends read marks, downloads new articles
  FosActivity::onEnter();
}

void ReadLaterActivity::reload() {
  articles_.clear();
  JsonDocument doc;
  if (loadJson(sync::cachePath(AppId::ReadLater).c_str(), doc)) {
    for (JsonObjectConst a : doc["data"]["articles"].as<JsonArrayConst>()) {
      Article art;
      art.id = a["id"] | "";
      art.title = a["title"] | "";
      art.source = a["source"] | "";
      art.text = a["text"] | "";
      art.words = a["words"] | countWords(art.text);
      art.read = a["read"] | false;
      if (!art.id.empty()) articles_.push_back(std::move(art));
    }
  }
  loadLocal(articles_);
  const std::vector<std::string> read = readIds(READ_FILE);
  for (Article& a : articles_) {
    if (std::find(read.begin(), read.end(), a.id) != read.end()) a.read = true;
  }
  shown_.clear();
  details_.clear();
  for (int i = 0; i < static_cast<int>(articles_.size()); ++i) {
    if (articles_[i].read != (tab_ == 1)) continue;
    shown_.push_back(i);
    const int minutes = std::max(1, (articles_[i].words + 199) / 200);
    details_.push_back(articles_[i].source + "  \xC2\xB7  " + std::to_string(minutes) + " min");
  }
  const int pages = std::max(1, (static_cast<int>(shown_.size()) + perPage() - 1) / perPage());
  if (page_ >= pages) page_ = pages - 1;
}

int ReadLaterActivity::perPage() const {
  const int h = bodyRect().h - HEADER_H - BORDER - GAP - TAB_H - GAP - BORDER - (MIN_TARGET + SHADOW + GAP);
  return std::max(1, h / ROW);
}

void ReadLaterActivity::draw(Canvas& c, const Rect& body) {
  Rect area = body;
  header(c, area.takeTop(HEADER_H), "Read-later", BACK);
  rule(c, 0, area.y, c.width());
  area.takeTop(BORDER + GAP);
  segmented(c, area.takeTop(TAB_H).inset(GUTTER, 0), TABS, 2, tab_, TAB);
  area.takeTop(GAP);
  rule(c, 0, area.y, c.width());
  area.takeTop(BORDER);
  if (shown_.empty()) {
    emptyState(c, area, tab_ == 0 ? "All caught up" : "Nothing read yet",
               tab_ == 0 ? "Articles you save arrive with the sync. Text files in the readlater folder of the microSD "
                           "show up here too."
                         : "Finished articles move here.");
    return;
  }
  const int per = perPage();
  const int pages = (static_cast<int>(shown_.size()) + per - 1) / per;
  if (pages > 1) {
    const Rect p = area.takeBottom(MIN_TARGET + SHADOW + GAP);
    pager(c, {p.x + GUTTER, p.y + GAP / 2, p.w - 2 * GUTTER, MIN_TARGET + SHADOW - GAP / 2}, page_, pages, PREV_PAGE,
          NEXT_PAGE);
  }
  for (int i = page_ * per; i < std::min(static_cast<int>(shown_.size()), (page_ + 1) * per); ++i) {
    const Article& a = articles_[shown_[i]];
    listRow(c, area.takeTop(ROW), a.title.c_str(), details_[i].c_str(), nullptr, OPEN, static_cast<int16_t>(i));
  }
}

void ReadLaterActivity::onAction(const uint16_t action, const int16_t value) {
  switch (action) {
    case BACK:
      onBack();
      return;
    case TAB:
      tab_ = value;
      page_ = 0;
      reload();
      return;
    case OPEN:
      if (value >= 0 && value < static_cast<int>(shown_.size())) {
        startActivityForResult(std::make_unique<ArticleActivity>(renderer, mappedInput, articles_[shown_[value]]),
                               [this](const ActivityResult&) {
                                 reload();
                                 sync::request(AppId::ReadLater);
                               });
      }
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

bool ReadLaterActivity::onUp() {
  if (page_ == 0) return false;
  --page_;
  return true;
}

bool ReadLaterActivity::onDown() {
  if ((page_ + 1) * perPage() >= static_cast<int>(shown_.size())) return false;
  ++page_;
  return true;
}

void ReadLaterActivity::onTick() {
  const uint32_t gen = sync::generation();
  if (gen == seenGen_ || sync::busy()) return;
  seenGen_ = gen;
  reload();
  refresh();
}

// --- article --------------------------------------------------------------------

ArticleActivity::ArticleActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, Article article)
    : FosActivity("Article", renderer, mappedInput), article_(std::move(article)) {}

void ArticleActivity::layout(Canvas& c, const Rect& area) {
  lines_.clear();
  // Title first, then the paragraphs, word-wrapped to the page width.
  const auto wrap = [&](const std::string& para) {
    std::string rest = para;
    while (!rest.empty()) {
      size_t fit = rest.size();
      while (fit > 0 && c.textWidth(font::BODY, rest.substr(0, fit).c_str()) > area.w) {
        const size_t space = rest.rfind(' ', fit - 1);
        fit = space == std::string::npos || space == 0 ? fit - 1 : space;
      }
      if (fit == 0) fit = 1;
      lines_.push_back(rest.substr(0, fit));
      rest.erase(0, fit);
      while (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
    }
  };
  size_t start = 0;
  const std::string& text = article_.text;
  while (start < text.size()) {
    size_t nl = text.find('\n', start);
    if (nl == std::string::npos) nl = text.size();
    const std::string para = text.substr(start, nl - start);
    if (!para.empty()) {
      wrap(para);
      lines_.emplace_back();  // paragraph gap
    }
    start = nl + 1;
  }
  // Pages: the first holds the title block.
  const int lh = c.lineHeight(font::BODY) + 6;
  const int titleH = c.lineHeight(font::TITLE) * 2 + c.lineHeight(font::SMALL) + GAP * 2;
  pageStarts_.clear();
  int i = 0;
  bool first = true;
  while (i < static_cast<int>(lines_.size()) || pageStarts_.empty()) {
    pageStarts_.push_back(i);
    const int rows = std::max(1, (area.h - (first ? titleH : 0)) / lh);
    i += rows;
    // A page does not start with a paragraph gap.
    while (i < static_cast<int>(lines_.size()) && lines_[i].empty()) ++i;
    first = false;
  }
  laidOut_ = true;
}

void ArticleActivity::draw(Canvas& c, const Rect& body) {
  Rect area = body;
  header(c, area.takeTop(HEADER_H), article_.source.empty() ? "Article" : article_.source.c_str(), BACK);
  rule(c, 0, area.y, c.width());
  area.takeTop(BORDER + GAP);
  const Rect footer = area.takeBottom(MIN_TARGET + SHADOW + GUTTER).inset(GUTTER, 0);
  area = area.inset(GUTTER, 0);
  area.takeBottom(GAP);
  if (!laidOut_) layout(c, area);

  const int pages = static_cast<int>(pageStarts_.size());
  if (page_ == 0) {
    area.takeTop(c.textWrapped({area.x, area.y, area.w, c.lineHeight(font::TITLE) * 2}, font::TITLE,
                               article_.title.c_str(), Tone::Black, 2, true));
    char meta[48];
    snprintf(meta, sizeof(meta), "%d words  \xC2\xB7  about %d min", article_.words,
             std::max(1, (article_.words + 199) / 200));
    c.textIn(area.takeTop(c.lineHeight(font::SMALL) + GAP), font::SMALL, meta, Tone::Dark);
    rule(c, area.x, area.y, area.w, 2);
    area.takeTop(GAP);
  }
  const int lh = c.lineHeight(font::BODY) + 6;
  const int start = pageStarts_[page_];
  const int end = page_ + 1 < pages ? pageStarts_[page_ + 1] : static_cast<int>(lines_.size());
  int y = area.y;
  for (int i = start; i < end && y + lh <= area.bottom() + 6; ++i) {
    c.text(font::BODY, area.x, y, lines_[i].c_str(), Tone::Black);
    y += lines_[i].empty() ? lh / 2 : lh;
  }
  // Tap left/right half to turn pages.
  c.hit({0, body.y + HEADER_H + BORDER, c.width() / 2, footer.y - body.y - HEADER_H - BORDER}, PAGE_TAP, -1);
  c.hit({c.width() / 2, body.y + HEADER_H + BORDER, c.width() / 2, footer.y - body.y - HEADER_H - BORDER}, PAGE_TAP, 1);

  char pos[24];
  snprintf(pos, sizeof(pos), "%d / %d", page_ + 1, pages);
  if (page_ + 1 == pages && !article_.read) {
    const int bw = footer.w * 3 / 5;
    button(c, {footer.right() - bw, footer.y, bw, MIN_TARGET + SHADOW}, "Mark as read", MARK_READ,
           ButtonStyle::Primary);
    c.textIn({footer.x, footer.y, footer.w - bw - GAP, MIN_TARGET}, font::BODY, pos, Tone::Dark);
  } else {
    c.textIn({footer.x, footer.y, footer.w, MIN_TARGET}, font::BODY, pos, Tone::Dark, Align::Center);
  }
}

void ArticleActivity::onAction(const uint16_t action, const int16_t value) {
  switch (action) {
    case BACK:
      onBack();
      return;
    case PAGE_TAP:
      if (value < 0 ? onUp() : onDown()) requestUpdate();
      return;
    case MARK_READ:
      markRead(article_);
      article_.read = true;
      finish();
      return;
    default:
      return;
  }
}

bool ArticleActivity::onUp() {
  if (page_ == 0) return false;
  --page_;
  return true;
}

bool ArticleActivity::onDown() {
  if (page_ + 1 >= static_cast<int>(pageStarts_.size())) return false;
  ++page_;
  return true;
}

}  // namespace fos::readlater
