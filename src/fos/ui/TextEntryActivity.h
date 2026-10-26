#pragma once

#include "FosActivity.h"
#include "Keyboard.h"

namespace fos::ui {

// Full-screen text entry with the on-screen keyboard. Finishes with a
// KeyboardResult, or cancelled on Back.
class TextEntryActivity final : public FosActivity {
 public:
  TextEntryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string title, std::string text,
                    bool secret = false, size_t maxLength = 120, std::string hint = {});

 protected:
  void draw(Canvas& c, const Rect& body) override;
  void onAction(uint16_t action, int16_t value) override;
  void onBack() override;

 private:
  enum Action : uint16_t { BACK = 1, DONE, CLEAR, KEY };

  void done();

  std::string title_;
  std::string hint_;
  bool secret_;
  Keyboard keyboard_;
};

}  // namespace fos::ui
