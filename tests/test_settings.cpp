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

QTEST_MAIN(TestSettings)
#include "test_settings.moc"
