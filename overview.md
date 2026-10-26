# FOs — Custom reTerminal Sticky Firmware

Oct 26, 2026 · @Francesco

## What it is

FOs is a custom firmware that turns the Seeed reTerminal Sticky into a pocket device for everyday things: money, tasks, reading, learning, chess, small utilities and a remote terminal. It is built on a fork of the Crosspoint e-reader firmware.

&#91;embedded content: FOs launcher · page 1 of 2\]

- **Offline-first:** every app except Terminal works with no network. Wi-Fi (home or phone hotspot) is only used to sync.
- **Glanceable:** e-paper, high contrast, portrait, one main action per screen.
- **Configured on the device:** Settings decides which apps appear, their order and icon size.
- **Unlocked:** no PIN or password; whoever holds the device can use it.
- **Not an assistant:** no voice, no AI on the device, no notifications fighting for attention.

## Design language

FOs is neo-brutalist, the style that looks best on a 4-level grayscale e-paper screen. Hard edges and high contrast stay crisp through partial refreshes, and nothing depends on animation. Every screen is designed for portrait, 480×800.

- **Thick outlines:** every card, tile and button has a 3 px black border.
- **Hard shadows:** solid black, offset 4 px down-right, no blur. A pressed button loses its shadow and shifts 4 px, so taps feel physical without animation.
- **Flat fills:** only the four gray levels (white, light gray, dark gray, black). No gradients, no dithering.
- **Heavy type:** bold grotesque for titles, oversized numerals for money, clocks and timers.
- **Inverted focus:** the one thing that matters on a screen (balance, running timer, whose move) is white on black.
- **Big targets:** at least 64×64 px, and each screen's main action is one full-width block.
- **Status bar:** time, battery, Wi-Fi and sync state on every screen.

## The app lineup

FOs ships sixteen tools in five groups. All but Terminal work offline; the tagged ones use the network when it is available.

&#91;embedded content: App lineup · 16 tools in 5 groups\]

## Screens

Every screen does one job and ends in one full-width action, in portrait at 480×800.

&#91;embedded content: App screens · portrait, sample data\]

## Apps in detail

Every app works offline except Terminal. Apps that use synced data fetch it when a network is available and keep the last copy on the microSD.

### Money and planning

**Budgeteer.** One app with two states.

- **Offline:** only the **+ New transaction** button and a counter of queued entries ("3 to sync").
- **Online:** opening the app sends the queue, then loads the month. Home shows income, expenses and balance; tabs show spent vs. budget per category and the transaction list, with arrows to change month.

Adding a transaction takes six steps:

1. **Type:** Expense or Income.
2. **Amount:** numeric keypad with decimal comma.
3. **Category:** grid filtered by type.
4. **Description:** chips with the last 5 descriptions for that category, the keyboard, or Skip.
5. **Date:** Today by default, with Yesterday and ±1 day.
6. **Save:** sent at once if online, otherwise queued.

Categories arrive with the synced data, so the device never hardcodes them. Income categories are Stipendio and Introito2; expense categories are Equitalia, Bollette, Trasporti, Spesa, Svago, Ristoranti, Risparmio, Abbonamenti, Salute, Cura and NoCat, with NoCat kept out of the monthly balance. A queued entry leaves the queue only once the backend confirms it, so a lost confirmation can cause a rare duplicate.

**To-do.** Lists, due dates, recurring tasks and tap-to-complete, typed on a compact on-screen keyboard. Data stays on the device.

**Calendar.** A read-only agenda for today and the next 7 days, from synced data. Offline, it shows the last copy.

**Notes and sketch.** Typed notes and freehand touch drawing on partial refresh. Stored on the device.

### Reading and learning

**Reader.** The Crosspoint e-reader: EPUB from microSD, bookmarks, saved position, adjustable font and margins, page turns by button or swipe.

**Read-later.** Articles arrive on sync as clean text and open in the reader engine. Marking one as read is sent back on the next sync.

**Flashcards.** Decks arrive on sync or are copied to the microSD as CSV. Spaced-repetition scheduling runs on the device, with a daily review count.

### Play

**Chess.** Four modes:

- **Vs bot:** an offline lightweight engine with adjustable strength.
- **1v1:** pass-and-play, the board flips after every move, optional clock.
- **Lichess:** correspondence, classical and rapid games plus the daily puzzle, using your personal API token typed once in Settings. No blitz, because of e-paper refresh times.
- **Blindfold trainer:** square-color and coordinate drills, a 15-minute daily session and a streak counter.

**Puzzles.** Sudoku generated offline in several difficulties, plus word puzzles: a bundled pack and a daily puzzle that arrives on sync.

### Utilities

**Timers.** Pomodoro (configurable work and break lengths), stopwatch with laps, and countdown. Timers keep running through sleep; the end is shown on screen.

**Calculator.** Standard calculator plus unit converter (length, weight, volume, temperature, currency). Currency rates arrive on sync and are cached.

**Wallet.** Loyalty cards, tickets and boarding passes. Codes arrive on sync as text with a label, and the device draws each QR code or barcode itself at full contrast.

**Habits.** A daily checklist with a streak grid of the last weeks. Data stays on the device.

**Terminal.** A terminal-style client for Claude Code sessions running on your computer. It needs a network and works live rather than through the sync queue.

- Monospace transcript of the session, scrolled with the buttons.
- Quick-reply chips (continue, run the tests, explain) and the on-screen keyboard for short prompts.
- Permission requests shown as full-width Approve and Deny buttons.

### System

**Lock screen.** The idle screen: clock, date, synced weather, and room temperature and humidity from the onboard sensor. It is not a lock; there is no PIN or password. Its refresh interval and items are set in Settings.

**Settings.** A full app for everything the device does. It is always in the launcher and can't be hidden.

&#91;embedded content: Settings · main menu, apps and layout, icon sizes\]

- **Apps and layout:** show or hide each app, reorder them, pick the icon size (small 3×5 grid, medium 2×4, large single column), and turn labels on or off.
- **Display:** system font size, full-refresh interval (cleaner image vs. faster screens), inverted colors.
- **Lock screen:** refresh interval, clock style, and which items show (weather, room climate, next event).
- **Wi-Fi and sync:** saved networks with priority, auto-sync on or off, sync now, last sync time.
- **Accounts:** Lichess API token, backend address and key.
- **Buttons and keyboard:** button mapping, keyboard layout, accented letters.
- **Power:** sleep timeout, battery level, battery saver.
- **Region and system:** 24-hour clock, date format, decimal comma, metric units, storage per app, clear caches, firmware version and update check, factory reset.

## Hardware

FOs runs on the stock Seeed reTerminal Sticky, with no hardware changes.

| Component | Spec | How FOs uses it |
| --- | --- | --- |
| SoC | ESP32-S3R8, 8 MB PSRAM | Lightweight chess engine instead of Stockfish |
| Display | 3.97-inch e-paper, 800×480, 4-level grayscale, capacitive touch | Partial refresh in apps, full refresh every N updates |
| Storage | 32 MB flash + microSD | All data, books, articles, decks and caches on the microSD |
| Connectivity | 2.4 GHz Wi-Fi, BLE 5.0 | Sync over home Wi-Fi or phone hotspot; BLE unused |
| Battery | 750 mAh, USB-C | Deep sleep between interactions |
| Sensors | Temperature/humidity, 3-axis accelerometer | Room climate on the lock screen; wake on tap |
| Audio | Single PDM microphone, no speaker | Unused |
| Input | Touch + physical buttons | Buttons for home, back and page turns |

## How it works

All synced data moves over a single backend connection, defined in a separate integration project. Lichess is the only service the device reaches directly.

&#91;embedded content: How it works · device, backend, Lichess\]

- **Launcher:** an app grid with only the enabled apps, in the order and icon size set in Settings. Physical buttons handle home, back and page turns.
- **Storage:** one folder per app on the microSD, plus the Budgeteer queue, synced content and the settings.
- **Sync:** at boot and when an app that needs data opens, over home Wi-Fi or a phone hotspot. Queued entries go first, then fresh data comes down.
- **Power:** deep sleep between interactions, partial refresh inside apps, a full refresh every N updates.
- **Updates:** over the air from GitHub releases.

## Sync and configuration

The device expects one backend that hands it per-app data, and accepts a few things back. How that backend is built is out of scope here.

| Area | Device receives | Device sends |
| --- | --- | --- |
| Budgeteer | Categories, month transactions, totals and budgets | New transactions |
| Calendar | Agenda for 7 days | — |
| Read-later | Articles as clean text | Read status |
| Flashcards | Decks | — |
| Puzzles | Daily word puzzle | — |
| Calculator | Currency rates | — |
| Wallet | Code payloads and labels | — |
| Lock screen | Weather | — |
| Terminal | Session output and permission requests, live | Prompts and approvals, live |

Rules:

- Settings live only on the device; layout and app visibility are never synced.
- Hidden apps stay in the firmware and keep their data, so showing one again is instant.
- Each app's last synced copy stays on the microSD for offline use.

## To verify during the build

- [ ] Number of physical buttons and their mapping.
- [ ] Whether the Sticky has any buzzer or vibration for timer alerts.
- [ ] Crosspoint's license, and whether it can host a multi-app launcher.
- [ ] Memory headroom on 8 MB of PSRAM with the chess engine, reader, Wi-Fi stack and Terminal's live connection.
- [ ] Battery life with lock-screen refreshes and syncs, to tune the defaults.
- [ ] Chess engine choice and its strength levels.
- [ ] Terminal readability: how many columns fit at a readable font size.
