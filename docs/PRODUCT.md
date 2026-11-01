# FOs — Custom reTerminal Sticky Firmware

Oct 26, 2026 · @Francesco · revised Nov 1, 2026 (no backend; Budgeteer, Calendar, Read-later, Flashcards and Puzzles removed)

## What it is

FOs is a custom firmware that turns the Seeed reTerminal Sticky into a pocket device for everyday things: tasks, notes, reading, chess, small utilities and a remote terminal. It is built on a fork of the Crosspoint e-reader firmware.

&#91;embedded content: FOs launcher · page 1 of 2\]

- **Offline-first, no server:** every app except Terminal works with no network, and there is no backend. Wi-Fi (home or phone hotspot) is used only for the lock screen weather, Lichess, the Terminal relay and CrossPoint's book tools.
- **Glanceable:** e-paper, high contrast, portrait, one main action per screen.
- **Configured on the device:** Settings decides which apps appear, their order and icon size.
- **Unlocked:** no PIN or password; whoever holds the device can use it.
- **Not an assistant:** no voice, no AI on the device, no notifications fighting for attention.

## Design language

FOs is neo-brutalist, the style that looks best on a 4-level grayscale e-paper screen. Hard edges and high contrast stay crisp through partial refreshes, and nothing depends on animation. Every screen is designed for portrait, 480×800.

- **Thick outlines:** every card, tile and button has a 3 px black border.
- **Hard shadows:** solid black, offset 4 px down-right, no blur. A pressed button loses its shadow and shifts 4 px, so taps feel physical without animation.
- **Flat fills:** only the four gray levels (white, light gray, dark gray, black). No gradients, no dithering.
- **Heavy type:** bold grotesque for titles, oversized numerals for clocks, timers and results.
- **Inverted focus:** the one thing that matters on a screen (running timer, whose move, the answer) is white on black.
- **Big targets:** at least 64×64 px, and each screen's main action is one full-width block.
- **Status bar:** time, battery and Wi-Fi on every screen, plus a tag when a timer has ended.

## The app lineup

FOs ships eleven tools. All but Terminal work offline; Chess (Lichess) and the lock screen weather use the network when it is available.

&#91;embedded content: App lineup · 11 tools\]

## Screens

Every screen does one job and ends in one full-width action, in portrait at 480×800.

&#91;embedded content: App screens · portrait, sample data\]

## Apps in detail

Every app works offline except Terminal. Data stays on the device, in one folder per app on the microSD.

### Planning

**To-do.** Lists, due dates, recurring tasks and tap-to-complete, typed on a compact on-screen keyboard. Data stays on the device.

**Notes and sketch.** Typed notes and freehand touch drawing on partial refresh. Stored on the device.

### Reading

**Reader.** The Crosspoint e-reader: EPUB from microSD, bookmarks, saved position, adjustable font and margins, page turns by button or swipe.

### Play

**Chess.** Four modes:

- **Vs bot:** an offline lightweight engine with adjustable strength.
- **1v1:** pass-and-play, the board flips after every move, optional clock.
- **Lichess:** correspondence, classical and rapid games plus the daily puzzle, using your personal API token typed once in Settings. No blitz, because of e-paper refresh times.
- **Blindfold trainer:** square-color and coordinate drills, a 15-minute daily session and a streak counter.

### Utilities

**Timers.** Pomodoro (configurable work and break lengths), stopwatch with laps, and countdown. Timers keep running through sleep; the end is shown on screen.

**Calculator.** Standard calculator plus unit converter (length, weight, volume, temperature).

**Wallet.** Loyalty cards, tickets and boarding passes. Codes are added on the device or copied to the microSD as text with a label, and the device draws each QR code or barcode itself at full contrast.

**Habits.** A daily checklist with a streak grid of the last weeks. Data stays on the device.

**Terminal.** A terminal-style client for Claude Code sessions running on your computer, through a small relay script there (tools/terminal-relay). It needs a network and works live.

- Monospace transcript of the session, scrolled with the buttons.
- Quick-reply chips (continue, run the tests, explain) and the on-screen keyboard for short prompts.
- Permission requests shown as full-width Approve and Deny buttons.

### System

**Lock screen.** The idle screen: clock, date, weather from Open-Meteo for a place set in Settings, room temperature and humidity from the onboard sensor, and a running timer. It is not a lock; there is no PIN or password. Its refresh interval and items are set in Settings.

**Settings.** A full app for everything the device does. It is always in the launcher and can't be hidden.

&#91;embedded content: Settings · main menu, apps and layout, icon sizes\]

- **Apps and layout:** show or hide each app, reorder them, pick the icon size (small 3×5 grid, medium 2×4, large single column), and turn labels on or off.
- **Display:** system font size, full-refresh interval (cleaner image vs. faster screens), inverted colors.
- **Lock screen:** refresh interval, clock style, weather on/off and its place, room climate on/off.
- **Wi-Fi:** saved networks with priority.
- **Accounts:** Lichess API token and server, Terminal relay address and key.
- **Buttons and keyboard:** button mapping, keyboard layout, accented letters.
- **Power:** sleep timeout, battery level, battery saver.
- **Region and system:** 24-hour clock, date format, decimal comma, metric units, storage per app, clear caches, firmware version and update check, factory reset.

## Hardware

FOs runs on the stock Seeed reTerminal Sticky, with no hardware changes.

| Component | Spec | How FOs uses it |
| --- | --- | --- |
| SoC | ESP32-S3R8, 8 MB PSRAM | Lightweight chess engine instead of Stockfish |
| Display | 3.97-inch e-paper, 800×480, 4-level grayscale, capacitive touch | Partial refresh in apps, full refresh every N updates |
| Storage | 32 MB flash + microSD | All data, books and the weather copy on the microSD |
| Connectivity | 2.4 GHz Wi-Fi, BLE 5.0 | Weather, Lichess, Terminal over home Wi-Fi or phone hotspot; BLE unused |
| Battery | 750 mAh, USB-C | Deep sleep between interactions |
| Sensors | Temperature/humidity, 3-axis accelerometer | Room climate on the lock screen; wake on tap |
| Audio | Single PDM microphone, no speaker | Unused |
| Input | Touch + physical buttons | Buttons for home, back and page turns |

## How it works

There is no backend. The device reaches three services directly, each optional: Open-Meteo (weather, no account), Lichess (personal token) and the Terminal relay on your computer.

- **Launcher:** an app grid with only the enabled apps, in the order and icon size set in Settings. Physical buttons handle home, back and page turns.
- **Storage:** one folder per app on the microSD, plus the settings.
- **Weather:** fetched at boot (unless battery saver is on) and when the lock screen app opens; the last copy is kept for offline use.
- **Power:** deep sleep between interactions, partial refresh inside apps, a full refresh every N updates.
- **Updates:** over the air from GitHub releases.

## Configuration

- Settings live only on the device.
- Hidden apps stay in the firmware and keep their data, so showing one again is instant.

## To verify during the build

- [ ] Number of physical buttons and their mapping.
- [ ] Whether the Sticky has any buzzer or vibration for timer alerts.
- [ ] Crosspoint's license, and whether it can host a multi-app launcher.
- [ ] Memory headroom on 8 MB of PSRAM with the chess engine, reader, Wi-Fi stack and Terminal's live connection.
- [ ] Battery life with lock-screen refreshes and weather fetches, to tune the defaults.
- [ ] Chess engine choice and its strength levels.
- [ ] Terminal readability: how many columns fit at a readable font size.
