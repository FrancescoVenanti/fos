#pragma once

#include <cstdint>
#include <string>

#include "fos/apps/AppRegistry.h"

namespace fos {

enum class IconSize : uint8_t { Small = 0, Medium, Large };  // 3x5, 2x4, 1x4 launcher grid
enum class FontSize : uint8_t { Small = 0, Medium, Large };
enum class ClockStyle : uint8_t { Large = 0, WithSeconds, Analog };
enum class DateFormat : uint8_t { DayMonthYear = 0, MonthDayYear, Iso };
enum class KeyboardLayout : uint8_t { Qwerty = 0, Qwertz, Azerty };
enum class OkAction : uint8_t { Back = 0, Home, Nothing };

// Every FOs setting. Lives only on the device (/fos/settings.json) and is never
// synced. Settings itself is always visible in the launcher.
struct FosSettings {
  // Apps and layout
  uint8_t order[APP_COUNT];  // launcher order: AppId values
  bool visible[APP_COUNT];
  IconSize iconSize = IconSize::Medium;
  bool showLabels = true;

  // Display
  FontSize fontSize = FontSize::Medium;
  uint8_t fullRefreshEvery = 10;  // updates between full refreshes (0 = only when entering an app)
  bool invertColors = false;

  // Lock screen
  uint8_t lockRefreshMinutes = 15;
  ClockStyle clockStyle = ClockStyle::Large;
  bool lockShowWeather = true;
  bool lockShowClimate = true;
  bool lockShowNextEvent = true;

  // Wi-Fi and sync. Saved networks live in CrossPoint's WifiCredentialStore
  // (/.crosspoint/wifi.json, passwords obfuscated); their order is the priority.
  bool autoSync = true;

  // Accounts
  std::string lichessToken;
  std::string lichessUrl = "https://lichess.org";  // a test server can stand in (tools/mock-backend /lichess)
  std::string backendUrl = "http://127.0.0.1:8787";
  std::string backendKey;

  // Buttons and keyboard
  bool swapUpDown = false;
  OkAction okAction = OkAction::Back;
  KeyboardLayout keyboardLayout = KeyboardLayout::Qwerty;
  bool accentedLetters = true;

  // Power
  uint8_t sleepMinutes = 5;  // 0 = never
  bool batterySaver = false;

  // Region and system
  bool clock24h = true;
  DateFormat dateFormat = DateFormat::DayMonthYear;
  bool decimalComma = true;
  bool metricUnits = true;

  FosSettings();

  bool isVisible(AppId id) const;
  void setVisible(AppId id, bool show);
  // Visible apps in launcher order.
  int visibleApps(AppId* out, int capacity) const;
  // Moves an app one step earlier (-1) or later (+1) in the launcher order.
  void move(AppId id, int delta);
  void resetLayout();

  bool load();
  bool save() const;
};

FosSettings& settings();

// Applies the settings that other subsystems read (sleep timeout, inversion).
void applySettings();

}  // namespace fos
