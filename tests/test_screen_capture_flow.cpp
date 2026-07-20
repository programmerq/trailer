// Unit test — Screen Capture permission preflight flow
// (the screen-capture preflight ADR, 2026-07-16-capture-permission-preflight.md).
//
// Exercises the pure, platform-agnostic pieces of the screen-capture
// permission preflight layer that gate the `screencapture` call site
// (Application::captureScreenshot, reached from MainWindow::onTakeScreenshot
// and the File ▸ Screenshot submenu / macOS no-window bar):
//   - decideScreenCaptureFlow(): the pure 3-state → 2-action decision table,
//   - screenRecordingSettingsUrlString(): the deep link to the macOS Screen
//     Recording settings pane,
//   - screenRecordingNeededMessage(): the shared graceful-degrade string.
// The native CGPreflight/CGRequest query and the modal dialogs are macOS-only
// and guarded; on non-mac the provider fallback is asserted here too. The rest
// covers only the platform-agnostic logic, which needs no Mac.

#include "platform/ScreenCapturePermission.h"
#include "settings/Settings.h"

#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace trailer;

class TestScreenCaptureFlow : public QObject {
    Q_OBJECT
  private slots:
    void grantedProceeds();
    void deniedRequests();
    void undeterminedRequests();
    void settingsUrlIsScreenCapturePane();
    void neededMessageMentions();
#ifndef Q_OS_MACOS
    void nonMacQueryReturnsGranted();
#endif
};

// Granted proceeds straight to capture.
void TestScreenCaptureFlow::grantedProceeds() {
    QCOMPARE(decideScreenCaptureFlow(ScreenCapturePermissionState::Granted),
             ScreenCaptureFlowAction::Proceed);
}

// Denied routes through the OS request (a silent no-op when actually denied, so
// no crosshair spawns); the explainer is retired for stills.
void TestScreenCaptureFlow::deniedRequests() {
    QCOMPARE(decideScreenCaptureFlow(ScreenCapturePermissionState::Denied),
             ScreenCaptureFlowAction::RequestAccess);
}

// Undetermined → drive the OS request (which prompts if truly undetermined and
// no-ops if denied). NOT a bare Proceed: the crosshair must never spawn until
// the request confirms access. No explainer precedes it.
void TestScreenCaptureFlow::undeterminedRequests() {
    QCOMPARE(decideScreenCaptureFlow(ScreenCapturePermissionState::Undetermined),
             ScreenCaptureFlowAction::RequestAccess);
}

// The deep link targets the Screen Recording privacy pane specifically.
void TestScreenCaptureFlow::settingsUrlIsScreenCapturePane() {
    const QString url = screenRecordingSettingsUrlString();
    QVERIFY2(url.startsWith("x-apple.systempreferences:"),
             "The settings deep link must use the x-apple.systempreferences scheme");
    QVERIFY2(url.contains("Privacy_ScreenCapture"),
             "The settings deep link must target the Screen Recording pane");
}

// The graceful-degrade message must name the macOS permission, where to change
// it, and the relaunch nuance so the user can recover.
void TestScreenCaptureFlow::neededMessageMentions() {
    const QString msg = screenRecordingNeededMessage();
    QVERIFY2(msg.contains("Screen Recording"),
             "The needed message must name the 'Screen Recording' permission");
    QVERIFY2(msg.contains("System Settings"),
             "The needed message must point the user at System Settings");
    QVERIFY2(msg.contains("reopen"),
             "The needed message must mention reopening Trailer (relaunch nuance)");
}

#ifndef Q_OS_MACOS
// Off-Mac there is no TCC: the provider must report Granted so the
// QScreen::grabWindow fallback runs unimpeded.
void TestScreenCaptureFlow::nonMacQueryReturnsGranted() {
    QCOMPARE(queryScreenCapturePermissionState(),
             ScreenCapturePermissionState::Granted);
}
#endif

QTEST_MAIN(TestScreenCaptureFlow)
#include "test_screen_capture_flow.moc"
