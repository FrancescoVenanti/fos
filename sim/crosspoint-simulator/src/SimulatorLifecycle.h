#pragma once

namespace SimulatorLifecycle {

enum class WakeReason { None, PowerButton, Timer };

void initProcessArgs(char** argv);
WakeReason consumeWakeReason();
[[noreturn]] void rebootAsPowerWake();

// FOs: timed wake from deep sleep. Each wake relaunches the simulator as a
// child process, so the timer only runs when FOS_SIM_WAKE_TIMER_MS is set
// (tests); it then replaces the requested interval.
void armWakeTimer(unsigned long seconds);
bool wakeTimerDue();
[[noreturn]] void rebootAsTimerWake();

}  // namespace SimulatorLifecycle
