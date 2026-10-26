#pragma once

#include <string>

#include "TodoStore.h"
#include "fos/ui/FosActivity.h"

namespace fos::todo {

// Edits one task: title, list, due date and repeat. Save writes it; Back
// leaves it unchanged. Delete asks first.
class TaskEditActivity final : public ui::FosActivity {
 public:
  TaskEditActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, uint32_t taskId);

 protected:
  void draw(ui::Canvas& c, const ui::Rect& body) override;
  void onAction(uint16_t action, int16_t value) override;
  void onBack() override;

 private:
  enum Action : uint16_t {
    BACK = 1,
    DELETE_TASK,
    TITLE,
    LIST,  // value = list index
    DAY_PREV,
    DAY_NEXT,
    DUE,     // value 0 = none, 1 = today, 2 = tomorrow
    REPEAT,  // value = Repeat
    SAVE,
    DIALOG_OK,
    DIALOG_CANCEL,
  };

  void updateTexts();

  uint32_t taskId_;
  Task task_;  // working copy
  bool confirmDelete_ = false;
  std::string dueText_;
  std::string today_;
};

}  // namespace fos::todo
