#include "settings/Settings.h"

#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <toml++/toml.h>

#include <string>
#include <vector>

using namespace trailer;

class TestSettingsVolatility : public QObject {
    Q_OBJECT
  private slots:
    void registryCoversEveryPersistedKey();
    void allCurrentKeysAreLive();
    void unknownKeyIsUnclassified();
};

// Flatten a parsed settings.toml to the dotted leaf keys it actually
// persists — the same "table.table.leaf" shape SettingsKeys uses. Arrays
// (e.g. session.open_files) are leaves: the array key is what's
// persisted, so we do not descend into its elements.
static void collectLeafKeys(const toml::table &tbl, const std::string &prefix,
                            std::vector<std::string> &out) {
    for (const auto &[k, node] : tbl) {
        const std::string path =
            prefix.empty() ? std::string(k.str()) : prefix + "." + std::string(k.str());
        if (const toml::table *sub = node.as_table()) {
            collectLeafKeys(*sub, path, out);
        } else {
            out.push_back(path);
        }
    }
}

// The core guard against the restart-surprise trap: persist a Settings
// with EVERY section populated, then assert every leaf key the file
// actually contains resolves in the volatility registry. A future dev
// who adds a persisted key without registering it fails here loudly,
// rather than shipping a silently restart-only setting.
void TestSettingsVolatility::registryCoversEveryPersistedKey() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("settings.toml");

    Settings s(path);
    // Drive every persisted surface so save() emits every key, including
    // the optional general.last_save_dir and a dynamic first_use.* entry.
    s.setTheme(Theme::Dark);
    s.setOpenFilesIn(OpenFilesIn::SameWindow);
    s.setLastSaveDir(QStringLiteral("/tmp/saves"));
    s.setAutoSave(false);
    s.setRecentMax(20);
    s.setRestorePreviousWindows(false);
    s.setSessionOpenFiles({QStringLiteral("/tmp/a.pdf"), QStringLiteral("/tmp/b.pdf")});
    s.setFirstUseAcknowledged(QStringLiteral("redaction"), true);
    s.setMlRecognizeTextInBackground(false);
    s.setMlPreloadSegmentationOnToolActivation(false);
    s.setMlRunOnBattery(true);
    s.setUpdatesAutoCheckEnabled(true);
    s.setUpdatesChannel(QStringLiteral("nightly"));
    s.setUpdatesLastCheckedUtc(QStringLiteral("2026-07-30T10:00:00Z"));
    s.save();
    QVERIFY(QFile::exists(path));

    // toml++ is built with exceptions here, so parse_file returns a
    // toml::table directly (throwing on malformed input); we just wrote
    // this file, so a parse failure is itself a test failure.
    std::vector<std::string> keys;
    try {
        const toml::table parsed = toml::parse_file(path.toStdString());
        collectLeafKeys(parsed, std::string(), keys);
    } catch (const toml::parse_error &e) {
        QFAIL(e.what());
    }
    // Non-vacuous: if save() ever stops writing keys this guard is moot.
    QVERIFY(keys.size() >= 8);

    for (const std::string &key : keys) {
        const QString qkey = QString::fromStdString(key);
        const std::optional<Settings::Volatility> v = Settings::volatilityOf(qkey);
        QVERIFY2(v.has_value(),
                 qPrintable(QStringLiteral("persisted key missing from the volatility "
                                           "registry (see docs/CONVENTIONS.md §15): %1")
                                .arg(qkey)));
    }
}

// The migration inventory: every key that exists today is classified
// Live, so introducing the machinery changes no user-visible behaviour.
void TestSettingsVolatility::allCurrentKeysAreLive() {
    const QLatin1StringView liveKeys[] = {
        SettingsKeys::Theme,
        SettingsKeys::OpenFilesIn,
        SettingsKeys::LastSaveDir,
        SettingsKeys::AutoSave,
        SettingsKeys::RecentMax,
        SettingsKeys::RestorePreviousWindows,
        SettingsKeys::SessionOpenFiles,
        SettingsKeys::MlRecognizeTextInBackground,
        SettingsKeys::MlPreloadSegmentationOnToolActivation,
        SettingsKeys::MlRunOnBattery,
        SettingsKeys::UpdatesAutoCheckEnabled,
        SettingsKeys::UpdatesChannel,
        SettingsKeys::UpdatesLastCheckedUtc,
    };
    for (const QLatin1StringView key : liveKeys) {
        const std::optional<Settings::Volatility> v = Settings::volatilityOf(key);
        QVERIFY2(v.has_value(), qPrintable(QString(key)));
        QVERIFY2(*v == Settings::Volatility::Live, qPrintable(QString(key)));
    }
    // The dynamic first_use.* group is Live via its prefix rule.
    QVERIFY(Settings::volatilityOf(QStringLiteral("first_use.redaction")) ==
            Settings::Volatility::Live);
    // The dormant test seam is the one RestartRequired classification.
    QVERIFY(Settings::volatilityOf(SettingsKeys::RestartProbe) ==
            Settings::Volatility::RestartRequired);
}

void TestSettingsVolatility::unknownKeyIsUnclassified() {
    // A key nobody registered resolves to nullopt (and logs) — never a
    // false Live/Restart classification.
    QVERIFY(!Settings::volatilityOf(QStringLiteral("nonsense.not_a_key")).has_value());
}

QTEST_MAIN(TestSettingsVolatility)
#include "test_settings_volatility.moc"
