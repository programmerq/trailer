#pragma once

namespace trailer {

// Platform-aware "is the user on wall power right now?" probe. Used
// by MlScheduler to decide whether to run speculative (Prefetch /
// Idle) ML work. On battery + run_on_battery=false, those priorities
// are short-circuited; user-driven (UserAction / VisiblePage) work
// always runs regardless of power state.
enum class PowerState {
    OnAC,      // External power, charging or charged.
    OnBattery, // Running off internal battery.
    Unknown,   // We could not determine the state (e.g. desktop PC
               // with no battery, or a platform we have not wired up).
};

class PowerSource {
  public:
    // Cheap synchronous probe of the system's current power state.
    // Implemented per-platform in PowerSource.cpp; the IOKit /
    // GetSystemPowerStatus calls are not free but are well under a
    // millisecond on every system we target. The scheduler polls
    // this on a 30 s cadence rather than per-submit, so the cost is
    // dominated by submit-time checks (which also call this).
    //
    // Linux currently stubs to OnAC — most desktops are mains-powered
    // and the scheduler's battery policy is permissive on OnAC, so
    // this defaults to "run all priorities" until a real reader lands.
    // A follow-up that reads /sys/class/power_supply/*/online (and
    // falls back to OnAC when the path doesn't exist on a desktop) is
    // tracked in TODO.md.
    static PowerState currentState();

    // Test seam: replace the production probe with a fixed value or
    // a lambda. Clears (returns to production behaviour) when
    // `override` is the default-constructed std::function (operator
    // bool() returns false). Not thread-safe with respect to
    // currentState() — only flip from test setUp/tearDown.
    using Probe = PowerState (*)();
    static void setProbeForTesting(Probe probe);
    static void clearProbeForTesting() { setProbeForTesting(nullptr); }
};

} // namespace trailer
