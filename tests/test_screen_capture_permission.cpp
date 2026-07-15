// Unit test — Screen Recording pre-permission explainer gate.
//
// Exercises the pure, platform-agnostic first-run gate that backs the
// macOS "Screen Recording" pre-permission explainer (backlog
// 2026-07-13-macos-screenrecording-services-clarity). The gate must:
//   - return true the FIRST time (show the explainer now),
//   - return false on every subsequent call (suppress),
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
    void showsOnceThenSuppressed();
    void persistsAcrossReload();
};

// First call shows (true); the same Settings instance suppresses forever after.
void TestScreenCapturePermission::showsOnceThenSuppressed() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Settings s(dir.filePath("settings.toml"));

    QVERIFY2(consumeScreenCaptureExplainer(s),
             "First use must show the explainer");
    QVERIFY2(!consumeScreenCaptureExplainer(s),
             "Second use must suppress the explainer");
    QVERIFY2(!consumeScreenCaptureExplainer(s),
             "Every subsequent use must keep suppressing");
}

// The gate persists its decision: a fresh Settings loaded from the same
// file after one "show" must suppress (the explainer survives a relaunch).
void TestScreenCapturePermission::persistsAcrossReload() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("settings.toml");

    {
        Settings s(path);
        QVERIFY2(consumeScreenCaptureExplainer(s),
                 "First use on a fresh profile must show the explainer");
    }

    Settings reloaded(path);
    reloaded.load();
    QVERIFY2(!consumeScreenCaptureExplainer(reloaded),
             "A relaunch (fresh Settings from the same file) must suppress the "
             "explainer that was already shown");
}

QTEST_MAIN(TestScreenCapturePermission)
#include "test_screen_capture_permission.moc"
