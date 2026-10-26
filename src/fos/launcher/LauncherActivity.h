#pragma once

#include "fos/apps/AppRegistry.h"
#include "fos/ui/FosActivity.h"

namespace fos {

// Home screen: the enabled apps in Settings order, as a paged grid of
// small (3x5), medium (2x4) or large (1x4) tiles.
class LauncherActivity final : public ui::FosActivity {
 public:
  LauncherActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, int page = 0);

  void onEnter() override;
  bool isHomeActivity() const override { return true; }

 protected:
  void draw(ui::Canvas& c, const ui::Rect& body) override;
  void onAction(uint16_t action, int16_t value) override;
  bool onUp() override;
  bool onDown() override;
  bool onOk() override;
  void onBack() override {}
  void onTick() override;

 private:
  enum Action : uint16_t { OPEN_APP = 1, PREV_PAGE, NEXT_PAGE };

  int perPage() const;
  int pageCount() const;
  void reload();
  void turn(int delta);

  AppId apps_[APP_COUNT];
  int count_ = 0;
  int page_ = 0;
};

}  // namespace fos
