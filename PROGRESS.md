# FOs progress

Legend: `[x]` done · `[~]` in progress · `[ ]` not started.
Firmware size is reported for the `sticky` environment after each phase.

## Phase 0 — Environment check
- [x] git, gh (authenticated), python, pio, MSYS2 UCRT64 gcc, SDL2, clang-format 21

## Phase 1 — Repository
- [x] Fork crosspoint-reader as `fos`, clone with submodules
- [x] `docs/PRODUCT.md` from the spec
- [x] Keep CrossPoint license; credit CrossPoint in README (About screen: Phase 5)

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
- [ ] Rebrand: boot screen, version string, About (credits CrossPoint)
- [ ] Design system: outlines, hard shadows, 4-gray fills, heavy type
- [ ] Components: status bar, buttons, cards, list rows, toggles, segmented control
- [ ] On-screen keyboard, numeric keypad
- [ ] Launcher: icon sizes S (3×5), M (2×4), L (1×4), labels on/off, pages
- [ ] Settings app (all sections from the spec), persisted on SD, never hideable

## Phase 6 — Mock backend
- [ ] `tools/mock-backend` (Python stdlib) for every row of the sync table
- [ ] Backend address in Settings > Accounts; online/offline tested

## Phase 7 — Apps
- [ ] Budgeteer (offline queue, 6-step entry, month view)
- [ ] To-do
- [ ] Chess (vs bot, 1v1, Lichess (mocked), blindfold trainer)
- [ ] Reader (CrossPoint)
- [ ] Lock screen
- [ ] Timers
- [ ] Calculator
- [ ] Notes and sketch
- [ ] Habits
- [ ] Wallet
- [ ] Flashcards
- [ ] Read-later
- [ ] Puzzles
- [ ] Calendar
- [ ] Terminal (UI + mock session)

## Size history (sticky)

Static RAM is of the 320 KB internal DRAM (8 MB PSRAM not counted); flash is of the
6.25 MB app partition.

| Phase | RAM | Flash |
| --- | --- | --- |
| 4 (CrossPoint baseline, prebuilt core) | 78,676 B (24.0%) | 5,304,363 B (80.9%) |
