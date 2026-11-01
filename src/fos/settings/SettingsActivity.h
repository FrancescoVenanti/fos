#pragma once

#include <string>
#include <vector>

#include "fos/ui/FosActivity.h"

namespace fos {

// Settings app (docs/PRODUCT.md, "Settings"): apps and layout, display, lock
// screen, Wi-Fi and sync, accounts, buttons and keyboard, power, region and
// system, plus storage and About. Changes save to /fos/settings.json at once.
class SettingsActivity final : public ui::FosActivity {
 public:
  SettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;

 protected:
  void draw(ui::Canvas& c, const ui::Rect& body) override;
  void onAction(uint16_t action, int16_t value) override;
  bool onUp() override;
  bool onDown() override;
  void onBack() override;
  void onTick() override;

 public:
  enum class Page : uint8_t { Main, Apps, Display, Lock, Wifi, Accounts, Buttons, Power, Region, StorageUse, About };

 private:
  enum class Kind : uint8_t { Section, Nav, Toggle, Choice, Info, Button, AppRow, NetworkRow };

  struct Item {
    Kind kind;
    uint8_t id;
    std::string label;
    std::string detail;
    bool on = false;
    int selected = 0;
    const char* const* options = nullptr;
    int optionCount = 0;
    int16_t arg = 0;
    bool primary = false;
  };

  enum Action : uint16_t {
    BACK = 1,
    ITEM,  // value = item index
    PREV_PAGE,
    NEXT_PAGE,
    MOVE_UP,    // value = item index
    MOVE_DOWN,  // value = item index
    REMOVE,     // value = item index
    DIALOG_OK,
    DIALOG_CANCEL,
    CHOICE_BASE = 100,  // + item index, value = option
  };

  void open(Page page);
  void rebuild();
  void paginate();
  int itemHeight(const Item& item) const;
  void activate(int index);
  void choose(int index, int option);
  void editText(uint8_t id, const char* title, const std::string& value, bool secret, const char* hint);
  void save();
  const char* title() const;

  void addSection(const char* label);
  void addNav(uint8_t id, const char* label, const std::string& detail = {});
  void addToggle(uint8_t id, const char* label, bool on, const char* detail = nullptr);
  void addChoice(uint8_t id, const char* label, const char* const* options, int count, int selected);
  void addInfo(uint8_t id, const char* label, const std::string& value);
  void addButton(uint8_t id, const char* label, bool primary = false);

  void drawItem(ui::Canvas& c, const ui::Rect& r, int index);

  Page page_ = Page::Main;
  std::vector<Page> stack_;
  std::vector<Item> items_;
  std::vector<int> pageStarts_;
  int listPage_ = 0;
  uint8_t confirmId_ = 0;  // item id waiting for dialog confirmation, 0 = none
  unsigned long lastBatteryAt_ = 0;
};

}  // namespace fos
