// Unit test — Screen Recording pre-permission explainer gate.
//
// Exercises the pure, platform-agnostic first-run gate that backs the
// macOS "Screen Recording" pre-permission explainer (backlog
// 2026-07-13-macos-screenrecording-services-clarity). The gate must:
//   - report "show" (shouldShow==true) while unacknowledged,
//   - NOT burn the flag on a mere query, so cancelling the explainer (query
//     without acknowledge) shows it again next time,
//   - once the user proceeds (acknowledge), stay suppressed thereafter,
//   - persist the "shown" flag so a fresh Settings loaded from the same
//     file also suppresses (survives a relaunch).
// The native dialog itself is macOS-only and guarded; this test covers
// only the flag gating, which needs no Mac.

#include "platform/ScreenCapturePermission.h"
#include "settings/Settings.h"

#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace trailer;

class TestScreenCapturePermission : public QObject {
    Q_OBJECT
  private slots:
    void queryDoesNotBurnFlag();
    void cancelReShowsContinueSuppresses();
    void showsUntilAcknowledged();
    void persistsAcrossReload();
};

// A bare query must never mutate the flag: shouldShow stays true across
// repeated calls until the user actually acknowledges.
void TestScreenCapturePermission::queryDoesNotBurnFlag() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Settings s(dir.filePath("settings.toml"));

    QVERIFY2(shouldShowScreenCaptureExplainer(s),
             "First query on a fresh profile must report show");
    QVERIFY2(shouldShowScreenCaptureExplainer(s),
             "Querying must not burn the flag — still show without acknowledge");
    QVERIFY2(shouldShowScreenCaptureExplainer(s),
             "Repeated queries must keep reporting show until acknowledged");
}

// The nit fix: cancelling the explainer (query, no acknowledge) must show it
// again next time; only proceeding (acknowledge) suppresses it thereafter.
void TestScreenCapturePermission::cancelReShowsContinueSuppresses() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Settings s(dir.filePath("settings.toml"));

    // First use: explainer would be shown. User CANCELS -> no acknowledge.
    QVERIFY2(shouldShowScreenCaptureExplainer(s),
             "First use must show the explainer");
    // Cancel does NOT call acknowledgeScreenCaptureExplainer.
    QVERIFY2(shouldShowScreenCaptureExplainer(s),
             "After a cancel the explainer must be shown again — flag NOT set");

    // Next use: user CONTINUES -> acknowledge burns the flag.
    acknowledgeScreenCaptureExplainer(s);
    QVERIFY2(!shouldShowScreenCaptureExplainer(s),
             "After the user proceeds (acknowledge) the explainer must be "
             "suppressed next time");
}

// Once acknowledged, the same Settings instance suppresses forever after.
void TestScreenCapturePermission::showsUntilAcknowledged() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Settings s(dir.filePath("settings.toml"));

    QVERIFY2(shouldShowScreenCaptureExplainer(s),
             "Fresh profile must show the explainer");
    acknowledgeScreenCaptureExplainer(s);
    QVERIFY2(!shouldShowScreenCaptureExplainer(s),
             "After acknowledge the explainer must be suppressed");
    QVERIFY2(!shouldShowScreenCaptureExplainer(s),
             "Every subsequent use must keep suppressing");
}

// The gate persists its decision: a fresh Settings loaded from the same
// file after an acknowledge must suppress (survives a relaunch).
void TestScreenCapturePermission::persistsAcrossReload() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("settings.toml");

    {
        Settings s(path);
        QVERIFY2(shouldShowScreenCaptureExplainer(s),
                 "First use on a fresh profile must show the explainer");
        acknowledgeScreenCaptureExplainer(s);
    }

    Settings reloaded(path);
    reloaded.load();
    QVERIFY2(!shouldShowScreenCaptureExplainer(reloaded),
             "A relaunch (fresh Settings from the same file) must suppress the "
             "explainer that was already acknowledged");
}

QTEST_MAIN(TestScreenCapturePermission)
#include "test_screen_capture_permission.moc"
