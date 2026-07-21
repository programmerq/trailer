// Unit test — capture-backend policy + settings round-trip.
//
// Covers the pure, platform-agnostic surface of the ScreenCaptureKit
// picker backend: the string<->enum mapping (captureBackendFromString /
// captureBackendToString), the backend-selection rule
// (effectiveCaptureBackend), and that a chosen capture_backend persists
// through Settings save/reload. The native picker (SCContentSharingPicker /
// SCScreenshotManager) is Apple-only and not exercised here — this test
// needs no Mac.

#include "platform/PortalScreenshot.h"
#include "platform/ScreenCaptureBackend.h"
#include "settings/Settings.h"

#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace trailer;

class TestCaptureBackend : public QObject {
    Q_OBJECT
  private slots:
    void stringRoundTrip();
    void unknownStringIsSafeDefault();
    void effectivePolicy();
    void settingsPersistRoundTrip();
    void settingsUnknownStringIsSafeDefault();
    void linuxScreenshotPolicy();
    void waylandSessionMatchesPlatform();
    void waylandSessionDetectsXWaylandViaEnv();
    void waylandSessionFromSignalsTruthTable();
    void xwaylandRoutesToPortalOrUnavailable();
    void livePortalCaptureOrSkip();
#ifndef Q_OS_MACOS
    void nativeStubIsUnavailable();
#endif
};

// The canonical strings survive a to->from round-trip, and the known
// aliases all resolve to ScreenCaptureKit.
void TestCaptureBackend::stringRoundTrip() {
    QCOMPARE(captureBackendToString(CaptureBackend::Screencapture), QStringLiteral("screencapture"));
    QCOMPARE(captureBackendToString(CaptureBackend::ScreenCaptureKit),
             QStringLiteral("screencapturekit"));

    QCOMPARE(captureBackendFromString(captureBackendToString(CaptureBackend::Screencapture)),
             CaptureBackend::Screencapture);
    QCOMPARE(captureBackendFromString(captureBackendToString(CaptureBackend::ScreenCaptureKit)),
             CaptureBackend::ScreenCaptureKit);

    // Accepted aliases for the picker backend.
    QCOMPARE(captureBackendFromString(QStringLiteral("screencapturekit")),
             CaptureBackend::ScreenCaptureKit);
    QCOMPARE(captureBackendFromString(QStringLiteral("sck")), CaptureBackend::ScreenCaptureKit);
    QCOMPARE(captureBackendFromString(QStringLiteral("picker")), CaptureBackend::ScreenCaptureKit);
}

// Anything unrecognised — including the empty string and the explicit
// "screencapture" token — maps to the safe Screencapture default.
void TestCaptureBackend::unknownStringIsSafeDefault() {
    QCOMPARE(captureBackendFromString(QStringLiteral("screencapture")),
             CaptureBackend::Screencapture);
    QCOMPARE(captureBackendFromString(QString()), CaptureBackend::Screencapture);
    QCOMPARE(captureBackendFromString(QStringLiteral("")), CaptureBackend::Screencapture);
    QCOMPARE(captureBackendFromString(QStringLiteral("nonsense")), CaptureBackend::Screencapture);
    // Case-sensitive: an off-case token is unknown, not the picker.
    QCOMPARE(captureBackendFromString(QStringLiteral("ScreenCaptureKit")),
             CaptureBackend::Screencapture);
}

// The selection rule: picker only when configured AND available AND not a
// freeform region; every other combination falls back to Screencapture.
void TestCaptureBackend::effectivePolicy() {
    // Configured picker, available, not a region -> picker.
    QCOMPARE(effectiveCaptureBackend(CaptureBackend::ScreenCaptureKit, /*available=*/true,
                                     /*freeformRegion=*/false),
             CaptureBackend::ScreenCaptureKit);
    // Configured picker but unavailable -> fall back, regardless of region.
    QCOMPARE(effectiveCaptureBackend(CaptureBackend::ScreenCaptureKit, /*available=*/false,
                                     /*freeformRegion=*/false),
             CaptureBackend::Screencapture);
    QCOMPARE(effectiveCaptureBackend(CaptureBackend::ScreenCaptureKit, /*available=*/false,
                                     /*freeformRegion=*/true),
             CaptureBackend::Screencapture);
    // Configured picker, available, but a freeform region -> fall back (the
    // picker offers display/window, not drag-to-select).
    QCOMPARE(effectiveCaptureBackend(CaptureBackend::ScreenCaptureKit, /*available=*/true,
                                     /*freeformRegion=*/true),
             CaptureBackend::Screencapture);
    // Configured screencapture always stays screencapture.
    QCOMPARE(effectiveCaptureBackend(CaptureBackend::Screencapture, /*available=*/true,
                                     /*freeformRegion=*/false),
             CaptureBackend::Screencapture);
}

// A chosen backend persists: save writes it under [general].capture_backend
// and a fresh Settings loaded from the same file reads it back.
void TestCaptureBackend::settingsPersistRoundTrip() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("settings.toml");

    {
        Settings s(path);
        // Default before any change is the safe screencapture path.
        QCOMPARE(s.captureBackend(), CaptureBackend::Screencapture);
        s.setCaptureBackend(CaptureBackend::ScreenCaptureKit);
        s.save();
    }
    QVERIFY(QFile::exists(path));

    Settings reloaded(path);
    reloaded.load();
    QCOMPARE(reloaded.captureBackend(), CaptureBackend::ScreenCaptureKit);
}

// A typo in settings.toml must not silently opt a user into the unvalidated
// picker path — an unknown capture_backend loads as Screencapture.
void TestCaptureBackend::settingsUnknownStringIsSafeDefault() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("settings.toml");

    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("[general]\ncapture_backend = \"totally-bogus\"\n");
    file.close();

    Settings s(path);
    s.load();
    QCOMPARE(s.captureBackend(), CaptureBackend::Screencapture);
}

// The Wayland/Linux capture-backend selection rule (pure, no platform calls).
// This is the deterministic core the backlog item's threshold turns on: on
// Wayland the affordance routes to the Portal when one is available, and to
// Unavailable (the honest-degrade signal) when it is not — never to a
// grabWindow that would silently return null.
void TestCaptureBackend::linuxScreenshotPolicy() {
    using B = LinuxScreenshotBackend;
    // Not Wayland (X11 / xcb / offscreen / Windows): always the client-side
    // grab, independent of whether a portal happens to be present.
    QCOMPARE(chooseLinuxScreenshotBackend(/*wayland=*/false, /*portal=*/false), B::QScreenGrab);
    QCOMPARE(chooseLinuxScreenshotBackend(/*wayland=*/false, /*portal=*/true), B::QScreenGrab);
    // Wayland with a portal -> route through the portal (the only path that
    // yields real pixels there).
    QCOMPARE(chooseLinuxScreenshotBackend(/*wayland=*/true, /*portal=*/true), B::Portal);
    // Wayland with no portal -> Unavailable, so the caller degrades honestly
    // instead of silently producing nothing.
    QCOMPARE(chooseLinuxScreenshotBackend(/*wayland=*/true, /*portal=*/false), B::Unavailable);
}

// The isWaylandSession() wrapper reflects the live Qt platform. The unit suite
// runs under offscreen, so this pins that offscreen is not misread as Wayland
// (which would wrongly route to the portal path in the real app).
void TestCaptureBackend::waylandSessionMatchesPlatform() {
    // Make the assertion deterministic regardless of the developer's shell: with
    // no Wayland env signals present, isWaylandSession() reflects only the live
    // platform (offscreen -> false), which is what the real app needs so the
    // offscreen/CI plugin is never misrouted to the portal path.
    const bool origHasWd = qEnvironmentVariableIsSet("WAYLAND_DISPLAY");
    const QString origWd = qEnvironmentVariable("WAYLAND_DISPLAY");
    const bool origHasSt = qEnvironmentVariableIsSet("XDG_SESSION_TYPE");
    const QString origSt = qEnvironmentVariable("XDG_SESSION_TYPE");
    qunsetenv("WAYLAND_DISPLAY");
    qunsetenv("XDG_SESSION_TYPE");

    const bool platformIsWayland =
        QGuiApplication::platformName().startsWith(QStringLiteral("wayland"), Qt::CaseInsensitive);
    QCOMPARE(isWaylandSession(), platformIsWayland);

    if (origHasWd)
        qputenv("WAYLAND_DISPLAY", origWd.toLocal8Bit());
    if (origHasSt)
        qputenv("XDG_SESSION_TYPE", origSt.toLocal8Bit());
}

// Safeguard 1 (pure): the full display-server-signal truth table for
// isWaylandSessionFromSignals — no env, no display, fully deterministic.
void TestCaptureBackend::waylandSessionFromSignalsTruthTable() {
    // Native Wayland plugin -> Wayland regardless of env.
    QVERIFY(isWaylandSessionFromSignals(QStringLiteral("wayland"), QString(), QString()));
    QVERIFY(isWaylandSessionFromSignals(QStringLiteral("wayland-egl"), QString(), QString()));

    // XWayland: xcb plugin but WAYLAND_DISPLAY set -> Wayland (the black-frame
    // case Safeguard 1 exists for).
    QVERIFY(isWaylandSessionFromSignals(QStringLiteral("xcb"), QStringLiteral("wayland-0"),
                                        QString()));
    // XWayland via XDG_SESSION_TYPE alone -> Wayland.
    QVERIFY(isWaylandSessionFromSignals(QStringLiteral("xcb"), QString(),
                                        QStringLiteral("wayland")));
    QVERIFY(isWaylandSessionFromSignals(QStringLiteral("xcb"), QString(),
                                        QStringLiteral("Wayland"))); // case-insensitive

    // Genuine X11 (no signals) -> NOT Wayland. This keeps the direct grab path.
    QVERIFY(!isWaylandSessionFromSignals(QStringLiteral("xcb"), QString(), QString()));
    QVERIFY(!isWaylandSessionFromSignals(QStringLiteral("xcb"), QString(), QStringLiteral("x11")));
    // Offscreen/CI with no signals -> NOT Wayland (parity preserved).
    QVERIFY(!isWaylandSessionFromSignals(QStringLiteral("offscreen"), QString(), QString()));
    // An EMPTY WAYLAND_DISPLAY is not a signal -> genuine X11 stays X11.
    QVERIFY(!isWaylandSessionFromSignals(QStringLiteral("xcb"), QString(), QStringLiteral("")));
}

// Safeguard 1 (composition): the XWayland case must route to the portal when
// available and to the honest-degrade signal (Unavailable) when not — NEVER to
// the direct QScreenGrab that returns a black pixmap. This composes the two
// pure seams exactly as the live app does (isWaylandSessionFromSignals feeding
// chooseLinuxScreenshotBackend), so it is the deterministic proof of the fix.
void TestCaptureBackend::xwaylandRoutesToPortalOrUnavailable() {
    using B = LinuxScreenshotBackend;
    // XWayland session (xcb plugin, WAYLAND_DISPLAY set) + portal available -> Portal.
    const bool xwaylandSession =
        isWaylandSessionFromSignals(QStringLiteral("xcb"), QStringLiteral("wayland-0"), QString());
    QVERIFY(xwaylandSession);
    QCOMPARE(chooseLinuxScreenshotBackend(xwaylandSession, /*portal=*/true), B::Portal);
    // XWayland + no portal -> Unavailable (honest degrade), NOT QScreenGrab.
    QCOMPARE(chooseLinuxScreenshotBackend(xwaylandSession, /*portal=*/false), B::Unavailable);
    QVERIFY(chooseLinuxScreenshotBackend(xwaylandSession, /*portal=*/false) != B::QScreenGrab);

    // Genuine X11 (no Wayland signals, xcb) -> QScreenGrab unchanged, either
    // portal-flag value.
    const bool x11Session =
        isWaylandSessionFromSignals(QStringLiteral("xcb"), QString(), QString());
    QVERIFY(!x11Session);
    QCOMPARE(chooseLinuxScreenshotBackend(x11Session, /*portal=*/true), B::QScreenGrab);
    QCOMPARE(chooseLinuxScreenshotBackend(x11Session, /*portal=*/false), B::QScreenGrab);
}

// Safeguard 1 (XWayland black-frame): the live isWaylandSession() must treat a
// Wayland display server as a Wayland session even when Qt loaded the xcb
// plugin (XWayland), where a direct grabWindow yields a BLACK pixmap. Detection
// keys off the display-server signals (WAYLAND_DISPLAY / XDG_SESSION_TYPE), not
// only the Qt platform name. Driven here through the env seam so it is
// deterministic and needs no real compositor. (RED before the fix: the old
// wrapper consulted only platformName() and returned false for XWayland.)
void TestCaptureBackend::waylandSessionDetectsXWaylandViaEnv() {
    // The unit suite runs under the offscreen plugin, so platformName() is not
    // "wayland"; any Wayland verdict below therefore comes purely from the env
    // signals — exactly the XWayland (xcb-plugin-on-a-Wayland-server) case.
    const bool origHasWd = qEnvironmentVariableIsSet("WAYLAND_DISPLAY");
    const QString origWd = qEnvironmentVariable("WAYLAND_DISPLAY");
    const bool origHasSt = qEnvironmentVariableIsSet("XDG_SESSION_TYPE");
    const QString origSt = qEnvironmentVariable("XDG_SESSION_TYPE");
    auto restore = [&]() {
        if (origHasWd)
            qputenv("WAYLAND_DISPLAY", origWd.toLocal8Bit());
        else
            qunsetenv("WAYLAND_DISPLAY");
        if (origHasSt)
            qputenv("XDG_SESSION_TYPE", origSt.toLocal8Bit());
        else
            qunsetenv("XDG_SESSION_TYPE");
    };

    // Baseline: no Wayland signals under offscreen -> not a Wayland session.
    qunsetenv("WAYLAND_DISPLAY");
    qunsetenv("XDG_SESSION_TYPE");
    QCOMPARE(isWaylandSession(), false);

    // XWayland via WAYLAND_DISPLAY set (non-empty) -> Wayland session.
    qputenv("WAYLAND_DISPLAY", QByteArrayLiteral("wayland-0"));
    QCOMPARE(isWaylandSession(), true);

    // XWayland via XDG_SESSION_TYPE=="wayland" alone -> Wayland session.
    qunsetenv("WAYLAND_DISPLAY");
    qputenv("XDG_SESSION_TYPE", QByteArrayLiteral("wayland"));
    QCOMPARE(isWaylandSession(), true);

    // An empty WAYLAND_DISPLAY is NOT a Wayland signal, and a non-wayland
    // session type keeps the genuine-X11 verdict.
    qputenv("WAYLAND_DISPLAY", QByteArray());
    qputenv("XDG_SESSION_TYPE", QByteArrayLiteral("x11"));
    QCOMPARE(isWaylandSession(), false);

    restore();
}

// Live end-to-end portal capture. This exercises the real QtDBus path
// (capturePortalScreenshotToPng) ONLY when a screenshot portal is actually
// reachable on the session bus — i.e. under the stood-up sway + dbus +
// pipewire + xdg-desktop-portal(+wlr) stack. In ordinary offscreen CI (no
// session bus, no portal) it QSKIPs, mirroring the Wine-only QSKIP precedent:
// the deterministic policy above is the always-on coverage; this adds real
// coverage where the stack exists without gating the fast PR loop.
void TestCaptureBackend::livePortalCaptureOrSkip() {
    if (!trailer::portalScreenshotAvailable())
        QSKIP("no XDG screenshot portal on the session bus — live capture not testable here");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString out = dir.filePath("portal-shot.png");

    QString err;
    const auto r = trailer::capturePortalScreenshotToPng(out, /*interactive=*/false, &err);
    // A non-interactive whole-screen grab should not be Cancelled; Ok is the
    // expected outcome, and any failure must carry a reason (never a silent
    // null — the property the backlog item guards).
    if (r == trailer::PortalCaptureResult::Cancelled)
        QSKIP("portal reported the request cancelled — not a deterministic outcome to assert");
    QVERIFY2(r == trailer::PortalCaptureResult::Ok,
             qPrintable(QStringLiteral("portal capture failed: %1").arg(err)));
    QVERIFY(QFileInfo::exists(out));
    QVERIFY(QFileInfo(out).size() > 0);
}

#ifndef Q_OS_MACOS
// Off-Mac, the native picker path is the link-clean stub: ScreenCaptureKit is
// never available and a capture attempt reports Unavailable with a reason. The
// Apple-only MacScreenCapture.mm is not built here, so this pins the stub's
// contract that the two capture call sites rely on to fall back gracefully.
void TestCaptureBackend::nativeStubIsUnavailable() {
    QCOMPARE(trailer::screenCaptureKitAvailable(), false);

    QString err;
    const auto r = trailer::captureViaPickerToPng(QStringLiteral("/tmp/x.png"),
                                                  /*wholeDisplay=*/false, &err);
    QCOMPARE(r, PickerCaptureResult::Unavailable);
    QVERIFY(!err.isEmpty());
}
#endif

QTEST_MAIN(TestCaptureBackend)
#include "test_capture_backend.moc"
