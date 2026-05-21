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

#if !defined(Q_OS_MACOS) && !defined(Q_OS_WIN)
#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QString>
#include <QStringList>
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
    // Linux (and other Unixes exposing the sysfs power_supply class).
    // Walk /sys/class/power_supply/* and find the "Mains"-type supply
    // (the AC adapter). Its `online` file reads 1 when wall power is
    // connected and 0 when not. Laptops, desktops, VMs and containers
    // all present differently: many have no power_supply class at all,
    // in which case we report OnAC so the speculative queue runs by
    // default (a desktop is effectively always on mains). Only when we
    // positively observe an offline AC adapter do we report OnBattery.
    auto readSysfsLine = [](const QString &path) -> QString {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return {};
        }
        return QString::fromLatin1(f.readLine()).trimmed();
    };

    QDir psDir(QStringLiteral("/sys/class/power_supply"));
    if (!psDir.exists()) {
        return PowerState::OnAC;
    }
    const QStringList supplies =
        psDir.entryList(QDir::NoDotAndDotDot | QDir::AllEntries | QDir::System);
    bool sawOfflineMains = false;
    for (const QString &name : supplies) {
        const QString base = psDir.absoluteFilePath(name);
        const QString type = readSysfsLine(base + QStringLiteral("/type"));
        if (type.compare(QStringLiteral("Mains"), Qt::CaseInsensitive) != 0) {
            continue; // batteries, USB supplies, UPS, etc.
        }
        const QString online = readSysfsLine(base + QStringLiteral("/online"));
        if (online == QLatin1String("1")) {
            return PowerState::OnAC; // any online adapter wins.
        }
        if (online == QLatin1String("0")) {
            sawOfflineMains = true;
        }
        // An unreadable/empty `online` is NOT treated as battery evidence;
        // keep scanning — another adapter (e.g. a dock) might be online.
    }
    // Only an explicitly-offline AC adapter means OnBattery. With no AC
    // line in sysfs at all (desktop, VM, container), or only adapters whose
    // state we couldn't read, default to OnAC so the queue keeps running.
    return sawOfflineMains ? PowerState::OnBattery : PowerState::OnAC;
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
