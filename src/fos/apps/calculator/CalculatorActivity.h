#pragma once

#include <string>
#include <vector>

#include "fos/ui/FosActivity.h"
#include "fos/ui/Keyboard.h"

namespace fos::calc {

// Calculator (docs/PRODUCT.md): a standard calculator and a unit converter
// (length, weight, volume, temperature, currency). Currency rates come from
// the sync (backend or Frankfurter) and are cached; the converter says how old
// they are. Up/Down switch between the two tabs.
class CalculatorActivity final : public ui::FosActivity {
 public:
  CalculatorActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;

 protected:
  void draw(ui::Canvas& c, const ui::Rect& body) override;
  void onAction(uint16_t action, int16_t value) override;
  bool onUp() override;
  bool onDown() override;
  void onTick() override;

 private:
  enum Action : uint16_t {
    BACK = 1,
    TAB,
    KEY,       // calculator key, value = index into KEYS
    CATEGORY,  // converter category
    PICK_FROM,
    PICK_TO,
    PICKED,  // value = unit index
    CLOSE_PICKER,
    SWAP,
    NEGATE,
    VALUE_KEY,  // converter keypad
  };

  struct Unit {
    std::string name;
    double factor = 1;  // to the category's base unit (for currency: units per EUR)
  };

  // Calculator
  void press(int key);
  void updateCalc();
  // Converter
  void loadUnits();
  void updateConvert();
  double convert(double value) const;

  void drawCalculator(ui::Canvas& c, ui::Rect area) const;
  void drawConverter(ui::Canvas& c, ui::Rect area);
  void drawPicker(ui::Canvas& c) const;

  int tab_ = 0;

  std::string expr_;  // internal: digits, '.', + - * / ( ) %
  std::string shownExpr_;
  std::string result_;  // live preview or the answer
  bool justEvaluated_ = false;
  bool error_ = false;

  int category_ = 0;
  std::vector<Unit> units_;
  int from_ = 0;
  int to_ = 1;
  int picking_ = 0;  // 0 none, 1 from, 2 to
  bool negative_ = false;
  ui::Keypad keypad_;
  std::string valueText_;
  std::string resultText_;
  std::string ratesNote_;
  uint32_t seenGen_ = 0;
};

}  // namespace fos::calc
