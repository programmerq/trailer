// Unit test — capture-backend policy + settings round-trip.
//
// Covers the pure, platform-agnostic surface of the ScreenCaptureKit
// picker backend: the string<->enum mapping (captureBackendFromString /
// captureBackendToString), the backend-selection rule
// (effectiveCaptureBackend), and that a chosen capture_backend persists
// through Settings save/reload. The native picker (SCContentSharingPicker /
// SCScreenshotManager) is Apple-only and not exercised here — this test
// needs no Mac.

#include "platform/ScreenCaptureBackend.h"
#include "settings/Settings.h"

#include <QFile>
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
