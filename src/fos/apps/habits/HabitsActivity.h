#pragma once

#include <string>
#include <vector>

#include "fos/ui/FosActivity.h"

namespace fos::habits {

// Habits (docs/PRODUCT.md): a daily checklist with a streak grid of the last
// weeks. Data stays on the device in /fos/habits/habits.json: each habit keeps
// the days it was done ("YYYY-MM-DD").
class HabitsActivity final : public ui::FosActivity {
 public:
  HabitsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;

 protected:
  void draw(ui::Canvas& c, const ui::Rect& body) override;
  void onAction(uint16_t action, int16_t value) override;
  bool onLongPress(uint16_t action, int16_t value) override;
  bool onUp() override;
  bool onDown() override;
  void onTick() override;

 private:
  enum Action : uint16_t { BACK = 1, ADD, CHECK, TAB, PREV_PAGE, NEXT_PAGE, DIALOG_OK, DIALOG_CANCEL };

  struct Habit {
    std::string name;
    std::vector<std::string> done;  // sorted dates
  };

  void load();
  void save() const;
  void rebuild();
  bool doneOn(const Habit& h, const std::string& day) const;
  int streak(const Habit& h) const;
  int perPage() const;

  void drawToday(ui::Canvas& c, ui::Rect area);
  void drawGrid(ui::Canvas& c, ui::Rect area) const;

  std::vector<Habit> habits_;
  std::string today_;
  std::vector<std::string> days_;  // grid: oldest first, ends today
  std::vector<std::string> details_;
  std::string summary_;
  int tab_ = 0;
  int page_ = 0;
  int confirmDelete_ = -1;
  std::string confirmText_;
};

}  // namespace fos::habits
