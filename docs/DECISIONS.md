# FOs design decisions

Choices the spec does not settle, with the reason for each. Newest last.

## D1 — Fork layout and branches
FOs work lives on `main` (the fork's default branch). `develop` stays an untouched
mirror of CrossPoint's `develop` so upstream fixes can be merged later. The CrossPoint
README moved to `docs/CROSSPOINT_README.md`; `LICENSE` keeps CrossPoint's copyright
line and adds one for the FOs changes.

## D2 — Upstream agent policy
CrossPoint's `AGENTS.md` forbids autonomous agents from committing or pushing; it is a
contribution policy for the upstream project. On this fork the owner has authorized
commits and pushes after each phase, so that rule does not apply. The technical rules
in `.agents/rules/` (HAL use, heap discipline, rendering contracts) still do.

## D3 — Simulator: vendor and port crosspoint-simulator
The simulator is [uxjulia/crosspoint-simulator](https://github.com/uxjulia/crosspoint-simulator)
(MIT, the one CrossInk uses), vendored into `sim/crosspoint-simulator/` at the commit in
`UPSTREAM_COMMIT` and ported to native Windows (MSYS2 UCRT64 gcc + SDL2):
- Windows-only code sits behind `_WIN32`, and nothing Windows-specific is in headers
  that firmware code includes (`windows.h` defines macros such as `IGNORE` that collide
  with firmware enums). Win32 calls are in `HostWin.cpp`; sockets in `HostSocket.h`,
  included only by the socket `.cpp` files.
- Hashing uses Windows CNG (`bcrypt`) instead of OpenSSL/CommonCrypto.
- HTTP still shells out to `curl` (bundled with Windows 10+), with cmd.exe quoting and the
  request body passed in a temp file.
- Files are opened with `O_BINARY`; renames that replace a file use `MoveFileEx`, as the
  firmware relies on POSIX atomic replace.
- Sleep/wake relaunch uses `_spawnv(_P_WAIT)` instead of `execvp`, so a test runner
  waiting on the process sees the whole session.
Vendoring (rather than a lib_dep URL) is needed because the port changes the library and
FOs adds HAL methods the upstream simulator does not have.

## D4 — FOs design system drawn directly with GfxRenderer
CrossPoint builds screens with FreeInkUI. FOs instead has a small design system of its
own (`src/fos/ui/`) drawn directly with `GfxRenderer`:
- FreeInkUI has no hard-shadow or pressed-shift primitives, and its gray fills are Bayer
  dithers in the B/W target; the spec asks for flat 4-level fills and no dithering.
- The Sticky's SSD1677 supports **absolute** 4-tone planes with a single activation.
  `fos::ui::Canvas` renders a screen once per plane and maps each `Tone` to the right
  bit, so every fill is an exact gray. Text is drawn 1-bit and knocked out of the gray
  planes, so black text on a gray card stays black.
- FOs screens are still CrossPoint `Activity`s, so the activity stack, render task,
  sleep and reader integrate unchanged.

## D5 — Refresh policy
A frame that uses no gray goes out as a fast B/W partial refresh. A frame with gray uses
the absolute 4-gray pass. Every N updates (Settings > Display > full-refresh interval) a
half refresh cleans ghosting. To be tuned on hardware.

## D6 — Physical buttons
The Sticky has three buttons: Up, Down and the shared OK/Power button (click = OK, hold =
sleep, handled by the SDK). Default mapping on FOs screens:
- **Up / Down**: scroll or page; in lists they move a page at a time.
- **OK** (short press): Back, one level up. On the launcher it goes to the next page.
- **OK** (double press): Home.
- **OK** (hold): sleep.
Touch adds the CrossPoint global gestures: right-swipe from the left edge = Back,
up-swipe from the bottom edge = Home. CrossPoint screens reused unchanged (the reader
and library) keep CrossPoint's own button meaning. Settings > Buttons can swap Up/Down
and change what OK does.

## D7 — Storage layout on the microSD
FOs data lives under `/fos/`: `/fos/settings.json` plus one folder per app
(`/fos/budgeteer/`, `/fos/todo/`, ...). Each holds the app's data and the last synced
copy. Books stay in `/books/` and CrossPoint caches in `/.crosspoint/`. All JSON writes
go to a `.tmp` file first, then `Storage.replaceFile()`.

## D8 — Sticky firmware uses the prebuilt Arduino core
Upstream's `sticky` env extends `firmware_tuned`, which rebuilds the Arduino core with a
custom sdkconfig to reclaim ~32 KB of heap. On this Windows toolchain that step fails:
pioarduino installs SCons 4.11 over the 4.8 the rebuild needs
(`No module named 'SCons.Tool.FortranCommon'`). FOs builds `sticky` on the prebuilt
`dio_opi` core instead, like CrossPoint's X4 Pro and Paper Mono profiles. With 8 MB of
PSRAM the lost heap does not matter, builds are faster, and BLE is unused by FOs.

## D9 — LF line endings for patch files
`.gitattributes` forces LF for `*.patch` and `*.sh`. With Git for Windows'
`core.autocrlf=true`, the SdFat/JPEGDEC patches were checked out with CRLF and
`git apply` rejected them, breaking every device build.

## D10 — Simulator test mode
Headless tests reuse the simulator's scripting hooks (`CROSSPOINT_SIM_INPUT_SCRIPT`,
`CROSSPOINT_SIM_SCREENSHOTS`). FOs adds: PNG screenshots written from the composited
panel pixels (exact 480x800, only the panel's 4 gray values, independent of window scale),
`FOS_SIM_HEADLESS=1` for a hidden window, `FOS_SIM_SCALE` for a 1-4x window, the Sticky's
real button set (Up, Down, OK/Power), and Esc/H as the back/home edge gestures. Scenarios
are JSON files run by `tools/simtest/run.py`, each against a fresh copy of
`tools/simtest/sd/`.
