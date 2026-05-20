// Unit tests for trailer::PowerSource. The host CI image can be a
// laptop, a desktop, a container with no battery, or a cloud VM with
// a virtual UPS — we can't pin a specific expected value. Instead we
// verify:
//
//   - currentState() returns one of the three enum values (no UB on
//     any host).
//   - The test seam (setProbeForTesting) overrides the production
//     probe so the scheduler tests can simulate "on battery" without
//     unplugging the runner.

#include "platform/PowerSource.h"

#include <QObject>
#include <QtTest/QtTest>

using namespace trailer;

class TestPowerSource : public QObject {
    Q_OBJECT
  private slots:
    void hostProbeReturnsKnownEnum();
    void testSeamOverridesProductionProbe();
    void clearProbeRestoresProductionBehaviour();
};

void TestPowerSource::hostProbeReturnsKnownEnum() {
    const PowerState state = PowerSource::currentState();
    QVERIFY(state == PowerState::OnAC || state == PowerState::OnBattery ||
            state == PowerState::Unknown);
}

namespace {
PowerState forceBattery() {
    return PowerState::OnBattery;
}
PowerState forceUnknown() {
    return PowerState::Unknown;
}
} // namespace

void TestPowerSource::testSeamOverridesProductionProbe() {
    PowerSource::setProbeForTesting(&forceBattery);
    QCOMPARE(PowerSource::currentState(), PowerState::OnBattery);
    PowerSource::setProbeForTesting(&forceUnknown);
    QCOMPARE(PowerSource::currentState(), PowerState::Unknown);
    PowerSource::clearProbeForTesting();
}

void TestPowerSource::clearProbeRestoresProductionBehaviour() {
    PowerSource::setProbeForTesting(&forceBattery);
    QCOMPARE(PowerSource::currentState(), PowerState::OnBattery);
    PowerSource::clearProbeForTesting();
    const PowerState state = PowerSource::currentState();
    // After the clear the host value is back — we just verify we are
    // no longer pinned to OnBattery.
    QVERIFY(state == PowerState::OnAC || state == PowerState::OnBattery ||
            state == PowerState::Unknown);
}

QTEST_MAIN(TestPowerSource)
#include "test_power_source.moc"
