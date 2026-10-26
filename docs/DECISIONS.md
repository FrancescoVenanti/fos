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

## D11 — FOs fonts and icons
UI type is Space Grotesk (Medium/Bold) with Space Mono for the terminal, both OFL, a
bold grotesque that fits the neo-brutalist look. They are converted with CrossPoint's
`fontconvert.py` (2-bit, compressed; ~120 KB) by `tools/fonts/build_fos_fonts.py`. The
44 pt "jumbo" font carries only digits and money/time symbols (`--only-intervals`, a
small FOs option added to `fontconvert.py`). App icons are Lucide (ISC) rasterized at
36/48/64 px with thicker strokes by `tools/icons/build_fos_icons.py` (resvg instead of
the SDK's librsvg dependency). Generated files live in `src/fos/ui/generated/` and
are committed, so building needs no Python font tools.

## D12 — Text is drawn without anti-aliasing
FOs text is solid ink in every plane: 2-bit glyphs are drawn as B/W, so on a gray card
black text stays black and white text stays white. Glyph edges are slightly heavier
than CrossPoint's anti-aliased reader text, which suits the heavy type of the design.

## D13 — Saved Wi-Fi networks stay in CrossPoint's store
Settings > Wi-Fi and sync lists CrossPoint's `WifiCredentialStore` (passwords
obfuscated with a device key) instead of keeping networks in `/fos/settings.json`. The
list order is the priority (`moveCredential` added); "Add network" opens CrossPoint's
scan-and-join screen.

## D14 — What "clear caches" and "factory reset" erase
Clear caches removes each app's synced copy (`/fos/<app>/cache*.json` and the read-later
articles) and keeps user data and the Budgeteer queue. Factory reset erases `/fos`
entirely (settings and all FOs data); books and CrossPoint's reader caches stay.

## D15 — Launcher and Home
`ActivityManager::goHome()` now shows the FOs launcher; CrossPoint's HomeActivity is
unused. Apps open on top of the launcher (`pushActivity`), so Back returns to it.
On the launcher, Up/Down and horizontal swipes turn pages and OK goes to the next page.

## D16 — Sync runs on its own task and owns its state
`fos::sync` runs on a FreeRTOS worker (12 KB stack) so screens never block on the
network. Each run first sends queued data (apps register a pusher), then downloads
into `/fos/<app>/cache*.json`. The last-sync time lives in `/fos/sync/state.json`, not
in `/fos/settings.json`, so the worker never writes the settings object that screens
edit on the main task. If a run had to turn Wi-Fi on, it turns it off again when the
queue is empty; if Wi-Fi was already on (web server, a live app) it is left on.

## D17 — Budgeteer saves through the queue
Every new entry goes into `/fos/budgeteer/queue.json` first, also when online; saving
online then asks the sync worker to push at once. One path means one place where an
entry can leave the queue: only after the backend confirms its `clientId` (201, or 200
with `duplicate`). Transport errors and 5xx stop the push and keep everything; a 4xx is
logged and the entry stays, so nothing typed is lost. The queue is guarded by a mutex
because the main task appends while the worker drains it. "Online" for the home screen
means the run the app triggered on open reached the backend; offline it shows only the
queue counter and "+ New transaction", as the spec asks. Description chips come from
`/fos/budgeteer/recent.json` (typed on the device) and then the downloaded month.

## D18 — Test scenarios can run the mock backend
A scenario with `"backend": <port>` starts `tools/mock-backend/server.py` on that port
for the run and saves its log as `backend.log`, so online flows are tested end to end.
Scenarios point the device at the port with an SD overlay (`sd` takes a list of
overlays). Online tests use 8799 (`backend-8799` overlay) and offline tests a closed
port, 8798 (`backend-down`), so a mock backend left running on the default 8787 cannot
change their outcome.

## D19 — To-do data and recurrence
To-do keeps everything in one file, `/fos/todo/todo.json` (lists, tasks, next id), never
synced. A first run creates one "Inbox" list; there are at most 6 lists so the chips fit
in two rows. Completing a recurring task does not mark it done: its due date moves to the
first occurrence after today (monthly keeps the day of month, clamped to short months),
so a missed daily or weekly task does not pile up. A one-off task is marked done, stays
greyed at the bottom of its list until "Clear done", and can be reopened by tapping its
box. New tasks are typed first and get a date or repeat in the editor, which saves only
on Save (Back discards).

## D20 — Calendar agenda
The agenda always shows today and the next 6 days from the device clock, not from the
copy's `from` date, so an old copy still lines up with the right days (past days drop
off). Every day has a header; a day without events says "Nothing planned". Today's
finished events are grey. The event that matters now is inverted with a NEXT tag (NOW
while it runs): the first timed event today that has not ended, else the first event
of a later day, else today's all-day event. An event that ends after midnight shows its
end as "06:10 +1" and counts as running until the end of its first day. The status line
says when the copy was downloaded, and "Offline" when the sync on opening failed.
`calendar::nextEvent()` is shared so the lock screen can show the same next event.

## D21 — Test overlays with relative dates
Synced copies on test SD cards must look current on any day the tests run. In overlay
`.json` files the runner replaces `{{date±N}}` with an ISO date relative to the day of
the run and `{{unix±N}}` with a Unix time. `backend-8799` and `backend-down` are the
shared overlays that point the device at the test backend or at a closed port.

## D22 — The lock screen is the sleep screen
The Lock screen is both an app and the sleep frame (CrossPoint's default sleep screen
mode draws it; custom images, covers and Quick Resume still work as before). Before
deep sleep, `HalPowerManager::setWakeTimer()` arms a timer wake for the next refresh
boundary (every Settings > Lock screen interval, aligned to the clock, so a 15-minute
interval updates at :00, :15, :30, :45; battery saver makes it hourly). A timed wake
(`WakeupReason::Timer`) only reads the clock, sensor and cached data, redraws the frame
with a fast refresh (a cleaning refresh every Settings > Display interval, counted in
RTC memory) and sleeps again: no UI, no Wi-Fi, no Bluetooth. The power button wakes the
device as before. The sleep frame is B/W only, because fast refreshes over a retained
image cannot keep gray planes; the app version can use grays and shows seconds when
the clock style asks for them. Weather is the synced copy (its age is shown after 3
hours); room temperature and humidity come from the Sticky's SHT40 through a new
`HalClimate`. Units follow Settings > Region (Celsius or Fahrenheit, decimal comma).

## D23 — Simulator: timed wake and climate
The simulator's deep sleep fires the timed wake only when `FOS_SIM_WAKE_TIMER_MS` is set
(tests), relaunching itself with wake reason "timer", like the button wake. The sleep
loop now presents the panel, so screenshots can be taken of the sleep frame. Scenarios
add `after_wake` steps for the relaunched process. `FOS_SIM_CLIMATE="<C>,<RH>"` sets the
simulated sensor, `none` removes it.

## D24 — Timers run on the wall clock
Timers store absolute times (epoch milliseconds) in `/fos/timers/state.json`: a running
countdown or pomodoro phase stores when it ends, a stopwatch when it started. Nothing
ticks in the background, so they keep running through deep sleep and restarts (the
ESP32 keeps the system clock in deep sleep; the Sticky's RTC covers power-off). A clock
change (NTP sync) shifts running timers by the same amount; acceptable for minutes-long
timers. The Sticky has no buzzer, so an ended timer is shown, not heard: "TIME'S UP"
in the Timers app, a "TIME'S UP" tag in the status bar of every FOs screen (polled every
250 ms), and a double-framed block on the lock screen, until it is dismissed. While the
device sleeps, the lock screen's timed wake is set to the earlier of its refresh boundary
and the end of a running timer, so the end appears on screen when it happens. A pomodoro
phase that ends waits for a tap to start the next one ("Later" moves on without
starting), so a break never starts unseen. All three timers can run at once; the
running countdown or pomodoro (whichever ends first), or else a running stopwatch, takes
the next event's place on the lock screen.

## D25 — Reader is CrossPoint's reader behind an FOs front page
The Reader app is a small FOs screen (Continue reading, recent books, Library, Files)
over CrossPoint's reader, library and file browser, which are used unchanged: they keep
their own look, buttons and settings (fonts, margins, bookmarks, dictionary, KOReader
sync). Opening a book replaces the activity stack, so leaving the reader (Home gesture,
or its menu) goes to the FOs launcher. Test books are generated by
`tools/simtest/fixtures/make_epub.py` (original text).

## D26 — Chess engine and pieces
FOs has its own small engine (`src/fos/apps/chess/`) instead of Stockfish: legal move
generation (checked with perft on five standard positions), iterative-deepening
alpha-beta with quiescence and piece-square tables. Five levels set depth, time (0.3 to
4 s) and random noise on root moves, so low levels make human-like mistakes. It runs on
the main loop task: its move buffer is one heap allocation, not per-ply stack arrays.
Pieces are original geometric silhouettes (`tools/icons/build_chess_pieces.py`) drawn as
a white cut-out plus black ink, so both colors read on white and light-gray squares.
Board squares are 54 px (432 px board) with coordinates outside it.

## D27 — Lichess
The device talks to Lichess directly with the personal token: `GET
/api/account/playing` (correspondence, classical and rapid only; faster games are
counted, not shown), `GET /api/puzzle/daily` (replayed from its PGN to the puzzle
position) and `POST /api/board/game/<id>/move/<uci>`. Moves are sent when played; a
failed send takes the move back. Settings > Accounts has a "Lichess server" field so
tests point at `tools/mock-backend` (`/lichess`). Live opponent moves are not streamed:
the game list reloads when you return to it.

