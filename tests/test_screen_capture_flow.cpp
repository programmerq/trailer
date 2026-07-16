// Unit test — Screen Capture permission preflight flow
// (the screen-capture preflight ADR, 2026-07-16-capture-permission-preflight.md).
//
// Exercises the pure, platform-agnostic pieces of the screen-capture
// permission preflight layer that gate the two `screencapture` call sites
// (MainWindow::onTakeScreenshot, Application::acquireFromScreenshot):
//   - decideScreenCaptureFlow(): the pure 3-state → 4-action decision table,
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
    void deniedDegrades();
    void undeterminedUnackShowsExplainer();
    void undeterminedAckRequests();
    void settingsUrlIsScreenCapturePane();
    void neededMessageMentions();
#ifndef Q_OS_MACOS
    void nonMacQueryReturnsGranted();
#endif
};

// Granted always proceeds straight to capture, regardless of whether the
// first-run explainer has been acknowledged.
void TestScreenCaptureFlow::grantedProceeds() {
    QCOMPARE(decideScreenCaptureFlow(ScreenCapturePermissionState::Granted, false),
             ScreenCaptureFlowAction::Proceed);
    QCOMPARE(decideScreenCaptureFlow(ScreenCapturePermissionState::Granted, true),
             ScreenCaptureFlowAction::Proceed);
}

// Denied always degrades gracefully (no OS crosshair), regardless of the
// explainer flag.
void TestScreenCaptureFlow::deniedDegrades() {
    QCOMPARE(decideScreenCaptureFlow(ScreenCapturePermissionState::Denied, false),
             ScreenCaptureFlowAction::DegradeDenied);
    QCOMPARE(decideScreenCaptureFlow(ScreenCapturePermissionState::Denied, true),
             ScreenCaptureFlowAction::DegradeDenied);
}

// Undetermined + not-yet-acknowledged → show the explainer before the OS UI.
void TestScreenCaptureFlow::undeterminedUnackShowsExplainer() {
    QCOMPARE(decideScreenCaptureFlow(ScreenCapturePermissionState::Undetermined, false),
             ScreenCaptureFlowAction::ShowExplainerFirst);
}

// Undetermined + already acknowledged → drive the OS request (which prompts if
// truly undetermined and no-ops if denied). NOT a bare Proceed: the crosshair
// must never spawn until the request confirms access.
void TestScreenCaptureFlow::undeterminedAckRequests() {
    QCOMPARE(decideScreenCaptureFlow(ScreenCapturePermissionState::Undetermined, true),
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
