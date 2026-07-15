// Unit test — macOS launch / activation shape.
//
// Guards the owner ruling (backlog 2026-07-12-macos-launch-no-open-panel):
//   - On ALL platforms, dismissing a dialog must never quit the app. The
//     off-Mac-testable guarantee is that Application disables Qt's
//     quit-on-last-window-closed, so tearing down the last window (or a
//     modal dialog that momentarily is the only top-level) can never
//     trigger an implicit QApplication::quit().
//   - On macOS only, activating the app with zero windows must NOT
//     auto-open a file panel, spawn a window, or quit — launch is dock
//     icon + menu bar only. That path is a native app-lifecycle event
//     (ApplicationStateChange -> Active) that the offscreen plugin on
//     Linux never raises, so it is asserted structurally here and
//     QSKIP-ped off-Mac (mirroring tests/uat/test_uat_empty_state.cpp).
//
// Mirrors the custom-main + HOME-sandbox scaffolding of
// test_uat_empty_state.cpp so Settings/RecentFiles write into a throwaway
// sandbox and the app starts from a no-window baseline.

#include "app/Application.h"
#include "ui/MainWindow.h"

#include <QDir>
#include <QEvent>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace trailer;

class TestMacosLaunch : public QObject {
    Q_OBJECT
  private slots:
    // All platforms: the empty-state window model means the app must
    // outlive its last window, so quit-on-last-window-closed must be off.
    // This is the concrete, off-Mac guarantee that dismissing a dialog
    // never quits the app.
    void quitOnLastWindowClosedIsDisabled();

    // macOS: activating with zero windows does nothing automatic — no
    // file panel, no new window, no quit.
    void zeroWindowActivationDoesNothing();
};

void TestMacosLaunch::quitOnLastWindowClosedIsDisabled() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    QVERIFY2(!app->quitOnLastWindowClosed(),
             "Application must disable quitOnLastWindowClosed so tearing down "
             "the last window / dismissing a dialog never quits the app");
}

void TestMacosLaunch::zeroWindowActivationDoesNothing() {
#ifndef Q_OS_MACOS
    QSKIP("Zero-window activation is a macOS-only app-lifecycle path "
          "(ApplicationStateChange -> Active); not exercisable off-Mac.");
#else
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    QCOMPARE(app->windowCount(), 0);

    // Deliver an application-state-change to the app the way the platform
    // would on a dock activation. The handler must not open a panel, spawn
    // a window, or quit when there are zero windows — launch is dock icon +
    // menu bar only (no automatic Open panel).
    QEvent stateChange(QEvent::ApplicationStateChange);
    QCoreApplication::sendEvent(app, &stateChange);
    QCoreApplication::processEvents();

    QCOMPARE(app->windowCount(), 0);
#endif
}

// Custom main: sandbox HOME before Application is constructed so
// Settings/RecentFiles never touch the real config dir.
int main(int argc, char **argv) {
    QTemporaryDir fakeHome;
    if (!fakeHome.isValid())
        return 1;
    qputenv("HOME", fakeHome.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (fakeHome.path() + "/.config").toUtf8());
    qputenv("XDG_DATA_HOME", (fakeHome.path() + "/.local/share").toUtf8());
    QDir().mkpath(fakeHome.path() + "/.config/trailer");
    QDir().mkpath(fakeHome.path() + "/.local/share/trailer");

    Application app(argc, argv);
    TestMacosLaunch tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_macos_launch.moc"
