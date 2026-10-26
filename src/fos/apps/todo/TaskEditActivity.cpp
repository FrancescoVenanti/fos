#include "TaskEditActivity.h"

#include "fos/ui/TextEntryActivity.h"

namespace fos::todo {

using namespace ui;
using namespace ui::theme;

namespace {

constexpr const char* REPEATS[] = {"Never", "Daily", "Weekly", "Monthly"};
constexpr int LABEL_H = 30;

}  // namespace

TaskEditActivity::TaskEditActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const uint32_t taskId)
    : FosActivity("TaskEdit", renderer, mappedInput), taskId_(taskId) {
  if (const Task* t = data().task(taskId)) task_ = *t;
  today_ = today();
  updateTexts();
}

void TaskEditActivity::updateTexts() { dueText_ = task_.due.empty() ? "No date" : describeDate(task_.due); }

void TaskEditActivity::draw(Canvas& c, const Rect& body) {
  Rect area = body;
  header(c, area.takeTop(HEADER_H), "Edit task", BACK, "Delete", DELETE_TASK);
  rule(c, 0, area.y, c.width());
  area.takeTop(BORDER + GAP);
  area = area.inset(GUTTER, 0);

  // Title: a raised field; tap to retype it on the keyboard.
  sectionLabel(c, area.takeTop(LABEL_H).inset(-GUTTER, 0), "TITLE");
  const Rect titleFace = block(c, area.takeTop(92), Tone::White, TITLE);
  c.textWrapped(titleFace.inset(12, 10), font::BODY, task_.title.c_str(), Tone::Black, 2, true);
  area.takeTop(GAP);

  // List chips, wrapping.
  sectionLabel(c, area.takeTop(LABEL_H).inset(-GUTTER, 0), "LIST");
  const Data& d = data();
  int x = area.x;
  int y = area.y;
  for (size_t i = 0; i < d.lists.size(); ++i) {
    const char* name = d.lists[i].name.c_str();
    const int w = chipWidth(c, name);
    if (x > area.x && x + w > area.right()) {
      x = area.x;
      y += CHIP_H + GAP;
    }
    chip(c, x, y, name, d.lists[i].id == task_.list, LIST, static_cast<int16_t>(i));
    x += w + GAP;
  }
  area.takeTop(y + CHIP_H + GAP - area.y);

  // Due date: arrows around the date, then quick chips.
  sectionLabel(c, area.takeTop(LABEL_H).inset(-GUTTER, 0), "DUE");
  const int size = MIN_TARGET + SHADOW;
  const Rect dateRow = area.takeTop(size);
  glyphButton(c, {dateRow.x, dateRow.y, size, size}, Glyph::Back, DAY_PREV);
  glyphButton(c, {dateRow.right() - size, dateRow.y, size, size}, Glyph::Forward, DAY_NEXT);
  c.textIn({dateRow.x + size, dateRow.y, dateRow.w - 2 * size, size - SHADOW}, font::TITLE, dueText_.c_str(),
           Tone::Black, Align::Center, true);
  area.takeTop(GAP);
  const char* dueChips[] = {"None", "Today", "Tomorrow"};
  const std::string tomorrow = today_.empty() ? std::string() : addDays(today_, 1);
  const bool selected[] = {task_.due.empty(), !today_.empty() && task_.due == today_,
                           !tomorrow.empty() && task_.due == tomorrow};
  x = area.x;
  for (int i = 0; i < 3; ++i) x += chip(c, x, area.y, dueChips[i], selected[i], DUE, static_cast<int16_t>(i)) + GAP;
  area.takeTop(CHIP_H + GAP);

  sectionLabel(c, area.takeTop(LABEL_H).inset(-GUTTER, 0), "REPEAT");
  segmented(c, area.takeTop(TAB_H), REPEATS, 4, static_cast<int>(task_.repeat), REPEAT);

  const Rect bottom = body.inset(GUTTER, 0);
  button(c, {bottom.x, bottom.bottom() - GUTTER - ACTION_H, bottom.w, ACTION_H}, "Save", SAVE, ButtonStyle::Primary, 0,
         font::TITLE);

  if (confirmDelete_) {
    dialog(c, "Delete task?", task_.title.c_str(), "Delete", DIALOG_OK, "Cancel", DIALOG_CANCEL);
  }
}

void TaskEditActivity::onAction(const uint16_t action, const int16_t value) {
  if (confirmDelete_) {
    if (action == DIALOG_OK) {
      data().removeTask(taskId_);
      save();
      finish();
    }
    if (action == DIALOG_CANCEL) confirmDelete_ = false;
    return;
  }
  switch (action) {
    case BACK:
      onBack();
      return;
    case DELETE_TASK:
      confirmDelete_ = true;
      return;
    case TITLE:
      startActivityForResult(
          std::make_unique<TextEntryActivity>(renderer, mappedInput, "Task", task_.title, false, MAX_TITLE),
          [this](const ActivityResult& result) {
            if (result.isCancelled) return;
            const auto* text = std::get_if<KeyboardResult>(&result.data);
            if (text && !text->text.empty()) task_.title = text->text;
          });
      return;
    case LIST:
      if (value >= 0 && value < static_cast<int>(data().lists.size())) task_.list = data().lists[value].id;
      return;
    case DAY_PREV:
    case DAY_NEXT:
      if (task_.due.empty()) {
        task_.due = today_;
      } else {
        task_.due = addDays(task_.due, action == DAY_PREV ? -1 : 1);
      }
      updateTexts();
      return;
    case DUE:
      task_.due = value == 0 ? std::string() : value == 1 ? today_ : addDays(today_, 1);
      updateTexts();
      return;
    case REPEAT:
      if (value >= 0 && value <= static_cast<int>(Repeat::Monthly)) task_.repeat = static_cast<Repeat>(value);
      return;
    case SAVE:
      if (Task* t = data().task(taskId_)) {
        *t = task_;
        save();
      }
      finish();
      return;
    default:
      return;
  }
}

void TaskEditActivity::onBack() {
  if (confirmDelete_) {
    confirmDelete_ = false;
    requestUpdate();
    return;
  }
  finish();
}

}  // namespace fos::todo
