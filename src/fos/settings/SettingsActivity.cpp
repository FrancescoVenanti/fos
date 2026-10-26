#include "SettingsActivity.h"

#include <HalPowerManager.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cstdio>

#include "WifiCredentialStore.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/settings/OtaUpdateActivity.h"
#include "fos/core/Storage.h"
#include "fos/core/Version.h"
#include "fos/settings/FosSettings.h"
#include "fos/sync/Sync.h"
#include "fos/ui/TextEntryActivity.h"

namespace fos {

using namespace ui;
using namespace ui::theme;

namespace {

// Item ids. Values only need to be unique within the app.
enum Id : uint8_t {
  NONE = 0,
  NAV_APPS,
  NAV_DISPLAY,
  NAV_LOCK,
  NAV_WIFI,
  NAV_ACCOUNTS,
  NAV_BUTTONS,
  NAV_POWER,
  NAV_REGION,
  NAV_STORAGE,
  NAV_ABOUT,
  ICON_SIZE,
  LABELS,
  RESET_LAYOUT,
  APP_ROW,
  FONT_SIZE,
  REFRESH,
  INVERT,
  LOCK_REFRESH,
  CLOCK_STYLE,
  LOCK_WEATHER,
  LOCK_CLIMATE,
  LOCK_EVENT,
  NETWORK_ROW,
  ADD_NETWORK,
  AUTO_SYNC,
  SYNC_NOW,
  LAST_SYNC,
  LICHESS,
  LICHESS_URL,
  BACKEND_URL,
  BACKEND_KEY,
  SWAP_BUTTONS,
  OK_ACTION,
  KEYBOARD,
  ACCENTS,
  SLEEP,
  BATTERY,
  SAVER,
  CLOCK24,
  DATE_FORMAT,
  DECIMAL_COMMA,
  METRIC,
  CLEAR_CACHES,
  FIRMWARE,
  CHECK_UPDATE,
  FACTORY_RESET,
  INFO,
};

constexpr const char* ICON_SIZES[] = {"S  3x5", "M  2x4", "L  1x4"};
constexpr const char* FONT_SIZES[] = {"Small", "Medium", "Large"};
constexpr const char* REFRESH_STEPS[] = {"5", "10", "20", "50"};
constexpr uint8_t REFRESH_VALUES[] = {5, 10, 20, 50};
constexpr const char* LOCK_STEPS[] = {"1 min", "5", "15", "30", "60"};
constexpr uint8_t LOCK_VALUES[] = {1, 5, 15, 30, 60};
constexpr const char* CLOCK_STYLES[] = {"Large", "Seconds", "Analog"};
constexpr const char* OK_ACTIONS[] = {"Back", "Home", "Nothing"};
constexpr const char* KEYBOARDS[] = {"QWERTY", "QWERTZ", "AZERTY"};
constexpr const char* SLEEP_STEPS[] = {"1 min", "5", "10", "30", "Never"};
constexpr uint8_t SLEEP_VALUES[] = {1, 5, 10, 30, 0};
constexpr const char* DATE_FORMATS[] = {"DD/MM/YY", "MM/DD/YY", "ISO"};

template <size_t N>
int indexOf(const uint8_t (&values)[N], const uint8_t value, const int fallback) {
  for (size_t i = 0; i < N; ++i) {
    if (values[i] == value) return static_cast<int>(i);
  }
  return fallback;
}

std::string masked(const std::string& secret) {
  if (secret.empty()) return "Not set";
  if (secret.size() <= 4) return "Set";
  return "\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2 " + secret.substr(secret.size() - 4);
}

std::string lastSyncText() {
  if (sync::busy()) return "Syncing now";
  const uint32_t last = sync::lastSyncTime();
  // generation() > 0: a run has finished since boot.
  const char* prefix = sync::generation() > 0 && !sync::lastRunOnline() ? "Offline. Last: " : "";
  if (last == 0) return std::string(prefix) + "Never";
  const std::time_t t = last;
  std::tm tm{};
  localtime_r(&t, &tm);
  char date[16];
  char clock[12];
  formatDate(tm, date, sizeof(date));
  formatClock(tm, clock, sizeof(clock));
  return std::string(prefix) + date + "  " + clock;
}

}  // namespace

SettingsActivity::SettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : FosActivity("FosSettings", renderer, mappedInput) {}

void SettingsActivity::onEnter() {
  open(Page::Main);
  FosActivity::onEnter();
}

const char* SettingsActivity::title() const {
  switch (page_) {
    case Page::Main:
      return "Settings";
    case Page::Apps:
      return "Apps and layout";
    case Page::Display:
      return "Display";
    case Page::Lock:
      return "Lock screen";
    case Page::Wifi:
      return "Wi-Fi and sync";
    case Page::Accounts:
      return "Accounts";
    case Page::Buttons:
      return "Buttons and keyboard";
    case Page::Power:
      return "Power";
    case Page::Region:
      return "Region and system";
    case Page::StorageUse:
      return "Storage";
    case Page::About:
      return "About FOs";
  }
  return "Settings";
}

void SettingsActivity::open(const Page page) {
  page_ = page;
  listPage_ = 0;
  confirmId_ = NONE;
  rebuild();
}

void SettingsActivity::addSection(const char* label) { items_.push_back({Kind::Section, NONE, label}); }

void SettingsActivity::addNav(const uint8_t id, const char* label, const std::string& detail) {
  items_.push_back({Kind::Nav, id, label, detail});
}

void SettingsActivity::addToggle(const uint8_t id, const char* label, const bool on, const char* detail) {
  Item item{Kind::Toggle, id, label, detail ? detail : ""};
  item.on = on;
  items_.push_back(std::move(item));
}

void SettingsActivity::addChoice(const uint8_t id, const char* label, const char* const* options, const int count,
                                 const int selected) {
  Item item{Kind::Choice, id, label};
  item.options = options;
  item.optionCount = count;
  item.selected = selected;
  items_.push_back(std::move(item));
}

void SettingsActivity::addInfo(const uint8_t id, const char* label, const std::string& value) {
  items_.push_back({Kind::Info, id, label, value});
}

void SettingsActivity::addButton(const uint8_t id, const char* label, const bool primary) {
  Item item{Kind::Button, id, label};
  item.primary = primary;
  items_.push_back(std::move(item));
}

void SettingsActivity::rebuild() {
  items_.clear();
  FosSettings& s = settings();
  switch (page_) {
    case Page::Main:
      addNav(NAV_APPS, "Apps and layout", "Show, hide and order apps, icon size");
      addNav(NAV_DISPLAY, "Display", "Font size, refresh, inverted colors");
      addNav(NAV_LOCK, "Lock screen", "Refresh, clock style, items");
      addNav(NAV_WIFI, "Wi-Fi and sync", "Networks, auto-sync, sync now");
      addNav(NAV_ACCOUNTS, "Accounts", "Lichess token, backend");
      addNav(NAV_BUTTONS, "Buttons and keyboard", "Button mapping, layout, accents");
      addNav(NAV_POWER, "Power", "Sleep, battery");
      addNav(NAV_REGION, "Region and system", "Clock, date, units, storage, reset");
      addNav(NAV_ABOUT, "About FOs", "Version and credits");
      break;

    case Page::Apps: {
      addChoice(ICON_SIZE, "ICON SIZE", ICON_SIZES, 3, static_cast<int>(s.iconSize));
      addToggle(LABELS, "Labels", s.showLabels, "App names under the icons");
      addSection("APPS  (ORDER AND VISIBILITY)");
      for (int i = 0; i < APP_COUNT; ++i) {
        const auto id = static_cast<AppId>(s.order[i]);
        Item item{Kind::AppRow, APP_ROW, appInfo(id).name};
        item.on = s.isVisible(id);
        item.arg = static_cast<int16_t>(id);
        item.detail = id == AppId::Settings ? "Always shown" : "";
        items_.push_back(std::move(item));
      }
      addButton(RESET_LAYOUT, "Reset layout");
      break;
    }

    case Page::Display:
      addChoice(FONT_SIZE, "SYSTEM FONT SIZE", FONT_SIZES, 3, static_cast<int>(s.fontSize));
      addChoice(REFRESH, "FULL REFRESH EVERY N UPDATES", REFRESH_STEPS, 4,
                indexOf(REFRESH_VALUES, s.fullRefreshEvery, 1));
      addSection("FEWER: CLEANER IMAGE.  MORE: FASTER SCREENS.");
      addToggle(INVERT, "Inverted colors", s.invertColors, "White on black");
      break;

    case Page::Lock:
      addChoice(LOCK_REFRESH, "REFRESH INTERVAL (MINUTES)", LOCK_STEPS, 5,
                indexOf(LOCK_VALUES, s.lockRefreshMinutes, 2));
      addChoice(CLOCK_STYLE, "CLOCK STYLE", CLOCK_STYLES, 3, static_cast<int>(s.clockStyle));
      addSection("ITEMS");
      addToggle(LOCK_WEATHER, "Weather", s.lockShowWeather, "From the last sync");
      addToggle(LOCK_CLIMATE, "Room climate", s.lockShowClimate, "Onboard temperature and humidity");
      addToggle(LOCK_EVENT, "Next event", s.lockShowNextEvent, "From the calendar");
      break;

    case Page::Wifi: {
      addSection("SAVED NETWORKS  (FIRST = HIGHEST PRIORITY)");
      const auto networks = WIFI_STORE.getCredentialSummaries();
      for (size_t i = 0; i < networks.size(); ++i) {
        Item item{Kind::NetworkRow, NETWORK_ROW, networks[i].ssid};
        item.arg = static_cast<int16_t>(i);
        item.detail = networks[i].isLastConnected ? "Last connected" : "";
        items_.push_back(std::move(item));
      }
      if (networks.empty()) addInfo(INFO, "No saved networks", "");
      addButton(ADD_NETWORK, "Add network");
      addSection("SYNC");
      addToggle(AUTO_SYNC, "Auto-sync", s.autoSync, "At boot and when an app opens");
      addInfo(LAST_SYNC, "Last sync", lastSyncText());
      addButton(SYNC_NOW, "Sync now", true);
      break;
    }

    case Page::Accounts:
      addNav(LICHESS, "Lichess API token", masked(s.lichessToken));
      addNav(LICHESS_URL, "Lichess server", s.lichessUrl);
      addNav(BACKEND_URL, "Backend address", s.backendUrl.empty() ? "Not set" : s.backendUrl);
      addNav(BACKEND_KEY, "Backend key", masked(s.backendKey));
      break;

    case Page::Buttons:
      addToggle(SWAP_BUTTONS, "Swap Up and Down", s.swapUpDown);
      addChoice(OK_ACTION, "OK BUTTON (SHORT PRESS)", OK_ACTIONS, 3, static_cast<int>(s.okAction));
      addSection("OK TWICE: HOME.  HOLD OK: SLEEP.");
      addChoice(KEYBOARD, "KEYBOARD LAYOUT", KEYBOARDS, 3, static_cast<int>(s.keyboardLayout));
      addToggle(ACCENTS, "Accented letters", s.accentedLetters,
                "Extra key row: \xC3\xA0 \xC3\xA8 \xC3\xA9 \xC3\xAC \xC3\xB2 \xC3\xB9");
      break;

    case Page::Power: {
      addChoice(SLEEP, "SLEEP AFTER", SLEEP_STEPS, 5, indexOf(SLEEP_VALUES, s.sleepMinutes, 1));
      char pct[16];
      snprintf(pct, sizeof(pct), "%d%%", powerManager.getBatteryPercentage());
      addInfo(BATTERY, "Battery level", pct);
      addToggle(SAVER, "Battery saver", s.batterySaver, "Longer refresh intervals, no auto-sync");
      break;
    }

    case Page::Region:
      addToggle(CLOCK24, "24-hour clock", s.clock24h);
      addChoice(DATE_FORMAT, "DATE FORMAT", DATE_FORMATS, 3, static_cast<int>(s.dateFormat));
      addToggle(DECIMAL_COMMA, "Decimal comma", s.decimalComma, "1.234,56 instead of 1,234.56");
      addToggle(METRIC, "Metric units", s.metricUnits);
      addSection("SYSTEM");
      addNav(NAV_STORAGE, "Storage per app");
      addButton(CLEAR_CACHES, "Clear caches");
      addInfo(FIRMWARE, "Firmware", FOS_VERSION_STRING);
      addButton(CHECK_UPDATE, "Check for update");
      addButton(FACTORY_RESET, "Factory reset");
      break;

    case Page::StorageUse: {
      addSection("FOs DATA ON THE MICROSD");
      for (int i = 0; i < APP_COUNT; ++i) {
        const auto id = static_cast<AppId>(i);
        char size[24];
        formatBytes(appDataBytes(id), size, sizeof(size));
        addInfo(INFO, appInfo(id).name, size);
      }
      break;
    }

    case Page::About:
      addInfo(INFO, "FOs", FOS_VERSION_STRING);
      addInfo(INFO, "Device", "Seeed reTerminal Sticky");
      addSection("BUILT ON");
      addInfo(INFO, "CrossPoint Reader", "MIT license");
      addInfo(INFO, "FreeInk SDK", "board support");
      addInfo(INFO, "Lucide icons", "ISC license");
      addInfo(INFO, "Space Grotesk, Space Mono", "OFL");
      addSection("THANKS");
      addInfo(INFO, "CrossPoint contributors", "github.com/crosspoint-reader");
      break;
  }
  paginate();
}

int SettingsActivity::itemHeight(const Item& item) const {
  switch (item.kind) {
    case Kind::Section:
      return 48;
    case Kind::Choice:
      return 124;
    case Kind::Button:
      return MIN_TARGET + SHADOW + GAP * 2;
    default:
      return ROW_H;
  }
}

void SettingsActivity::paginate() {
  const int listH = renderer.getScreenHeight() - STATUS_BAR_H - HEADER_H - MIN_TARGET - SHADOW - GAP * 2;
  pageStarts_.clear();
  pageStarts_.push_back(0);
  int used = 0;
  for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
    const int h = itemHeight(items_[i]);
    if (used + h > listH && used > 0) {
      pageStarts_.push_back(i);
      used = 0;
    }
    used += h;
  }
  if (listPage_ >= static_cast<int>(pageStarts_.size())) listPage_ = static_cast<int>(pageStarts_.size()) - 1;
}

void SettingsActivity::drawItem(Canvas& c, const Rect& r, const int index) {
  const Item& item = items_[index];
  const int16_t v = static_cast<int16_t>(index);
  switch (item.kind) {
    case Kind::Section:
      sectionLabel(c, {r.x, r.y + 12, r.w, r.h - 12}, item.label.c_str());
      break;
    case Kind::Nav:
      listRow(c, r, item.label.c_str(), item.detail.c_str(), nullptr, ITEM, v);
      break;
    case Kind::Toggle:
      toggleRow(c, r, item.label.c_str(), item.detail.c_str(), item.on, ITEM, v);
      break;
    case Kind::Info:
      listRow(c, r, item.label.c_str(), nullptr, item.detail.c_str(), 0, 0, false);
      break;
    case Kind::Choice: {
      const Rect inner = r.inset(GUTTER, 0);
      c.textIn({inner.x, r.y + 6, inner.w, 32}, font::SMALL, item.label.c_str(), Tone::Dark, Align::Left, true);
      segmented(c, {inner.x, r.y + 42, inner.w, 64}, item.options, item.optionCount, item.selected,
                static_cast<uint16_t>(CHOICE_BASE + index));
      break;
    }
    case Kind::Button: {
      const Rect b{r.x + GUTTER, r.y + GAP, r.w - 2 * GUTTER, MIN_TARGET + SHADOW};
      button(c, b, item.label.c_str(), ITEM, item.primary ? ButtonStyle::Primary : ButtonStyle::Secondary, v);
      break;
    }
    case Kind::AppRow: {
      // Name, reorder arrows, visibility toggle.
      c.fill(r, Tone::White);
      rule(c, r.x, r.bottom() - BORDER, r.w);
      Rect row{r.x + GUTTER, r.y, r.w - 2 * GUTTER, r.h - BORDER};
      const Rect tog = row.takeRight(68);
      if (item.arg != static_cast<int16_t>(AppId::Settings)) {
        toggle(c, {tog.x, tog.y + (tog.h - 36) / 2, 68, 36}, item.on);
        c.hit(tog, ITEM, v);
      }
      row.takeRight(GAP);
      const int bs = 52;
      glyphButton(c, {row.right() - bs, row.y + (row.h - bs) / 2, bs, bs}, Glyph::Down, MOVE_DOWN,
                  ButtonStyle::Secondary, v);
      row.takeRight(bs + 8);
      glyphButton(c, {row.right() - bs, row.y + (row.h - bs) / 2, bs, bs}, Glyph::Up, MOVE_UP, ButtonStyle::Secondary,
                  v);
      row.takeRight(bs + GAP);
      if (item.detail.empty()) {
        c.textIn(row, font::BODY, item.label.c_str(), item.on ? Tone::Black : Tone::Dark, Align::Left, true);
      } else {
        const int th = c.lineHeight(font::BODY);
        const int y = row.y + (row.h - th - c.lineHeight(font::SMALL)) / 2;
        c.textIn({row.x, y, row.w, th}, font::BODY, item.label.c_str(), Tone::Black, Align::Left, true);
        c.textIn({row.x, y + th, row.w, c.lineHeight(font::SMALL)}, font::SMALL, item.detail.c_str(), Tone::Dark);
      }
      break;
    }
    case Kind::NetworkRow: {
      c.fill(r, Tone::White);
      rule(c, r.x, r.bottom() - BORDER, r.w);
      Rect row{r.x + GUTTER, r.y, r.w - 2 * GUTTER, r.h - BORDER};
      const int bs = 52;
      const int by = row.y + (row.h - bs) / 2;
      glyphButton(c, {row.right() - bs, by, bs, bs}, Glyph::Close, REMOVE, ButtonStyle::Secondary, v);
      row.takeRight(bs + 8);
      glyphButton(c, {row.right() - bs, by, bs, bs}, Glyph::Down, MOVE_DOWN, ButtonStyle::Secondary, v);
      row.takeRight(bs + 8);
      glyphButton(c, {row.right() - bs, by, bs, bs}, Glyph::Up, MOVE_UP, ButtonStyle::Secondary, v);
      row.takeRight(bs + GAP);
      char label[48];
      snprintf(label, sizeof(label), "%d. %s", item.arg + 1, item.label.c_str());
      const int th = c.lineHeight(font::BODY);
      const int dh = item.detail.empty() ? 0 : c.lineHeight(font::SMALL);
      const int y = row.y + (row.h - th - dh) / 2;
      c.textIn({row.x, y, row.w, th}, font::BODY, label, Tone::Black, Align::Left, true);
      if (dh) c.textIn({row.x, y + th, row.w, dh}, font::SMALL, item.detail.c_str(), Tone::Dark);
      break;
    }
  }
}

void SettingsActivity::draw(Canvas& c, const Rect& body) {
  Rect area = body;
  header(c, area.takeTop(HEADER_H), title(), BACK);

  const int pages = static_cast<int>(pageStarts_.size());
  const Rect footer = area.takeBottom(MIN_TARGET + SHADOW + GAP * 2);
  if (pages > 1) {
    pager(c, {footer.x + GUTTER, footer.y + GAP, footer.w - 2 * GUTTER, MIN_TARGET + SHADOW}, listPage_, pages,
          PREV_PAGE, NEXT_PAGE);
  }

  rule(c, 0, area.y, c.width());
  int y = area.y + BORDER;
  const int start = pageStarts_.empty() ? 0 : pageStarts_[listPage_];
  const int end = listPage_ + 1 < pages ? pageStarts_[listPage_ + 1] : static_cast<int>(items_.size());
  for (int i = start; i < end; ++i) {
    const int h = itemHeight(items_[i]);
    drawItem(c, {0, y, c.width(), h}, i);
    y += h;
  }

  if (confirmId_ == CLEAR_CACHES) {
    dialog(c, "Clear caches?", "Removes the synced copies of all apps. Your own data and the Budgeteer queue stay.",
           "Clear", DIALOG_OK, "Cancel", DIALOG_CANCEL);
  } else if (confirmId_ == FACTORY_RESET) {
    dialog(c, "Factory reset?", "Erases all FOs settings and app data on the microSD. Books stay.", "Reset", DIALOG_OK,
           "Cancel", DIALOG_CANCEL);
  }
}

void SettingsActivity::onAction(const uint16_t action, const int16_t value) {
  if (action == DIALOG_CANCEL) {
    confirmId_ = NONE;
    return;
  }
  if (action == DIALOG_OK) {
    const uint8_t id = confirmId_;
    confirmId_ = NONE;
    if (id == CLEAR_CACHES) {
      clearSyncedCaches();
    } else if (id == FACTORY_RESET) {
      factoryReset();
      settings() = FosSettings();
      save();
      goHome();
    }
    return;
  }
  if (confirmId_ != NONE) return;

  if (action >= CHOICE_BASE) {
    choose(action - CHOICE_BASE, value);
    return;
  }
  FosSettings& s = settings();
  switch (action) {
    case BACK:
      onBack();
      break;
    case PREV_PAGE:
      onUp();
      break;
    case NEXT_PAGE:
      onDown();
      break;
    case ITEM:
      activate(value);
      break;
    case MOVE_UP:
    case MOVE_DOWN: {
      if (value < 0 || value >= static_cast<int>(items_.size())) break;
      const int delta = action == MOVE_UP ? -1 : 1;
      const Item& item = items_[value];
      if (item.kind == Kind::AppRow) {
        s.move(static_cast<AppId>(item.arg), delta);
        save();
      } else if (item.kind == Kind::NetworkRow) {
        WIFI_STORE.moveCredential(static_cast<size_t>(item.arg), delta);
      }
      rebuild();
      break;
    }
    case REMOVE:
      if (value >= 0 && value < static_cast<int>(items_.size())) {
        WIFI_STORE.removeCredential(items_[value].label);
        rebuild();
      }
      break;
    default:
      break;
  }
}

void SettingsActivity::activate(const int index) {
  if (index < 0 || index >= static_cast<int>(items_.size())) return;
  FosSettings& s = settings();
  const Item& item = items_[index];
  switch (item.id) {
    case NAV_APPS:
      stack_.push_back(page_), open(Page::Apps);
      return;
    case NAV_DISPLAY:
      stack_.push_back(page_), open(Page::Display);
      return;
    case NAV_LOCK:
      stack_.push_back(page_), open(Page::Lock);
      return;
    case NAV_WIFI:
      stack_.push_back(page_), open(Page::Wifi);
      return;
    case NAV_ACCOUNTS:
      stack_.push_back(page_), open(Page::Accounts);
      return;
    case NAV_BUTTONS:
      stack_.push_back(page_), open(Page::Buttons);
      return;
    case NAV_POWER:
      stack_.push_back(page_), open(Page::Power);
      return;
    case NAV_REGION:
      stack_.push_back(page_), open(Page::Region);
      return;
    case NAV_STORAGE:
      stack_.push_back(page_), open(Page::StorageUse);
      return;
    case NAV_ABOUT:
      stack_.push_back(page_), open(Page::About);
      return;
    case LABELS:
      s.showLabels = !s.showLabels;
      break;
    case APP_ROW:
      s.setVisible(static_cast<AppId>(item.arg), !item.on);
      break;
    case RESET_LAYOUT:
      s.resetLayout();
      break;
    case INVERT:
      s.invertColors = !s.invertColors;
      save();
      rebuild();
      refresh(true);
      return;
    case LOCK_WEATHER:
      s.lockShowWeather = !s.lockShowWeather;
      break;
    case LOCK_CLIMATE:
      s.lockShowClimate = !s.lockShowClimate;
      break;
    case LOCK_EVENT:
      s.lockShowNextEvent = !s.lockShowNextEvent;
      break;
    case ADD_NETWORK:
      startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput, false),
                             [this](const ActivityResult&) { rebuild(); });
      return;
    case AUTO_SYNC:
      s.autoSync = !s.autoSync;
      break;
    case SYNC_NOW:
      sync::syncNow();
      rebuild();
      return;
    case LICHESS:
      editText(LICHESS, "Lichess API token", s.lichessToken, true,
               "Create a personal token at lichess.org/account/oauth/token. Stored only on this device.");
      return;
    case LICHESS_URL:
      editText(LICHESS_URL, "Lichess server", s.lichessUrl, false, "Normally https://lichess.org");
      return;
    case BACKEND_URL:
      editText(BACKEND_URL, "Backend address", s.backendUrl, false, "For example http://192.168.1.20:8787");
      return;
    case BACKEND_KEY:
      editText(BACKEND_KEY, "Backend key", s.backendKey, true, "Sent with every sync request.");
      return;
    case SWAP_BUTTONS:
      s.swapUpDown = !s.swapUpDown;
      break;
    case ACCENTS:
      s.accentedLetters = !s.accentedLetters;
      break;
    case SAVER:
      s.batterySaver = !s.batterySaver;
      break;
    case CLOCK24:
      s.clock24h = !s.clock24h;
      break;
    case DECIMAL_COMMA:
      s.decimalComma = !s.decimalComma;
      break;
    case METRIC:
      s.metricUnits = !s.metricUnits;
      break;
    case CLEAR_CACHES:
    case FACTORY_RESET:
      confirmId_ = item.id;
      return;
    case CHECK_UPDATE:
      startActivityForResult(std::make_unique<OtaUpdateActivity>(renderer, mappedInput), [](const ActivityResult&) {});
      return;
    default:
      return;
  }
  save();
  rebuild();
}

void SettingsActivity::choose(const int index, const int option) {
  if (index < 0 || index >= static_cast<int>(items_.size()) || option < 0) return;
  FosSettings& s = settings();
  switch (items_[index].id) {
    case ICON_SIZE:
      s.iconSize = static_cast<IconSize>(option);
      break;
    case FONT_SIZE:
      s.fontSize = static_cast<FontSize>(option);
      break;
    case REFRESH:
      s.fullRefreshEvery = REFRESH_VALUES[option];
      break;
    case LOCK_REFRESH:
      s.lockRefreshMinutes = LOCK_VALUES[option];
      break;
    case CLOCK_STYLE:
      s.clockStyle = static_cast<ClockStyle>(option);
      break;
    case OK_ACTION:
      s.okAction = static_cast<OkAction>(option);
      break;
    case KEYBOARD:
      s.keyboardLayout = static_cast<KeyboardLayout>(option);
      break;
    case SLEEP:
      s.sleepMinutes = SLEEP_VALUES[option];
      break;
    case DATE_FORMAT:
      s.dateFormat = static_cast<DateFormat>(option);
      break;
    default:
      return;
  }
  save();
  rebuild();
}

void SettingsActivity::editText(const uint8_t id, const char* title, const std::string& value, const bool secret,
                                const char* hint) {
  startActivityForResult(std::make_unique<TextEntryActivity>(renderer, mappedInput, title, value, secret, 200, hint),
                         [this, id](const ActivityResult& result) {
                           if (result.isCancelled) return;
                           const auto* text = std::get_if<KeyboardResult>(&result.data);
                           if (!text) return;
                           FosSettings& s = settings();
                           if (id == LICHESS) s.lichessToken = text->text;
                           if (id == LICHESS_URL)
                             s.lichessUrl = text->text.empty() ? "https://lichess.org" : text->text;
                           if (id == BACKEND_URL) s.backendUrl = text->text;
                           if (id == BACKEND_KEY) s.backendKey = text->text;
                           save();
                           rebuild();
                         });
}

void SettingsActivity::save() {
  settings().save();
  applySettings();
}

bool SettingsActivity::onUp() {
  if (listPage_ == 0) return false;
  --listPage_;
  return true;
}

bool SettingsActivity::onDown() {
  if (listPage_ + 1 >= static_cast<int>(pageStarts_.size())) return false;
  ++listPage_;
  return true;
}

void SettingsActivity::onBack() {
  if (confirmId_ != NONE) {
    confirmId_ = NONE;
    requestUpdate();
    return;
  }
  if (stack_.empty()) {
    finish();
    return;
  }
  const Page previous = stack_.back();
  stack_.pop_back();
  open(previous);
  requestUpdate();
}

void SettingsActivity::onTick() {
  // "Last sync" follows the sync worker while the Wi-Fi page is open.
  if (page_ == Page::Wifi && sync::generation() != syncGen_) {
    syncGen_ = sync::generation();
    rebuild();
    requestUpdate();
  }
  // Keep the battery level fresh while the Power page is open.
  if (page_ == Page::Power && millis() - lastBatteryAt_ > 30000) {
    lastBatteryAt_ = millis();
    rebuild();
    requestUpdate();
  }
}

}  // namespace fos
