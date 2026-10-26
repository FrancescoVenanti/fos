#pragma once

#include <string>
#include <vector>

#include "TodoStore.h"
#include "fos/ui/FosActivity.h"

namespace fos::todo {

// To-do (docs/PRODUCT.md): lists as chips, open tasks sorted by due date,
// tap the box to complete, tap the task to edit it. Long-press a list chip to
// delete that list. Up/Down page through the tasks.
class TodoActivity final : public ui::FosActivity {
 public:
  TodoActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;

 protected:
  void draw(ui::Canvas& c, const ui::Rect& body) override;
  void onAction(uint16_t action, int16_t value) override;
  bool onLongPress(uint16_t action, int16_t value) override;
  bool onUp() override;
  bool onDown() override;
  void onBack() override;

 private:
  enum Action : uint16_t {
    BACK = 1,
    ADD,
    FILTER,  // value = chip index
    CHECK,   // value = row index
    OPEN,    // value = row index
    CLEAR_DONE,
    PREV_PAGE,
    NEXT_PAGE,
    DIALOG_OK,
    DIALOG_CANCEL,
  };

  // Display-ready rows and chips, built when data changes (not while drawing).
  struct Row {
    uint32_t task = 0;  // 0 = the "clear done" row
    std::string title;
    std::string detail;
    bool done = false;
    bool overdue = false;
  };
  struct Chip {
    std::string label;
    uint32_t list = 0;  // 0 = All
    bool add = false;   // "+ List"
    int x = 0;
    int row = 0;
  };

  void rebuild();
  void layoutChips();
  ui::Rect listRect() const;
  int perPage(bool& paged) const;
  int pageCount() const;
  void addTask();
  void addList();
  void drawRow(ui::Canvas& c, const ui::Rect& r, int index) const;

  uint32_t filter_ = 0;  // list id, 0 = all lists
  int page_ = 0;
  std::vector<Row> rows_;
  std::vector<Chip> chips_;
  int chipRows_ = 1;
  uint32_t confirmList_ = 0;  // list waiting for delete confirmation
  std::string confirmText_;
};

}  // namespace fos::todo
