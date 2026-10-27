# FOs progress

Legend: `[x]` done · `[~]` in progress · `[ ]` not started.
Firmware size is reported for the `sticky` environment after each phase.

## Phase 0 — Environment check
- [x] git, gh (authenticated), python, pio, MSYS2 UCRT64 gcc, SDL2, clang-format 21

## Phase 1 — Repository
- [x] Fork crosspoint-reader as `fos`, clone with submodules
- [x] `docs/PRODUCT.md` from the spec
- [x] Keep CrossPoint license; credit CrossPoint in README and the About screen

## Phase 2 — Understand
- [x] Study architecture, rendering, input, fonts, SD caching, build environments
- [x] CLAUDE.md, PROGRESS.md, docs/DECISIONS.md

## Phase 3 — Simulator
- [x] `simulator` env: native Windows build (MSYS2 UCRT64 gcc + SDL2)
- [x] 480×800 portrait window, exactly 4 grays, optional 2× scaling (`FOS_SIM_SCALE`)
- [x] Mouse = touch, keys = physical buttons (mapping in CLAUDE.md)
- [x] Local folder as microSD (`fs_`, or `CROSSPOINT_SIM_SD`)
- [x] Headless test mode: scripted inputs → PNG screenshots (`tools/simtest/run.py`)

## Phase 4 — Hardware target
- [x] `sticky` env builds (ESP32-S3); RAM/flash usage reported

## Phase 5 — FOs foundations
- [x] Rebrand: boot screen, sleep screen, version string, About (credits CrossPoint), web page titles
- [x] Design system: outlines, hard shadows, 4-gray fills, heavy type (`src/fos/ui/`)
- [x] Components: status bar, header, buttons, cards, list rows, toggles, segmented control, chips, dialog, pager
- [x] On-screen keyboard (QWERTY/QWERTZ/AZERTY, accents), numeric keypad
- [x] Launcher: icon sizes S (3×5), M (2×4), L (1×4), labels on/off, pages
- [x] Settings app (all sections from the spec), persisted on SD, never hideable

## Phase 6 — Mock backend
- [x] `tools/mock-backend` (Python stdlib) for every row of the sync table, with tests
- [x] Sync service (`src/fos/sync/`): worker task, push then pull, per-app caches, started at boot
- [x] Backend address and key in Settings > Accounts; sync now / last sync in Settings > Wi-Fi and sync
- [x] Backend optional: Google Sheet (Budgeteer), iCal, Open-Meteo, Frankfurter as direct sources
- [x] Online/offline tested in the simulator (`budgeteer-online` runs the mock backend, `budgeteer-offline` an unreachable one)

## Phase 7 — Apps
- [x] Budgeteer (offline queue, 6-step entry, month view)
- [x] To-do (lists, due dates, recurring tasks, tap-to-complete)
- [x] Chess (vs bot, 1v1, Lichess (mocked), blindfold trainer)
- [x] Reader (CrossPoint)
- [x] Lock screen (clock styles, weather, room climate, next event; sleep screen with timed refresh)
- [x] Timers (Pomodoro, stopwatch with laps, countdown; run through sleep)
- [x] Calculator (expression calculator, unit converter, synced currency rates)
- [x] Notes and sketch (multi-line notes, freehand drawing with pen, eraser, undo)
- [x] Habits (daily checklist, streaks, 5-week grid)
- [x] Wallet (QR, Code 128, EAN-13 drawn on the device; synced or on the card)
- [x] Flashcards (spaced repetition on the device; synced decks and CSV files)
- [ ] Read-later
- [ ] Puzzles
- [x] Calendar (read-only 7-day agenda, last copy offline)
- [ ] Terminal (UI + mock session)

## Size history (sticky)

Static RAM is of the 320 KB internal DRAM (8 MB PSRAM not counted); flash is of the
6.25 MB app partition.

| Phase | RAM | Flash |
| --- | --- | --- |
| 4 (CrossPoint baseline, prebuilt core) | 78,676 B (24.0%) | 5,304,363 B (80.9%) |
| 5 (design system, launcher, settings, fonts) | 78,924 B (24.1%) | 5,429,707 B (82.9%) |
| 7 (Budgeteer) | 79,012 B (24.1%) | 5,459,779 B (83.3%) |
| 7 (To-do) | 79,052 B (24.1%) | 5,479,711 B (83.6%) |
| 7 (Calendar) | 79,052 B (24.1%) | 5,490,155 B (83.8%) |
| 7 (Lock screen) | 79,068 B (24.1%) | 5,503,527 B (84.0%) |
| 7 (Timers) | 79,212 B (24.2%) | 5,517,359 B (84.2%) |
| 7 (Reader, Chess) | 79,236 B (24.2%) | 5,614,831 B (85.7%) |
| 7 (direct sources, Calculator) | 79,428 B (24.2%) | 5,650,131 B (86.2%) |
