#pragma once

#include <ctime>
#include <string>
#include <vector>

#include "BudgetStore.h"
#include "fos/ui/FosActivity.h"
#include "fos/ui/Keyboard.h"

namespace fos::budget {

// Big money figure: digits in the jumbo font with a smaller euro sign on the
// same baseline, right-aligned in r. Falls back to the large font when wide.
void drawMoney(ui::Canvas& c, const ui::Rect& r, const char* digits, ui::Tone tone);

// Six-step entry: type, amount, category, description, date, save.
// Back steps back; on the first step it cancels. Saving queues the entry and
// finishes with a plain (not cancelled) result.
class NewTransactionActivity final : public ui::FosActivity {
 public:
  NewTransactionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::vector<Category> categories,
                         Month month, bool online);

 protected:
  void draw(ui::Canvas& c, const ui::Rect& body) override;
  void onAction(uint16_t action, int16_t value) override;
  void onBack() override;

 private:
  enum class Step : uint8_t { Type, Amount, Category, Description, Date, Review };
  static constexpr int STEPS = 6;

  enum Action : uint16_t {
    BACK = 1,
    TYPE,  // value 0 = expense, 1 = income
    KEY,   // keypad value
    NEXT,
    CATEGORY,  // value = index in shown_
    CHIP,      // value = index in recent_
    TYPE_TEXT,
    SKIP,
    DAY_PREV,
    DAY_NEXT,
    TODAY,
    YESTERDAY,
    SAVE,
  };

  void go(Step step);
  void setDay(int offsetFromToday);
  void updateTexts();
  const char* categoryName(const std::string& id) const;

  void drawType(ui::Canvas& c, ui::Rect area);
  void drawAmount(ui::Canvas& c, ui::Rect area);
  void drawCategory(ui::Canvas& c, ui::Rect area);
  void drawDescription(ui::Canvas& c, ui::Rect area);
  void drawDate(ui::Canvas& c, ui::Rect area);
  void drawReview(ui::Canvas& c, ui::Rect area);

  std::vector<Category> categories_;
  std::vector<int> shown_;  // categories_ indexes for the chosen type
  Month month_;
  bool online_;

  Step step_ = Step::Type;
  Entry entry_;
  ui::Keypad keypad_;
  std::vector<std::string> recent_;
  int dayOffset_ = 0;  // 0 = today, -1 = yesterday

  // Texts drawn every frame, rebuilt when the entry changes.
  std::string amountText_;
  char stepText_[40] = "";
  char dateText_[40] = "";
};

}  // namespace fos::budget
