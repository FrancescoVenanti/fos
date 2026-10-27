# FOs â€” guide for coding agents

FOs is a custom firmware for the **Seeed reTerminal Sticky** (ESP32-S3R8, 8 MB
PSRAM, 3.97" SSD1677 e-paper 800Ã—480 used as 480Ã—800 portrait, 4 grays, GT911
touch, microSD). It is a hard fork of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader).

- Product spec: [docs/PRODUCT.md](docs/PRODUCT.md) (the spec says "Sticky OS"; the product name is **FOs**).
- Progress checklist: [PROGRESS.md](PROGRESS.md). Design decisions: [docs/DECISIONS.md](docs/DECISIONS.md).
- CrossPoint's own agent rules are in [AGENTS.md](AGENTS.md) and `.agents/rules/`. They are
  accurate for CrossPoint internals (HAL, heap, rendering contracts). Their PR/commit
  workflow rules are for upstream contributions and do not apply to this fork.

## Repository layout

| Path | What |
| --- | --- |
| `src/main.cpp` | Arduino `setup()`/`loop()`: boot, sleep, input dispatch, activity loop |
| `src/activities/` | CrossPoint screens (reader, library, settings, network, ...) |
| `src/fos/` | **All FOs code**: design system, launcher, settings, apps, sync |
| `src/fos/ui/` | Neo-brutalist design system (`Canvas`, components, keyboard, keypad) |
| `src/fos/apps/` | One folder per FOs app |
| `lib/` | CrossPoint libraries: `GfxRenderer`, `EpdFont`, `Epub`, `hal/`, ... |
| `freeink-sdk/` | Submodule: board profiles (`BoardConfig.h` â†’ `STICKY`), drivers, FreeInkUI |
| `sim/crosspoint-simulator/` | Vendored desktop simulator (MIT), ported to native Windows. Replaces `lib/hal` in the `simulator` env. Do not reformat it |
| `tools/mock-backend/` | Python stdlib mock of the sync backend |
| `tools/simtest/` | Headless simulator test scripts and screenshot output |
| `test/` | CrossPoint host gtest suites (CMake) |

## Build, run, test (Windows 11, PowerShell, no WSL)

Prerequisites: PlatformIO Core (`%USERPROFILE%\.platformio\penv\Scripts\pio.exe`),
MSYS2 UCRT64 at `C:\msys64` with `gcc` and `SDL2` (override with `FOS_MSYS2_ROOT`),
LLVM clang-format 21, Python 3.12. `pio` is not on PATH on this machine, so commands use:

```powershell
$pio = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"
```

`tools\build.ps1 <env>` wraps `pio run` with UTF-8 output (PlatformIO crashes on
non-ASCII lines when its output is redirected), writes `.pio\build-<env>.log` and prints
errors plus RAM/flash use.

| Task | Command |
| --- | --- |
| Build simulator | `.\tools\build.ps1 simulator` |
| Run simulator | `.\.pio\build\simulator\program.exe` (2Ã— window: `$env:FOS_SIM_SCALE="2"`) |
| Headless UI test | `python tools\simtest\run.py <scenario>` â†’ PNGs in `tools\simtest\out\<scenario>\` |
| All headless scenarios | `python tools\simtest\run.py --all` |
| Build device firmware | `.\tools\build.ps1 sticky` |
| Flash device | `.\tools\build.ps1 sticky -Target upload` |
| Static analysis | `& $pio check -e sticky --skip-packages` |
| Format | `.\bin\clang-format-fix.ps1 -g` (stage new files first: `-g` only sees tracked changes) |
| Mock backend | `python tools\mock-backend\server.py` (http://127.0.0.1:8787) |
| Regenerate fonts / icons | `.venv\Scripts\python tools\fonts\build_fos_fonts.py` / `tools\icons\build_fos_icons.py` |

A first simulator build compiles the whole firmware natively and takes 10+ minutes;
incremental builds are fast. Editing `platformio.ini` makes PlatformIO wipe **all** of
`.pio/build`, so never edit it while another env is building.

### Simulator

- Window: 480Ã—800 portrait, exactly 4 grays (white, light, dark, black), nearest-neighbour scaling.
- The SD card is the folder `fs_` in the repo root (override with `CROSSPOINT_SIM_SD`).
  Put EPUBs in `fs_\books\`. FOs data lives in `fs_\fos\`.
- Mouse = touch (click = tap, drag = swipe). Keyboard = physical buttons:

| Key | Sticky button / action |
| --- | --- |
| `â†‘` / `â†“` | Up / Down side buttons (scroll, page turn) |
| `Enter` | OK (the shared OK/power button, short press) |
| `P` | Power: hold to sleep (on the device OK and Power are one button) |
| `S` | Simulate sleep immediately |
| `Esc` | Back gesture (same as a right-swipe from the left edge) |
| `H` | Home gesture (same as an up-swipe from the bottom edge) |

`â†`/`â†’` do nothing: the Sticky has no Left/Right buttons. On the sleep screen any mapped
key wakes the device (the simulator relaunches itself as a child process).

### Headless test mode

`tools/simtest/run.py` drives `program.exe` through the simulator's scripting hooks:
`CROSSPOINT_SIM_INPUT_SCRIPT` (`<ms>:<action>;...`, actions `TAP:x,y`, `SWIPE:x1,y1,x2,y2`,
`UP`, `DOWN`, `ENTER`, `BACK`, `HOME`, `POWER`, `QUIT`) and `CROSSPOINT_SIM_SCREENSHOTS`
(`<ms>:<path>.png;...`). Scenarios are JSON files in `tools/simtest/scenarios/`; each run
uses a fresh copy of `tools/simtest/sd/` as the SD card, plus the overlays named in `sd`
(`{{date+N}}` / `{{unix-N}}` in overlay JSON are replaced with run-relative values).
`"backend": 8799` runs the mock backend for the scenario (overlay `backend-8799`;
`backend-down` points at a closed port). `after_wake` steps run in the process a
deep-sleep wake relaunches; `FOS_SIM_WAKE_TIMER_MS` makes the lock screen's timed wake
fire, and `FOS_SIM_CLIMATE="21.4,47"` (or `none`) sets the room sensor. **After any UI change, run the
relevant scenarios and look at the PNGs before committing.**

## Architecture (CrossPoint core)

- **Boot/loop**: `src/main.cpp`. `setup()` brings up HAL, fonts, settings, then
  `activityManager.goHome()`/`goToReader()`. `loop()` updates input, handles sleep and
  global gestures, then `activityManager.loop()`.
- **Activities** (`src/activities/Activity.h`, `ActivityManager.h`): one screen each,
  Android-like stack (`pushActivity`, `startActivityForResult`, `finish()`), lifecycle
  `onEnter/loop/render/onExit`. `loop()` runs on the main task; `render(RenderLock&&)`
  runs on a separate FreeRTOS render task. Shared state is guarded by `RenderLock`.
  Call `requestUpdate()` to repaint.
- **Rendering**: `GfxRenderer` draws into a 1-bit panel framebuffer (48 KB, 800Ã—480
  physical, rotated to portrait). Grays use a second and third pass into LSB/MSB planes.
  The Sticky supports **absolute** 4-tone planes (black (0,0), dark (1,0), light (0,1),
  white (1,1)) with one combined refresh. See `freeink-sdk/docs/grayscale-capabilities.md`.
- **Input**: `MappedInputManager` (`src/MappedInputManager.h`) maps HAL buttons and touch
  (taps, swipes, holds) to logical `Button`s. The Sticky has three buttons: Up (GPIO5),
  Down (GPIO6), OK/Power (GPIO4: click = Confirm, hold = sleep). Back/Home are touch
  gestures (left-edge right-swipe, bottom-edge up-swipe).
- **Storage**: `Storage` (`lib/hal/HalStorage.h`) over SdFat. Always use `HalFile`; never
  raw SdFat. Settings and caches live under `/.crosspoint/` (CrossPoint) and `/fos/` (FOs).
- **Fonts**: built-in bitmap fonts in `lib/EpdFont/builtinFonts/`, generated by
  `lib/EpdFont/scripts/fontconvert.py`. FOs fonts are generated by
  `tools/fonts/build_fos_fonts.py` into `src/fos/ui/generated/`.
- **HAL**: `lib/hal/Hal*.h` wraps the SDK (display, GPIO, storage, power, clock, tilt).
  The simulator replaces the entire `lib/hal` with `sim/crosspoint-simulator/src/Hal*.cpp`;
  a new HAL method needs a stub there too.
- **Logging**: `LOG_INF/LOG_DBG/LOG_ERR("TAG", fmt, ...)` from `Logging.h`, never `Serial`.

## Architecture (FOs layer)

- **Design system** (`src/fos/ui/`): `fos::ui::Canvas` wraps `GfxRenderer` with four
  `Tone`s. A screen's `draw()` runs once per plane (BW, then LSB and MSB for absolute
  grays); every primitive maps its tone to the right bit for the current plane, so fills
  are flat 4-level grays with no dithering. Text is drawn without anti-aliasing (crisp,
  knocked out of the gray planes). Components: status bar, header, buttons (3 px outline,
  4 px hard shadow, pressed = no shadow + 4 px shift), cards, list rows, toggles,
  segmented control, chips, tabs, on-screen keyboard, numeric keypad.
- **Screens** (`src/fos/ui/FosActivity.h`): FOs screens subclass `FosActivity`. They
  implement `draw(Canvas&, body)` (pure: may be called 3Ã— per frame) and
  `onAction(action, value)`. Hit areas are registered while drawing and routed from touch
  in `loop()`, which runs under the render lock. Physical buttons arrive as
  `onUp/onDown/onOk/onBack`. Text entry: push `ui::TextEntryActivity` with
  `startActivityForResult` and read the `KeyboardResult`.
- **Apps** (`src/fos/apps/`): `AppId` order is persisted (append only). `openApp()` in
  `AppLauncher.cpp` maps each id to its activity.
- **Launcher** (`src/fos/launcher/`): app grid from `FosSettings` (visible apps, order,
  icon size S 3Ã—5 / M 2Ã—4 / L 1Ã—4, labels, pages). Replaces CrossPoint's Home.
- **Settings** (`src/fos/settings/`): `/fos/settings.json`, device-only, never synced.
  Settings can never be hidden.
- **Sync** (`src/fos/sync/`): an optional HTTP backend (address + key from Settings > Accounts),
  plus per-app fetchers with direct sources (`Direct.cpp`; Budgeteer's Google Sheet in
  `BudgetStore.cpp`, script in `tools/google-sheets/`).
  Each app caches its last copy under `/fos/<app>/`. Budgeteer keeps an offline queue.
  `sync::begin()` runs at the end of `setup()`; screens reload caches when
  `sync::generation()` changes.

## Conventions

- C++20, no exceptions, no RTTI. Follow `.clang-format` (2-space indent, 120 cols).
- Naming: `PascalCase` types, `camelCase` functions and variables, `UPPER_CASE` constants.
- FOs code lives in `namespace fos` under `src/fos/`. Keep CrossPoint files changed only
  where integration needs it, and mark those edits with a short `// FOs:` comment.
- No heap allocation in steady-state repaint paths; size containers when data loads.
- All user-visible strings are English for now (FOs strings are not in the i18n tables).
- Macro hazards: Arduino and HalStorage define `DISPLAY`, `Storage`, `HIGH`, `LOW`, `PI`, `HEX` and
  friends as macros, so avoid those names for enum values and constants.
- Persist with ArduinoJson to `/fos/<app>/*.json` through `Storage`; write to a `.tmp`
  file and `Storage.replaceFile()` for atomic saves.
- Never commit secrets: no real tokens or keys in code, tests, or `tools/simtest/sd/`.
- Before each commit: build `simulator` and `sticky`, run headless scenarios and inspect
  the PNGs, `.\bin\clang-format-fix.ps1 -g`, `pio check -e sticky`, update PROGRESS.md.
