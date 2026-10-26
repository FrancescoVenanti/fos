#include "SimulatorLifecycle.h"
#include "HalGPIO.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#ifdef _WIN32
#include <SDL.h>
#include <process.h>

#include <string>
#include <vector>
#endif

namespace {

constexpr const char *kWakeReasonEnv = "CROSSPOINT_SIM_WAKE_REASON";
constexpr const char *kInputScriptEnv = "CROSSPOINT_SIM_INPUT_SCRIPT";
constexpr const char *kInputScriptAfterWakeEnv =
    "CROSSPOINT_SIM_INPUT_SCRIPT_AFTER_WAKE";
constexpr const char *kScreenshotsEnv = "CROSSPOINT_SIM_SCREENSHOTS";
constexpr const char *kScreenshotsAfterWakeEnv =
    "CROSSPOINT_SIM_SCREENSHOTS_AFTER_WAKE";
char **gArgv = nullptr;

void promoteAfterWakeValue(const char *target, const char *afterWake) {
  const char *value = std::getenv(afterWake);
  if (value) {
    setenv(target, value, 1);
  } else {
    unsetenv(target);
  }
  unsetenv(afterWake);
}

} // namespace

namespace SimulatorLifecycle {

void initProcessArgs(char **argv) { gArgv = argv; }

WakeReason consumeWakeReason() {
  const char *value = std::getenv(kWakeReasonEnv);
  if (!value) {
    return WakeReason::None;
  }

  unsetenv(kWakeReasonEnv);
  if (std::strcmp(value, "power") == 0) {
    return WakeReason::PowerButton;
  }
  if (std::strcmp(value, "timer") == 0) {
    return WakeReason::Timer;
  }
  return WakeReason::None;
}

namespace {
// FOs: deadline of the timed wake.
std::chrono::steady_clock::time_point wakeTimerDeadline{};
bool wakeTimerArmed = false;

[[noreturn]] void reboot(const char *reason);
} // namespace

void armWakeTimer(const unsigned long seconds) {
  const char *override = std::getenv("FOS_SIM_WAKE_TIMER_MS");
  wakeTimerArmed = seconds > 0 && override && *override;
  if (wakeTimerArmed)
    wakeTimerDeadline =
        std::chrono::steady_clock::now() +
        std::chrono::milliseconds(std::strtoul(override, nullptr, 10));
}

bool wakeTimerDue() {
  return wakeTimerArmed &&
         std::chrono::steady_clock::now() >= wakeTimerDeadline;
}

[[noreturn]] void rebootAsTimerWake() { reboot("timer"); }

[[noreturn]] void rebootAsPowerWake() { reboot("power"); }

namespace {
[[noreturn]] void reboot(const char *reason) {
  if (!gArgv || !gArgv[0]) {
    std::fputs("SimulatorLifecycle: missing argv for reboot\n", stderr);
    _exit(1);
  }

  setenv(kWakeReasonEnv, reason, 1);
  // Retain the physical switch level when the host relaunches after sleep.
  if (gpio.hasToggleSwitch())
    setenv("CROSSPOINT_SIM_SLIDE_SWITCH",
           gpio.isToggleSwitchOn() ? "on" : "off", 1);
  // A deep-sleep wake is a fresh process. Do not replay the pre-sleep script,
  // which would otherwise put every relaunched process back to sleep forever.
  // Tests can provide an explicit post-wake schedule when they need to capture
  // or terminate the relaunched instance.
  promoteAfterWakeValue(kInputScriptEnv, kInputScriptAfterWakeEnv);
  promoteAfterWakeValue(kScreenshotsEnv, kScreenshotsAfterWakeEnv);
#ifdef _WIN32
  // FOs: Windows has no exec that replaces the process. Close this window,
  // run the woken instance as a child and pass its exit code through, so a
  // caller waiting on the simulator (headless tests) sees the whole session.
  SDL_Quit();
  std::vector<std::string> quoted;
  for (char **arg = gArgv; *arg; ++arg) quoted.push_back(std::string("\"") + *arg + "\"");
  std::vector<const char *> args;
  for (const auto &arg : quoted) args.push_back(arg.c_str());
  args.push_back(nullptr);
  const intptr_t code = _spawnv(_P_WAIT, gArgv[0], args.data());
  if (code >= 0) _exit(static_cast<int>(code));
  std::perror("_spawnv");
#else
  execvp(gArgv[0], gArgv);

  std::perror("execvp");
#endif
  _exit(1);
}
} // namespace

} // namespace SimulatorLifecycle
