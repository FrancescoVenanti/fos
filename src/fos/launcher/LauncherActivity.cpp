#include "LauncherActivity.h"

#include <cstdio>

#include "fos/apps/AppLauncher.h"
#include "fos/settings/FosSettings.h"
#include "fos/ui/generated/AppIconData.h"

namespace fos {

using namespace ui;
using namespace ui::theme;

namespace {

struct GridSpec {
  int cols;
  int rows;
  int iconSize;
  const uint8_t* const* icons;
};

GridSpec gridFor(const IconSize size) {
  switch (size) {
    case IconSize::Small:
      return {3, 5, 36, icons::APP_36};
    case IconSize::Large:
      return {1, 4, 64, icons::APP_64};
    case IconSize::Medium:
    default:
      return {2, 4, 48, icons::APP_48};
  }
}

}  // namespace

LauncherActivity::LauncherActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const int page)
    : FosActivity("Launcher", renderer, mappedInput), page_(page) {}

void LauncherActivity::onEnter() {
  reload();
  FosActivity::onEnter();
}

void LauncherActivity::reload() {
  count_ = settings().visibleApps(apps_, APP_COUNT);
  if (page_ >= pageCount()) page_ = pageCount() - 1;
  if (page_ < 0) page_ = 0;
}

int LauncherActivity::perPage() const {
  const GridSpec g = gridFor(settings().iconSize);
  return g.cols * g.rows;
}

int LauncherActivity::pageCount() const { return count_ == 0 ? 1 : (count_ + perPage() - 1) / perPage(); }

void LauncherActivity::turn(const int delta) {
  const int pages = pageCount();
  page_ = (page_ + delta + pages) % pages;
  refresh();
}

bool LauncherActivity::onUp() {
  turn(-1);
  return true;
}

bool LauncherActivity::onDown() {
  turn(1);
  return true;
}

bool LauncherActivity::onOk() {
  // Nothing to go back to from Home: OK flips through the pages.
  turn(1);
  return true;
}

void LauncherActivity::onTick() {
  // Settings may have changed while an app was on top; the list is tiny.
  const int before = count_;
  reload();
  if (count_ != before) refresh();
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Left) turn(1);
  if (swipe == MappedInputManager::SwipeDir::Right && !mappedInput.wasBackGesture()) turn(-1);
}

void LauncherActivity::onAction(const uint16_t action, const int16_t value) {
  switch (action) {
    case OPEN_APP:
      openApp(static_cast<AppId>(value));
      break;
    case PREV_PAGE:
      turn(-1);
      break;
    case NEXT_PAGE:
      turn(1);
      break;
    default:
      break;
  }
}

void LauncherActivity::draw(Canvas& c, const Rect& body) {
  Rect area = body.inset(GUTTER, 0);

  // Date line.
  Rect top = area.takeTop(64);
  std::tm now{};
  char date[40] = "FOs";
  if (localNow(now)) snprintf(date, sizeof(date), "%s %d %s", weekdayName(now.tm_wday), now.tm_mday, monthName(now.tm_mon));
  c.textIn(top, font::TITLE, date, Tone::Black, Align::Left, true);

  const int pages = pageCount();
  if (pages > 1) {
    const Rect bottom = area.takeBottom(MIN_TARGET + SHADOW + GUTTER);
    pager(c, {bottom.x, bottom.y, bottom.w, MIN_TARGET + SHADOW}, page_, pages, PREV_PAGE, NEXT_PAGE);
  } else {
    area.takeBottom(GUTTER);
  }

  const GridSpec g = gridFor(settings().iconSize);
  const bool labels = settings().showLabels;
  const int tileW = (area.w - (g.cols - 1) * GAP) / g.cols;
  const int tileH = (area.h - (g.rows - 1) * GAP) / g.rows;
  const int first = page_ * perPage();
  for (int i = 0; i < perPage() && first + i < count_; ++i) {
    const AppId id = apps_[first + i];
    const int col = i % g.cols;
    const int row = i / g.cols;
    const Rect tile{area.x + col * (tileW + GAP), area.y + row * (tileH + GAP), tileW, tileH};
    const Rect face = block(c, tile, Tone::White, OPEN_APP, static_cast<int16_t>(id));
    const uint8_t* icon = g.icons[static_cast<int>(id)];
    const char* name = appInfo(id).name;

    if (g.cols == 1) {
      // Large: icon left, name and group right.
      if (!labels) {
        c.bits(icon, face.x + (face.w - g.iconSize) / 2, face.y + (face.h - g.iconSize) / 2, g.iconSize, g.iconSize,
               Tone::Black);
        continue;
      }
      Rect content = face.inset(GUTTER, 0);
      const Rect iconBox = content.takeLeft(g.iconSize);
      c.bits(icon, iconBox.x, iconBox.y + (iconBox.h - g.iconSize) / 2, g.iconSize, g.iconSize, Tone::Black);
      content.takeLeft(GUTTER);
      const int th = c.lineHeight(font::TITLE);
      const int sh = c.lineHeight(font::SMALL);
      const int y = content.y + (content.h - th - sh) / 2;
      c.textIn({content.x, y, content.w, th}, font::TITLE, name, Tone::Black, Align::Left, true);
      c.textIn({content.x, y + th, content.w, sh}, font::SMALL, groupName(appInfo(id).group), Tone::Dark);
      continue;
    }

    const int labelFont = g.cols == 3 ? font::SMALL : font::BODY;
    const int labelH = labels ? c.lineHeight(labelFont) + 6 : 0;
    const int iconY = face.y + (face.h - g.iconSize - labelH) / 2;
    c.bits(icon, face.x + (face.w - g.iconSize) / 2, iconY, g.iconSize, g.iconSize, Tone::Black);
    if (labels) {
      c.textIn({face.x + 4, iconY + g.iconSize + 6, face.w - 8, labelH - 6}, labelFont, name, Tone::Black,
               Align::Center, true);
    }
  }
}

}  // namespace fos
