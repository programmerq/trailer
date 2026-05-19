#include "settings/Settings.h"

#include <QDir>
#include <QFile>
#include <QObject>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace trailer;

class TestSettings : public QObject {
    Q_OBJECT
  private slots:
    void defaults();
    void roundTrip();
    void missingFileYieldsDefaults();
    void enumConversions();
    void firstUseFlagsRoundTrip();
    void sessionRoundTrips();
    void mlSchedulerDefaults();
    void mlSchedulerRoundTrip();
};

void TestSettings::defaults() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Settings s(dir.filePath("settings.toml"));
    QCOMPARE(s.theme(), Theme::System);
    // Default flipped to NewWindow in the 2026-04-24 HITL pass —
    // tabs are opt-in via settings.toml.
    QCOMPARE(s.openFilesIn(), OpenFilesIn::NewWindow);
    QCOMPARE(s.autoSave(), true);
    QCOMPARE(s.recentMax(), 50);
    QCOMPARE(s.redactionWarningAcknowledged(), false);
    QCOMPARE(s.restorePreviousWindows(), true);
    QVERIFY(s.sessionOpenFiles().isEmpty());
    QCOMPARE(s.mlRecognizeTextInBackground(), true);
    QCOMPARE(s.mlPreloadSegmentationOnToolActivation(), true);
    QCOMPARE(s.mlRunOnBattery(), false);
}

void TestSettings::roundTrip() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("settings.toml");

    {
        Settings s(path);
        s.setTheme(Theme::Dark);
        s.setOpenFilesIn(OpenFilesIn::NewWindow);
        s.setAutoSave(false);
        s.setRecentMax(20);
        s.setRedactionWarningAcknowledged(true);
        s.setLastSaveDir(QStringLiteral("/some/where/Documents"));
        s.save();
    }

    QVERIFY(QFile::exists(path));

    Settings reloaded(path);
    reloaded.load();
    QCOMPARE(reloaded.theme(), Theme::Dark);
    QCOMPARE(reloaded.openFilesIn(), OpenFilesIn::NewWindow);
    QCOMPARE(reloaded.autoSave(), false);
    QCOMPARE(reloaded.recentMax(), 20);
    QCOMPARE(reloaded.redactionWarningAcknowledged(), true);
    QCOMPARE(reloaded.lastSaveDir(), QStringLiteral("/some/where/Documents"));
}

void TestSettings::missingFileYieldsDefaults() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Settings s(dir.filePath("does_not_exist.toml"));
    s.load();
    QCOMPARE(s.theme(), Theme::System);
}

void TestSettings::enumConversions() {
    QCOMPARE(themeFromString("dark"), Theme::Dark);
    QCOMPARE(themeFromString("light"), Theme::Light);
    QCOMPARE(themeFromString("system"), Theme::System);
    QCOMPARE(themeFromString("bogus"), Theme::System);

    QCOMPARE(openFilesInFromString("new_tab"), OpenFilesIn::NewTab);
    QCOMPARE(openFilesInFromString("new_window"), OpenFilesIn::NewWindow);
    QCOMPARE(openFilesInFromString("same_window"), OpenFilesIn::SameWindow);
}

void TestSettings::firstUseFlagsRoundTrip() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("settings.toml");

    {
        Settings s(path);
        s.setFirstUseAcknowledged(QStringLiteral("ml_never_download_u2netp"), true);
        s.setFirstUseAcknowledged(QStringLiteral("ml_never_download_mobile_sam_encoder"), true);
        s.save();
    }

    Settings reloaded(path);
    reloaded.load();
    QVERIFY(reloaded.firstUseAcknowledged(QStringLiteral("ml_never_download_u2netp")));
    QVERIFY(reloaded.firstUseAcknowledged(QStringLiteral("ml_never_download_mobile_sam_encoder")));
    QVERIFY(!reloaded.firstUseAcknowledged(QStringLiteral("ml_never_download_pp_ocr_detector")));
}

void TestSettings::sessionRoundTrips() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("settings.toml");
    {
        Settings s(path);
        s.setRestorePreviousWindows(false);
        s.setSessionOpenFiles({QStringLiteral("/tmp/a.pdf"), QStringLiteral("/tmp/b.png")});
        s.save();
    }
    Settings reloaded(path);
    reloaded.load();
    QCOMPARE(reloaded.restorePreviousWindows(), false);
    QCOMPARE(reloaded.sessionOpenFiles().size(), 2);
    QCOMPARE(reloaded.sessionOpenFiles().first(), QStringLiteral("/tmp/a.pdf"));
    QCOMPARE(reloaded.sessionOpenFiles().last(), QStringLiteral("/tmp/b.png"));
}

void TestSettings::mlSchedulerDefaults() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Settings s(dir.filePath("missing.toml"));
    s.load();
    QCOMPARE(s.mlRecognizeTextInBackground(), true);
    QCOMPARE(s.mlPreloadSegmentationOnToolActivation(), true);
    QCOMPARE(s.mlRunOnBattery(), false);
}

void TestSettings::mlSchedulerRoundTrip() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("settings.toml");
    {
        Settings s(path);
        s.setMlRecognizeTextInBackground(false);
        s.setMlPreloadSegmentationOnToolActivation(false);
        s.setMlRunOnBattery(true);
        s.save();
    }
    Settings reloaded(path);
    reloaded.load();
    QCOMPARE(reloaded.mlRecognizeTextInBackground(), false);
    QCOMPARE(reloaded.mlPreloadSegmentationOnToolActivation(), false);
    QCOMPARE(reloaded.mlRunOnBattery(), true);
}

QTEST_MAIN(TestSettings)
#include "test_settings.moc"
