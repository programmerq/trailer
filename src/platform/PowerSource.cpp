#include "PowerSource.h"

#include <QtGlobal>

#include <atomic>

#if defined(Q_OS_MACOS)
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/ps/IOPSKeys.h>
#include <IOKit/ps/IOPowerSources.h>
#endif

#if defined(Q_OS_WIN)
#include <windows.h>
#endif

namespace trailer {

namespace {

// Test seam — set via setProbeForTesting() to inject a synthetic
// state for unit tests. An atomic so we can read it from worker
// threads without a mutex; the assumption is that tests flip the
// probe from one thread (the test driver) before submitting work.
std::atomic<PowerSource::Probe> g_probeOverride{nullptr};

PowerState detectFromOs() {
#if defined(Q_OS_MACOS)
    // IOPSGetProvidingPowerSourceType returns one of the constants
    // defined in IOPSKeys.h: kIOPMACPowerKey, kIOPMBatteryPowerKey,
    // or kIOPMUPSPowerKey. We treat UPS as "AC" — a UPS is by
    // definition external power and the speculative-prefetch
    // policy that consumes this enum cares about "is the user
    // currently drawing from a finite reservoir?".
    CFTypeRef snapshot = IOPSCopyPowerSourcesInfo();
    if (snapshot == nullptr) {
        return PowerState::Unknown;
    }
    CFStringRef provider = IOPSGetProvidingPowerSourceType(snapshot);
    PowerState state = PowerState::Unknown;
    if (provider != nullptr) {
        if (CFStringCompare(provider, CFSTR(kIOPMBatteryPowerKey), 0) == kCFCompareEqualTo) {
            state = PowerState::OnBattery;
        } else if (CFStringCompare(provider, CFSTR(kIOPMACPowerKey), 0) == kCFCompareEqualTo) {
            state = PowerState::OnAC;
        } else if (CFStringCompare(provider, CFSTR(kIOPMUPSPowerKey), 0) == kCFCompareEqualTo) {
            state = PowerState::OnAC;
        }
    }
    CFRelease(snapshot);
    return state;
#elif defined(Q_OS_WIN)
    SYSTEM_POWER_STATUS status{};
    if (GetSystemPowerStatus(&status) == 0) {
        return PowerState::Unknown;
    }
    // ACLineStatus: 0 = offline (on battery), 1 = online (AC),
    // 255 = unknown. BatteryFlag bit 0x80 indicates "no system
    // battery" which we collapse to OnAC since a desktop has no
    // battery to protect.
    if (status.ACLineStatus == 1) {
        return PowerState::OnAC;
    }
    if (status.ACLineStatus == 0) {
        if ((status.BatteryFlag & 0x80) != 0) {
            return PowerState::OnAC;
        }
        return PowerState::OnBattery;
    }
    return PowerState::Unknown;
#else
    // Linux (and any future platforms). Reading
    // /sys/class/power_supply/AC*/online (or /BAT*/status) gives a
    // direct answer but requires walking the directory and
    // tolerating absence — laptops, desktops, and containers all
    // present differently. Punt: report OnAC so the speculative
    // queue runs by default. A separate follow-up can wire the
    // sysfs scan if a Linux user surfaces a battery-life complaint.
    return PowerState::OnAC;
#endif
}

} // namespace

PowerState PowerSource::currentState() {
    if (auto *probe = g_probeOverride.load(std::memory_order_acquire); probe != nullptr) {
        return probe();
    }
    return detectFromOs();
}

void PowerSource::setProbeForTesting(Probe probe) {
    g_probeOverride.store(probe, std::memory_order_release);
}

} // namespace trailer
