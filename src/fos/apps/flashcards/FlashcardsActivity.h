#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Deck.h"
#include "fos/ui/FosActivity.h"

namespace fos::cards {

// Flashcards (docs/PRODUCT.md): decks with today's review count; spaced
// repetition runs on the device.
class FlashcardsActivity final : public ui::FosActivity {
 public:
  FlashcardsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;

 protected:
  void draw(ui::Canvas& c, const ui::Rect& body) override;
  void onAction(uint16_t action, int16_t value) override;
  void onTick() override;

 private:
  enum Action : uint16_t { BACK = 1, OPEN };

  void reload();

  std::vector<Deck> decks_;
  std::shared_ptr<Scheduler> scheduler_;
  std::vector<std::string> details_;
  std::string summary_;
  int totalDue_ = 0;
  uint32_t seenGen_ = 0;
};

// A review session: the front, then "Show answer", then Again / Hard / Good /
// Easy with the next interval on each button. OK shows the answer; Up/Down
// grade Again/Good when the answer is shown.
class ReviewActivity final : public ui::FosActivity {
 public:
  ReviewActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, Deck deck,
                 std::shared_ptr<Scheduler> scheduler);

  void onEnter() override;

 protected:
  void draw(ui::Canvas& c, const ui::Rect& body) override;
  void onAction(uint16_t action, int16_t value) override;
  bool onUp() override;
  bool onDown() override;
  bool onOk() override;

 private:
  enum Action : uint16_t { BACK = 1, SHOW, GRADE };

  void answer(Grade g);
  void updateTexts();

  Deck deck_;
  std::shared_ptr<Scheduler> scheduler_;
  std::vector<int> queue_;
  size_t done_ = 0;
  int today_ = 0;
  bool showing_ = false;
  std::string progress_;
  std::string intervals_[4];
};

}  // namespace fos::cards
