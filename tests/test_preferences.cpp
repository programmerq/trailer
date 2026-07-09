#include "settings/Settings.h"
#include "ui/PreferencesDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QObject>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest/QtTest>

using namespace trailer;

class TestPreferences : public QObject {
    Q_OBJECT
  private slots:
    void roundTrip();
    void cancelDiscards();
    void restoreDefaultsResetsAll();
    void perFieldReset();
    void themeControlDisabled();
    void captureScreenshots();
};

// Drive every editable control to a value that differs from the default.
static void mutateAllControls(PreferencesDialog &dlg) {
    // Defaults: open_files_in = NewWindow, restore = true, auto_save = true,
    // recent_max = 50, all three ML toggles = (true, true, false).
    dlg.findChild<QComboBox *>("openFilesInCombo")
        ->setCurrentIndex(dlg.findChild<QComboBox *>("openFilesInCombo")
                              ->findData(static_cast<int>(OpenFilesIn::SameWindow)));
    dlg.findChild<QCheckBox *>("restoreWindowsCheck")->setChecked(false);
    dlg.findChild<QCheckBox *>("autoSaveCheck")->setChecked(false);
    dlg.findChild<QSpinBox *>("recentMaxSpin")->setValue(7);
    dlg.findChild<QCheckBox *>("mlRecognizeTextCheck")->setChecked(false);
    dlg.findChild<QCheckBox *>("mlPreloadSegCheck")->setChecked(false);
    dlg.findChild<QCheckBox *>("mlRunOnBatteryCheck")->setChecked(true);
}

void TestPreferences::roundTrip() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("settings.toml");

    {
        Settings s(path);
        PreferencesDialog dlg(s);
        mutateAllControls(dlg);
        dlg.accept();
    }

    Settings s2(path);
    s2.load();
    QCOMPARE(s2.openFilesIn(), OpenFilesIn::SameWindow);
    QCOMPARE(s2.restorePreviousWindows(), false);
    QCOMPARE(s2.autoSave(), false);
    QCOMPARE(s2.recentMax(), 7);
    QCOMPARE(s2.mlRecognizeTextInBackground(), false);
    QCOMPARE(s2.mlPreloadSegmentationOnToolActivation(), false);
    QCOMPARE(s2.mlRunOnBattery(), true);
}

void TestPreferences::cancelDiscards() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("settings.toml");

    {
        Settings s(path);
        PreferencesDialog dlg(s);
        mutateAllControls(dlg);
        dlg.reject();
    }

    // Reject writes nothing: the file must not exist, and a fresh load
    // yields the defaults.
    QVERIFY(!QFile::exists(path));
    Settings s2(path);
    s2.load();
    QCOMPARE(s2.openFilesIn(), OpenFilesIn::NewWindow);
    QCOMPARE(s2.restorePreviousWindows(), true);
    QCOMPARE(s2.autoSave(), true);
    QCOMPARE(s2.recentMax(), 50);
    QCOMPARE(s2.mlRecognizeTextInBackground(), true);
    QCOMPARE(s2.mlPreloadSegmentationOnToolActivation(), true);
    QCOMPARE(s2.mlRunOnBattery(), false);
}

void TestPreferences::restoreDefaultsResetsAll() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("settings.toml");

    {
        Settings s(path);
        PreferencesDialog dlg(s);
        mutateAllControls(dlg);
        auto *box = dlg.findChild<QDialogButtonBox *>("buttonBox");
        QVERIFY(box);
        box->button(QDialogButtonBox::RestoreDefaults)->click();
        dlg.accept();
    }

    Settings s2(path);
    s2.load();
    QCOMPARE(s2.openFilesIn(), OpenFilesIn::NewWindow);
    QCOMPARE(s2.restorePreviousWindows(), true);
    QCOMPARE(s2.autoSave(), true);
    QCOMPARE(s2.recentMax(), 50);
    QCOMPARE(s2.mlRecognizeTextInBackground(), true);
    QCOMPARE(s2.mlPreloadSegmentationOnToolActivation(), true);
    QCOMPARE(s2.mlRunOnBattery(), false);
}

void TestPreferences::perFieldReset() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("settings.toml");

    {
        Settings s(path);
        PreferencesDialog dlg(s);
        // Mutate two controls...
        dlg.findChild<QSpinBox *>("recentMaxSpin")->setValue(7);
        dlg.findChild<QCheckBox *>("autoSaveCheck")->setChecked(false);
        // ...then reset only the recent-max field.
        dlg.findChild<QToolButton *>("reset_recentMax")->click();
        dlg.accept();
    }

    Settings s2(path);
    s2.load();
    QCOMPARE(s2.recentMax(), 50);    // reset back to default
    QCOMPARE(s2.autoSave(), false);  // still the mutated value
}

void TestPreferences::themeControlDisabled() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Settings s(dir.filePath("settings.toml"));
    PreferencesDialog dlg(s);
    auto *combo = dlg.findChild<QComboBox *>("themeCombo");
    QVERIFY(combo);
    QCOMPARE(combo->isEnabled(), false);
    QVERIFY(!combo->toolTip().isEmpty());
}

// Evidence capture — runs only when PREF_SHOT_DIR is set; grabs one PNG
// per tab so the pane can be reviewed without a live display. Uses
// QWidget::grab(), which renders regardless of platform (offscreen ok).
void TestPreferences::captureScreenshots() {
    const QByteArray shotDir = qgetenv("PREF_SHOT_DIR");
    if (shotDir.isEmpty())
        QSKIP("PREF_SHOT_DIR not set; skipping screenshot capture");

    QDir().mkpath(QString::fromLocal8Bit(shotDir));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Settings s(dir.filePath("settings.toml"));
    // Populate with a mix of values so the shots show real state.
    s.setOpenFilesIn(OpenFilesIn::NewTab);
    s.setRecentMax(25);
    s.setMlRunOnBattery(true);

    PreferencesDialog dlg(s);
    dlg.setManageModelsCallback([]() {});
    dlg.setResetAllCallback([]() {});
    dlg.resize(560, 360);

    auto *tabs = dlg.findChild<QTabWidget *>("tabWidget");
    QVERIFY(tabs);
    const QString names[] = {QStringLiteral("general"), QStringLiteral("files"),
                             QStringLiteral("machine_learning"),
                             QStringLiteral("advanced")};
    for (int i = 0; i < tabs->count(); ++i) {
        tabs->setCurrentIndex(i);
        dlg.show();
        QApplication::processEvents();
        const QString file =
            QString::fromLocal8Bit(shotDir) + "/pref_" + names[i] + ".png";
        QVERIFY(dlg.grab().save(file));
    }
}

QTEST_MAIN(TestPreferences)
#include "test_preferences.moc"
