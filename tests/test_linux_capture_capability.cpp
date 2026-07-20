// Unit test — pure Linux/Wayland capture-capability policy.
//
// Covers the whole surface of src/platform/LinuxCaptureCapability: the
// platformName x underWaylandSession x portalUsable truth table, the
// X11-parity guarantee (genuine xcb / offscreen with underWaylandSession=false
// always select the unchanged grabWindow path), the XWayland degrade
// (platformName "xcb" but underWaylandSession=true -> WaylandNoCapture), and
// the G3 content of the shared honest message. No display, no Mac — pure
// decision only.

#include "platform/LinuxCaptureCapability.h"

#include <QtTest/QtTest>

using namespace trailer;

class TestLinuxCaptureCapability : public QObject {
    Q_OBJECT
  private slots:
    void truthTable();
    void x11ParityUnchangedByPortalFlag();
    void xwaylandDegradesEvenWhenPlatformIsXcb();
    void emptyPlatformNameFollowsSessionFlag();
    void unavailableMessageIsHonest();
};

// The full platformName x underWaylandSession x portalUsable table. The native
// wayland plugin and (via underWaylandSession) XWayland both route to
// WaylandPortal when portalUsable and WaylandNoCapture otherwise; genuine X11
// (underWaylandSession=false, non-wayland platformName) is X11Grab regardless
// of portalUsable.
void TestLinuxCaptureCapability::truthTable() {
    // Native Wayland plugin, portal usable -> reserved portal path (either
    // session-flag value, since the platformName clause wins first).
    for (bool session : {true, false}) {
        QCOMPARE(linuxCaptureCapability(QStringLiteral("wayland"), session, /*portalUsable=*/true),
                 LinuxCaptureCapability::WaylandPortal);
        QCOMPARE(
            linuxCaptureCapability(QStringLiteral("wayland-egl"), session, /*portalUsable=*/true),
            LinuxCaptureCapability::WaylandPortal);

        // Native Wayland plugin, no usable portal -> honest no-capture degrade.
        QCOMPARE(linuxCaptureCapability(QStringLiteral("wayland"), session, /*portalUsable=*/false),
                 LinuxCaptureCapability::WaylandNoCapture);
        QCOMPARE(
            linuxCaptureCapability(QStringLiteral("wayland-egl"), session, /*portalUsable=*/false),
            LinuxCaptureCapability::WaylandNoCapture);
    }

    // Non-wayland platformName + NOT under a Wayland session -> the unchanged
    // grabWindow path either way (genuine X11, offscreen/CI).
    for (bool portal : {true, false}) {
        QCOMPARE(linuxCaptureCapability(QStringLiteral("xcb"), /*underWaylandSession=*/false, portal),
                 LinuxCaptureCapability::X11Grab);
        QCOMPARE(
            linuxCaptureCapability(QStringLiteral("offscreen"), /*underWaylandSession=*/false, portal),
            LinuxCaptureCapability::X11Grab);
        QCOMPARE(
            linuxCaptureCapability(QStringLiteral("minimal"), /*underWaylandSession=*/false, portal),
            LinuxCaptureCapability::X11Grab);
    }

    // Non-wayland platformName but UNDER a Wayland session (XWayland) -> degrade
    // by the session flag: portal path when usable, else no-capture.
    QCOMPARE(linuxCaptureCapability(QStringLiteral("xcb"), /*underWaylandSession=*/true,
                                    /*portalUsable=*/true),
             LinuxCaptureCapability::WaylandPortal);
    QCOMPARE(linuxCaptureCapability(QStringLiteral("xcb"), /*underWaylandSession=*/true,
                                    /*portalUsable=*/false),
             LinuxCaptureCapability::WaylandNoCapture);
}

// The byte-identical guarantee, made deterministic: for genuine xcb and
// offscreen (underWaylandSession=false) the result is X11Grab for BOTH
// portalUsable values, so the historical QScreen::grabWindow path is selected
// unchanged and the portal flag can never perturb X11 behaviour.
void TestLinuxCaptureCapability::x11ParityUnchangedByPortalFlag() {
    QCOMPARE(linuxCaptureCapability(QStringLiteral("xcb"), /*underWaylandSession=*/false,
                                    /*portalUsable=*/true),
             LinuxCaptureCapability::X11Grab);
    QCOMPARE(linuxCaptureCapability(QStringLiteral("xcb"), /*underWaylandSession=*/false,
                                    /*portalUsable=*/false),
             LinuxCaptureCapability::X11Grab);
    QCOMPARE(linuxCaptureCapability(QStringLiteral("offscreen"), /*underWaylandSession=*/false,
                                    /*portalUsable=*/true),
             LinuxCaptureCapability::X11Grab);
    QCOMPARE(linuxCaptureCapability(QStringLiteral("offscreen"), /*underWaylandSession=*/false,
                                    /*portalUsable=*/false),
             LinuxCaptureCapability::X11Grab);
}

// The default GNOME-Wayland case: Qt loads the xcb (XWayland) plugin, so
// platformName()=="xcb", yet WAYLAND_DISPLAY is set. grabWindow(0) yields a
// black pixmap there, so we must degrade rather than save it — the blocker
// FIX 1 was raised for.
void TestLinuxCaptureCapability::xwaylandDegradesEvenWhenPlatformIsXcb() {
    QCOMPARE(linuxCaptureCapability(QStringLiteral("xcb"), /*underWaylandSession=*/true,
                                    /*portalUsable=*/false),
             LinuxCaptureCapability::WaylandNoCapture);
}

// Defensive edge: an empty platformName should not be treated as Wayland by the
// startsWith clause, so it falls through to the session flag: no session ->
// X11Grab, under a Wayland session -> WaylandNoCapture.
void TestLinuxCaptureCapability::emptyPlatformNameFollowsSessionFlag() {
    QCOMPARE(linuxCaptureCapability(QString(), /*underWaylandSession=*/false, /*portalUsable=*/false),
             LinuxCaptureCapability::X11Grab);
    QCOMPARE(linuxCaptureCapability(QString(), /*underWaylandSession=*/true, /*portalUsable=*/false),
             LinuxCaptureCapability::WaylandNoCapture);
    QCOMPARE(linuxCaptureCapability(QString(), /*underWaylandSession=*/true, /*portalUsable=*/true),
             LinuxCaptureCapability::WaylandPortal);
}

// G3 content: the shared message is non-empty, names the portal (the "why"),
// and points at X11 (the "where to go") — honest for an XWayland user too.
void TestLinuxCaptureCapability::unavailableMessageIsHonest() {
    const QString msg = waylandCaptureUnavailableMessage();
    QVERIFY(!msg.isEmpty());
    QVERIFY2(msg.contains(QStringLiteral("portal"), Qt::CaseInsensitive),
             "the degrade message must name the desktop portal (the why)");
    QVERIFY2(msg.contains(QStringLiteral("X11"), Qt::CaseInsensitive),
             "the degrade message must point at X11 (the where-to-go)");
}

QTEST_MAIN(TestLinuxCaptureCapability)
#include "test_linux_capture_capability.moc"
