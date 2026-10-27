#pragma once

#include <string>
#include <vector>

#include "fos/ui/FosActivity.h"

namespace fos::readlater {

struct Article {
  std::string id;
  std::string title;
  std::string source;
  std::string text;
  int words = 0;
  bool read = false;
  bool local = false;  // a .txt file on the card
};

// Read-later (docs/PRODUCT.md): articles arrive with the sync as clean text,
// or as .txt files copied to /fos/readlater/ (first line = title). Marking an
// article read is queued and sent back on the next sync.
class ReadLaterActivity final : public ui::FosActivity {
 public:
  ReadLaterActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;

 protected:
  void draw(ui::Canvas& c, const ui::Rect& body) override;
  void onAction(uint16_t action, int16_t value) override;
  bool onUp() override;
  bool onDown() override;
  void onTick() override;

 private:
  enum Action : uint16_t { BACK = 1, OPEN, TAB, PREV_PAGE, NEXT_PAGE };

  void reload();
  int perPage() const;

  std::vector<Article> articles_;
  std::vector<int> shown_;  // indexes for the current tab
  std::vector<std::string> details_;
  int tab_ = 0;  // 0 unread, 1 read
  int page_ = 0;
  uint32_t seenGen_ = 0;
};

// Reading view: the article paginated in the reading font, Up/Down or a tap on
// the right/left half turn pages. The last page offers "Mark as read".
class ArticleActivity final : public ui::FosActivity {
 public:
  ArticleActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, Article article);

 protected:
  void draw(ui::Canvas& c, const ui::Rect& body) override;
  void onAction(uint16_t action, int16_t value) override;
  bool onUp() override;
  bool onDown() override;
  bool pressable(uint16_t action) const override { return action != PAGE_TAP; }

 private:
  enum Action : uint16_t { BACK = 1, PAGE_TAP, MARK_READ };

  void layout(ui::Canvas& c, const ui::Rect& area);

  Article article_;
  std::vector<std::string> lines_;  // wrapped; "" = paragraph gap
  std::vector<int> pageStarts_;
  int page_ = 0;
  bool laidOut_ = false;
  std::string pageText_;
};

// Read status waiting to be sent ("/fos/readlater/queue.json"); registered as
// the app's sync pusher.
void init();
void markRead(const Article& article);
bool isMarkedRead(const std::string& id);

}  // namespace fos::readlater
