#pragma once

#include <string>
#include <vector>

#include "NotesStore.h"
#include "fos/ui/FosActivity.h"

namespace fos::notes {

// Notes and sketch (docs/PRODUCT.md): a list of typed notes and sketches,
// newest first. "+ Note" opens the keyboard, "+ Sketch" a drawing canvas.
class NotesActivity final : public ui::FosActivity {
 public:
  NotesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;

 protected:
  void draw(ui::Canvas& c, const ui::Rect& body) override;
  void onAction(uint16_t action, int16_t value) override;
  bool onUp() override;
  bool onDown() override;

 private:
  enum Action : uint16_t { BACK = 1, NEW_NOTE, NEW_SKETCH, OPEN, PREV_PAGE, NEXT_PAGE };

  void rebuild();
  void open(uint32_t id);
  int perPage() const;

  struct Row {
    uint32_t id;
    std::string title;
    std::string detail;
  };
  std::vector<Row> rows_;
  int page_ = 0;
};

// One typed note: the text, wrapped, paged with Up/Down; Edit opens the
// keyboard, Delete asks first.
class NoteViewActivity final : public ui::FosActivity {
 public:
  NoteViewActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, uint32_t id, bool editNow);

  void onEnter() override;

 protected:
  void draw(ui::Canvas& c, const ui::Rect& body) override;
  void onAction(uint16_t action, int16_t value) override;
  bool onUp() override;
  bool onDown() override;

 private:
  enum Action : uint16_t { BACK = 1, EDIT, DELETE_NOTE, DIALOG_OK, DIALOG_CANCEL };

  void edit();
  void paginate(ui::Canvas& c, const ui::Rect& area);

  uint32_t id_;
  bool editNow_;
  bool confirmDelete_ = false;
  std::vector<std::string> lines_;  // wrapped lines, built when the text changes
  bool wrapped_ = false;
  int top_ = 0;  // first line shown
  int linesPerPage_ = 1;
};

// Freehand sketch: draw with a finger on the canvas (fast partial refresh),
// pen or eraser, undo, clear. Saved when leaving.
class SketchActivity final : public ui::FosActivity {
 public:
  SketchActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, uint32_t id);

  void onEnter() override;
  void onExit() override;

 protected:
  void draw(ui::Canvas& c, const ui::Rect& body) override;
  void onAction(uint16_t action, int16_t value) override;
  void onTick() override;
  bool pressable(uint16_t action) const override { return action != CANVAS; }
  bool capturesSwipes() const override { return drawing_ || strokeEnded_; }

 private:
  enum Action : uint16_t { BACK = 1, CANVAS, PEN, ERASER, UNDO, CLEAR, DELETE_SKETCH, DIALOG_OK, DIALOG_CANCEL };

  ui::Rect canvasRect() const;
  int pointCount() const;

  uint32_t id_;
  std::vector<Stroke> strokes_;
  bool eraser_ = false;
  bool drawing_ = false;
  bool strokeEnded_ = false;  // the frame a stroke ends in: its swipe is not a gesture
  bool dirty_ = false;
  unsigned long lastFrameAt_ = 0;
  int confirm_ = 0;  // 1 clear, 2 delete
};

}  // namespace fos::notes
