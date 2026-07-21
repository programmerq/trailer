#pragma once

#include "platform/ScreenCaptureBackend.h"

#include <QAnyStringView>
#include <QHash>
#include <QLatin1StringView>
#include <QString>
#include <QStringList>
#include <QtCore/qnamespace.h>

#include <optional>

namespace trailer {

// Canonical dotted key paths for every persisted setting. Both the
// live-vs-restart registry (Settings::volatilityOf) and the Preferences
// UI reference these constants to classify a control. load()/save()
// themselves build nested TOML tables from raw literals rather than
// these constants; the anti-drift guarantee is instead
// test_settings_volatility's registryCoversEveryPersistedKey completeness
// test, which saves a fully-populated file and asserts every persisted
// key resolves in the registry — so a key's persisted name and its
// classification cannot drift apart. Nested TOML tables are flattened
// with '.' (e.g. [ml.scheduler].run_on_battery ->
// "ml.scheduler.run_on_battery"), matching how that completeness test
// walks the saved file. See docs/CONVENTIONS.md §15.
namespace SettingsKeys {
inline constexpr QLatin1StringView Theme{"general.theme"};
inline constexpr QLatin1StringView OpenFilesIn{"general.open_files_in"};
inline constexpr QLatin1StringView CaptureBackend{"general.capture_backend"};
inline constexpr QLatin1StringView LastSaveDir{"general.last_save_dir"};
inline constexpr QLatin1StringView AutoSave{"files.auto_save"};
inline constexpr QLatin1StringView RecentMax{"files.recent_max"};
inline constexpr QLatin1StringView RestorePreviousWindows{"session.restore_previous_windows"};
inline constexpr QLatin1StringView SessionOpenFiles{"session.open_files"};
inline constexpr QLatin1StringView MlRecognizeTextInBackground{
    "ml.scheduler.recognize_text_in_background"};
inline constexpr QLatin1StringView MlPreloadSegmentationOnToolActivation{
    "ml.scheduler.preload_segmentation_on_tool_activation"};
inline constexpr QLatin1StringView MlRunOnBattery{"ml.scheduler.run_on_battery"};
// Dynamic key group: arbitrary boolean acknowledgements are persisted
// under the [first_use] table with hand-chosen leaf names, so the
// registry classifies the whole group by this prefix rather than key
// by key.
inline constexpr QLatin1StringView FirstUsePrefix{"first_use."};
// Test seam: a key that no control ever persists, registered
// RestartRequired so the restart-hint mechanism can be exercised without
// reclassifying a real (Live) key. Never written to settings.toml.
inline constexpr QLatin1StringView RestartProbe{"__test.restart_probe"};
} // namespace SettingsKeys

// API: these two enums are serialised by their *string* mapping in
// settings.toml ("new_tab" / "new_window" / "same_window"; "system" /
// "light" / "dark"), not by ordinal. The C++ enum may be renumbered
// freely; what must never change is the string a value maps to in
// Settings::openFilesInToString / themeToString and the parser pair.
// Adding new values means adding new string mappings; removing a
// value silently breaks any settings.toml that names it.
enum class OpenFilesIn {
    NewTab,
    NewWindow,
    SameWindow,
};

enum class Theme {
    System,
    Light,
    Dark,
};

class Settings {
  public:
    // Whether a change to a setting takes effect while Trailer is running
    // (Live) or only after a restart (RestartRequired). Every persisted
    // key is classified in the registry backing volatilityOf(); a
    // RestartRequired key surfaces a restart hint in Preferences. Today
    // every key is Live, so the machinery is dormant until the first
    // RestartRequired key is added. See docs/CONVENTIONS.md §15.
    enum class Volatility { Live, RestartRequired };

    // Classify a persisted settings key (a dotted path from
    // SettingsKeys, e.g. "files.recent_max"). Returns std::nullopt for a
    // key that is not in the registry — an unregistered persisted key is
    // exactly the restart-surprise trap, and test_settings_volatility
    // fails loudly on it. Dynamic key groups (first_use.*) are matched by
    // prefix.
    static std::optional<Volatility> volatilityOf(QAnyStringView key);

    Settings();
    explicit Settings(QString filePath);

    void load();
    void save() const;

    Theme theme() const { return m_theme; }
    void setTheme(Theme value);

    OpenFilesIn openFilesIn() const { return m_openFilesIn; }
    void setOpenFilesIn(OpenFilesIn value);

    // Which native still-capture backend the macOS screenshot flow drives.
    // Persisted as a string under [general].capture_backend. Defaults to
    // the long-standing Screencapture (/usr/sbin/screencapture) path — the
    // ScreenCaptureKit picker backend is opt-in and only takes effect on
    // macOS 14+ once selected AND validated on-device (see ADR 0015).
    // RestartRequired: the backend is resolved at the capture call site, so
    // a change is picked up cleanly only on a fresh run.
    trailer::CaptureBackend captureBackend() const { return m_captureBackend; }
    void setCaptureBackend(trailer::CaptureBackend value);

    bool autoSave() const { return m_autoSave; }
    void setAutoSave(bool value);

    int recentMax() const { return m_recentMax; }
    void setRecentMax(int value);

    // When true, Trailer reopens whatever files were open at the time
    // of the last quit (macOS-style "quit and keep windows" across
    // every platform). When the user launches Trailer with explicit
    // file arguments, the session list is ignored — those files
    // override the persisted set. Default: on.
    bool restorePreviousWindows() const { return m_restorePreviousWindows; }
    void setRestorePreviousWindows(bool value);

    // List of file paths that were open at the last aboutToQuit.
    // Persisted under [session].open_files; the launch path consults
    // this only when restorePreviousWindows() is true AND no CLI
    // files were supplied.
    QStringList sessionOpenFiles() const { return m_sessionOpenFiles; }
    void setSessionOpenFiles(const QStringList &value);

    // Directory the user last saved into. Used to seed Save-As file
    // dialogs so successive saves of related documents land in the
    // same folder rather than always defaulting to ~/Documents. The
    // value is best-effort — empty string means "use platform default".
    QString lastSaveDir() const { return m_lastSaveDir; }
    void setLastSaveDir(const QString &value);

    // [ml.scheduler] knobs (see PHILOSOPHY: don't chew battery
    // greedily). Read by MlScheduler when deciding whether to admit
    // speculative work.
    //
    //   mlRecognizeTextInBackground — run OCR on currently-visible
    //     pages so the search index is warm. Off => OCR only fires
    //     on explicit user action.
    //   mlPreloadSegmentationOnToolActivation — when the user picks
    //     Instant Alpha / Smart Lasso, kick off the MobileSAM
    //     encoder for the current image so the first click is fast.
    //   mlRunOnBattery — when on battery, run *all* priorities. Off
    //     (the default) restricts to UserAction only; Prefetch /
    //     Idle / VisiblePage submissions get pre-cancelled.
    bool mlRecognizeTextInBackground() const { return m_mlRecognizeTextInBackground; }
    void setMlRecognizeTextInBackground(bool value);

    bool mlPreloadSegmentationOnToolActivation() const {
        return m_mlPreloadSegmentationOnToolActivation;
    }
    void setMlPreloadSegmentationOnToolActivation(bool value);

    bool mlRunOnBattery() const { return m_mlRunOnBattery; }
    void setMlRunOnBattery(bool value);

    // Whether the user has seen the one-time "redaction is not
    // defence-grade" warning (DESIGN §6.11.6). True = do not show
    // again; false = show on next redaction attempt. Convenience
    // wrapper around the generic firstUseAcknowledged store.
    bool redactionWarningAcknowledged() const {
        return firstUseAcknowledged(QStringLiteral("redaction"));
    }
    void setRedactionWarningAcknowledged(bool value) {
        setFirstUseAcknowledged(QStringLiteral("redaction"), value);
    }

    // Generic key→bool storage. Originally added for one-time prompts
    // (e.g. the redaction warning); now also reused for the
    // never-download policy bits per ML model under keys of the form
    // `ml_never_download_<model_key>`. Keys are written under the
    // [first_use] table in settings.toml; unknown keys load as false.
    // Treat the table name as legacy — the storage is a generic bool bag.
    bool firstUseAcknowledged(const QString &key) const {
        return m_firstUseFlags.value(key, false);
    }
    void setFirstUseAcknowledged(const QString &key, bool value);

    QString filePath() const { return m_filePath; }

  private:
    QString m_filePath;
    Theme m_theme = Theme::System;
    // Window-per-file is the default after the 2026-04-24 HITL review
    // ("tabs are mostly in the way"). Tabs remain available as an opt-in
    // by setting open_files_in = "new_tab" in settings.toml.
    OpenFilesIn m_openFilesIn = OpenFilesIn::NewWindow;
    // Safe default: the existing screencapture path, so runtime behaviour is
    // unchanged until the picker backend is validated on-device.
    trailer::CaptureBackend m_captureBackend = trailer::CaptureBackend::Screencapture;
    bool m_autoSave = true;
    int m_recentMax = 50;
    // macOS-style "pick up where you left off" default — on. The
    // Settings → General preferences UI exposes a checkbox to flip
    // this; users who prefer a clean launch each time turn it off.
    bool m_restorePreviousWindows = true;
    QStringList m_sessionOpenFiles;
    QString m_lastSaveDir;
    QHash<QString, bool> m_firstUseFlags;
    // [ml.scheduler] defaults. Background recognition + tool-
    // activation preload are *on* so the app feels fast; running
    // speculative work on battery is *off* so the laptop doesn't
    // get hot on a coffee-shop trip. Flip via the eventual
    // Preferences UI; persisted under [ml.scheduler] in settings.toml.
    bool m_mlRecognizeTextInBackground = true;
    bool m_mlPreloadSegmentationOnToolActivation = true;
    bool m_mlRunOnBattery = false;
};

QString themeToString(Theme value);
Theme themeFromString(const QString &value);

// Map a Theme to the Qt colour scheme applied at runtime via
// QStyleHints::setColorScheme (Qt 6.8+). This is the single translation
// point between Trailer's persisted theme and Qt's live theming:
//   System → Qt::ColorScheme::Unknown — hand control back to Qt so it
//     tracks the OS appearance and flips live when the OS does.
//   Light  → Qt::ColorScheme::Light  (force light regardless of OS)
//   Dark   → Qt::ColorScheme::Dark   (force dark regardless of OS)
// A free function (not a member, no QApplication needed) so the mapping
// is unit-testable in isolation. See Application::applyTheme and
// docs/decision-records/2026-07-20-theme-applies-live.md.
Qt::ColorScheme colorSchemeFor(Theme value);

QString openFilesInToString(OpenFilesIn value);
OpenFilesIn openFilesInFromString(const QString &value);

} // namespace trailer
