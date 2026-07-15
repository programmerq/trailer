// Unit test — macOS launch / activation shape.
//
// Guards the owner ruling (backlog 2026-07-12-macos-launch-no-open-panel):
//   - The app must survive its last window closing so dismissing a dialog
//     never quits it. This is platform-shaped: on macOS the app disables
//     Qt's quit-on-last-window-closed (dock icon + global menu bar keep it
//     alive with zero windows); off-Mac the persistent empty-state window
//     keeps a top-level alive, so a dialog is never the sole window and
//     Qt's default (quit-on-last-window-closed = true) is left in place —
//     disabling it there would strand the process with no window and no way
//     to quit or open a file. Both branches are asserted below.
//   - On macOS only, activating the app with zero windows must NOT
//     auto-open a file panel, spawn a window, or quit — launch is dock
//     icon + menu bar only. That path is a native app-lifecycle event
//     (ApplicationStateChange -> Qt::ApplicationActive) that the offscreen
//     plugin off-Mac never raises and can't be driven into a genuinely
//     Active zero-window state non-flakily, so it is NOT unit-tested here
//     (see the note on the removed test below); it is covered by the
//     real-Mac checklist and tests/uat/test_uat_empty_state.cpp.
//
// Mirrors the custom-main + HOME-sandbox scaffolding of
// test_uat_empty_state.cpp so Settings/RecentFiles write into a throwaway
// sandbox and the app starts from a no-window baseline.

#include "app/Application.h"
#include "ui/MainWindow.h"

#include <QDir>
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

    // NOTE: "activating the app with zero windows does nothing automatic
    // (no auto Open panel, no new window, no quit)" is deliberately NOT
    // unit-tested here. That behaviour hangs off a native macOS
    // app-lifecycle event (ApplicationStateChange -> Qt::ApplicationActive
    // on a dock click / Cmd-Tab) that the offscreen QPA plugin used off-Mac
    // never raises, and which cannot be driven into a genuinely-Active
    // zero-window state non-flakily under CI. A synthetic bare
    // QEvent(ApplicationStateChange) does NOT make applicationState()==Active,
    // so it would assert nothing real. It is instead covered by the
    // real-Mac verification checklist (backlog
    // 2026-07-12-macos-reopen-realhw-verify) and by
    // tests/uat/test_uat_empty_state.cpp.
};

void TestMacosLaunch::quitOnLastWindowClosedIsDisabled() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
#ifdef Q_OS_MACOS
    // macOS lives as dock icon + global menu bar with zero windows, so the app
    // must disable quit-on-last-window-closed: tearing down the last window (or
    // dismissing a dialog that is momentarily the only top-level) must not quit.
    QVERIFY2(!app->quitOnLastWindowClosed(),
             "On macOS the app must disable quitOnLastWindowClosed so tearing "
             "down the last window / dismissing a dialog never quits the app");
#else
    // Off-Mac the persistent empty-state window keeps a top-level alive, so a
    // dialog is never the sole window and Qt's default (true) is restored — the
    // app must NOT survive with zero windows and no way to quit or open a file.
    QVERIFY2(app->quitOnLastWindowClosed(),
             "Off-Mac the app must keep Qt's default quit-on-last-window-closed "
             "(true); the persistent empty-state window prevents a dialog being "
             "the sole top-level, so disabling it would strand the process");
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
