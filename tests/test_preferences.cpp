#include "recent/RecentFiles.h"
#include "settings/Settings.h"
#include "ui/PreferencesDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QObject>
#include <QPushButton>
#include <QScopedPointer>
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
    void themeControlEnabled();
    void themeAppliesLiveThroughSignal();
    void cancelPreservesExistingFile();
    void okPreservesMachineState();
    void untouchedRecentMaxNotClamped();
    void themePreservedThroughOk();
    void openFilesInEachValueRoundTrips();
    void liveApplyRecentMaxThroughSignal();
    void restartHintRendersForRestartRequiredKey();
    void captureScreenshots();
};

// Write a fully-populated settings.toml (including machine-state the
// dialog never exposes) so the safety tests can prove nothing is lost.
static void writePopulatedSettings(const QString &path) {
    Settings s(path);
    // Exposed settings, deliberately non-default / out-of-range.
    s.setOpenFilesIn(OpenFilesIn::SameWindow);
    s.setAutoSave(false);
    s.setRecentMax(500); // out of the dialog's 1..200 range → clamps on display
    s.setTheme(Theme::Dark);
    // Machine state the dialog does NOT surface.
    s.setSessionOpenFiles({QStringLiteral("/tmp/a.pdf"), QStringLiteral("/tmp/b.pdf")});
    s.setLastSaveDir(QStringLiteral("/tmp/saves"));
    s.setFirstUseAcknowledged(QStringLiteral("redaction"), true);
    s.save();
}

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

// The Theme control is now live-wired (docs/decision-records/
// 2026-07-20-theme-applies-live.md, superseding docs/decisions/0004): the
// combo is enabled and the old "not applied yet" helper label is gone.
void TestPreferences::themeControlEnabled() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Settings s(dir.filePath("settings.toml"));
    PreferencesDialog dlg(s);
    auto *combo = dlg.findChild<QComboBox *>("themeCombo");
    QVERIFY(combo);
    QVERIFY(combo->isEnabled());
    // The obsolete disabled-state helper label must be gone.
    QVERIFY(!dlg.findChild<QLabel *>("themeHelpLabel"));
    // All three modes are offered.
    QCOMPARE(combo->count(), 3);
}

// Live-apply proof for theme. Like recent_max, theme is not read live by
// its consumer — it takes effect without a restart only because accept()
// emits settingsApplied and the host re-applies it (MainWindow re-applies
// via Application::applyTheme). This drives the real signal end to end:
// change the combo, accept, and a connected consumer sees the new theme
// and its mapped colour scheme — no dialog reconstruction, no restart. If
// accept() stopped emitting the signal, the consumer would keep the stale
// theme and this fails.
void TestPreferences::themeAppliesLiveThroughSignal() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    Settings s(dir.filePath("settings.toml"));
    QCOMPARE(s.theme(), Theme::System); // default

    PreferencesDialog dlg(s);
    QObject ctx;
    Qt::ColorScheme applied = Qt::ColorScheme::Unknown;
    int applyCount = 0;
    QObject::connect(&dlg, &PreferencesDialog::settingsApplied, &ctx,
                     [&applied, &applyCount, &s]() {
                         applied = colorSchemeFor(s.theme());
                         ++applyCount;
                     });

    auto *combo = dlg.findChild<QComboBox *>("themeCombo");
    combo->setCurrentIndex(combo->findData(static_cast<int>(Theme::Dark)));
    dlg.accept(); // applyToSettings -> save -> emit settingsApplied

    QCOMPARE(applyCount, 1);
    QCOMPARE(s.theme(), Theme::Dark);
    QCOMPARE(applied, Qt::ColorScheme::Dark);
}

void TestPreferences::cancelPreservesExistingFile() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("settings.toml");
    writePopulatedSettings(path);

    {
        Settings s(path);
        s.load();
        PreferencesDialog dlg(s);
        // Mutate a couple of exposed controls, then cancel.
        dlg.findChild<QComboBox *>("openFilesInCombo")
            ->setCurrentIndex(dlg.findChild<QComboBox *>("openFilesInCombo")
                                  ->findData(static_cast<int>(OpenFilesIn::NewTab)));
        dlg.findChild<QCheckBox *>("autoSaveCheck")->setChecked(true);
        dlg.reject();
    }

    // Every original value — exposed and machine-state — must be intact.
    Settings s2(path);
    s2.load();
    QCOMPARE(s2.openFilesIn(), OpenFilesIn::SameWindow);
    QCOMPARE(s2.autoSave(), false);
    QCOMPARE(s2.recentMax(), 500);
    QCOMPARE(s2.theme(), Theme::Dark);
    QCOMPARE(s2.sessionOpenFiles().size(), 2);
    QCOMPARE(s2.lastSaveDir(), QStringLiteral("/tmp/saves"));
    QCOMPARE(s2.firstUseAcknowledged(QStringLiteral("redaction")), true);
}

void TestPreferences::okPreservesMachineState() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("settings.toml");
    writePopulatedSettings(path);

    {
        Settings s(path);
        s.load();
        PreferencesDialog dlg(s);
        // Change exactly one exposed setting, then accept.
        dlg.findChild<QCheckBox *>("autoSaveCheck")->setChecked(true);
        dlg.accept();
    }

    Settings s2(path);
    s2.load();
    // (a) machine-state survived the whole-file rewrite...
    QCOMPARE(s2.sessionOpenFiles().size(), 2);
    QCOMPARE(s2.lastSaveDir(), QStringLiteral("/tmp/saves"));
    QCOMPARE(s2.firstUseAcknowledged(QStringLiteral("redaction")), true);
    // (b) ...and the one changed exposed setting stuck.
    QCOMPARE(s2.autoSave(), true);
}

void TestPreferences::untouchedRecentMaxNotClamped() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("settings.toml");
    writePopulatedSettings(path); // recent_max = 500

    {
        Settings s(path);
        s.load();
        PreferencesDialog dlg(s);
        // Accept WITHOUT touching the recent-max spinbox (which displays a
        // clamped 200); the untouched field must not be written back.
        dlg.accept();
    }

    Settings s2(path);
    s2.load();
    QCOMPARE(s2.recentMax(), 500);
}

void TestPreferences::themePreservedThroughOk() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("settings.toml");
    writePopulatedSettings(path); // theme = Dark

    {
        Settings s(path);
        s.load();
        PreferencesDialog dlg(s);
        // Change an unrelated setting, accept.
        dlg.findChild<QCheckBox *>("mlRunOnBatteryCheck")->setChecked(true);
        dlg.accept();
    }

    Settings s2(path);
    s2.load();
    QCOMPARE(s2.theme(), Theme::Dark);
    QCOMPARE(s2.mlRunOnBattery(), true);
}

void TestPreferences::openFilesInEachValueRoundTrips() {
    const OpenFilesIn values[] = {OpenFilesIn::NewTab, OpenFilesIn::NewWindow,
                                  OpenFilesIn::SameWindow};
    for (const OpenFilesIn value : values) {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("settings.toml");

        {
            Settings s(path);
            PreferencesDialog dlg(s);
            auto *combo = dlg.findChild<QComboBox *>("openFilesInCombo");
            combo->setCurrentIndex(combo->findData(static_cast<int>(value)));
            dlg.accept();
        }

        Settings s2(path);
        s2.load();
        QCOMPARE(s2.openFilesIn(), value);
    }
}

// Live-apply proof for a "live via apply-signal" key. recent_max is
// cached at startup, so it only takes effect without a restart because
// accept() emits settingsApplied and the host RE-READS the getter in the
// slot (mirrors MainWindow.cpp:3272). This drives the real signal end to
// end: change the spinbox, accept, and the connected consumer's cap
// updates live — no reconstruction. If accept() stops emitting the
// signal (or the slot caches the value at connect time instead of
// re-reading), the consumer keeps the stale cap and this fails.
void TestPreferences::liveApplyRecentMaxThroughSignal() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    Settings s(dir.filePath("settings.toml"));
    s.setRecentMax(50);

    RecentFiles recent(dir.filePath("recent.json"));
    recent.setMaxEntries(s.recentMax()); // startup snapshot: 50
    QCOMPARE(recent.maxEntries(), 50);

    PreferencesDialog dlg(s);
    QObject ctx; // connection context so the lambda is torn down safely
    QObject::connect(&dlg, &PreferencesDialog::settingsApplied, &ctx,
                     [&recent, &s]() { recent.setMaxEntries(s.recentMax()); });

    dlg.findChild<QSpinBox *>("recentMaxSpin")->setValue(7);
    dlg.accept(); // applyToSettings -> save -> emit settingsApplied

    // Re-applied live, without rebuilding `recent`.
    QCOMPARE(s.recentMax(), 7);
    QCOMPARE(recent.maxEntries(), 7);
}

// Hint-render proof. The restart-hint factory is dormant for every Live
// key (all of them today) and only produces a label for a RestartRequired
// key. Drive it with the RestartRequired probe key and with a real Live
// key to prove both branches. If makeRestartHint stopped honouring the
// classification (e.g. always returned nullptr), the RestartRequired arm
// fails here.
void TestPreferences::restartHintRendersForRestartRequiredKey() {
    QScopedPointer<QLabel> hint(PreferencesDialog::makeRestartHint(SettingsKeys::RestartProbe));
    QVERIFY(!hint.isNull());
    QCOMPARE(hint->objectName(), QStringLiteral("restartHint"));
    QVERIFY(!hint->text().isEmpty());

    // A Live key renders nothing — no visible change in today's dialog.
    QScopedPointer<QLabel> none(PreferencesDialog::makeRestartHint(SettingsKeys::RecentMax));
    QVERIFY(none.isNull());
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
