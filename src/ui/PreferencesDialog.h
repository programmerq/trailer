#pragma once

#include "settings/Settings.h"

#include <QDialog>
#include <functional>
#include <vector>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QPushButton;
class QSpinBox;
class QToolButton;

namespace trailer {

// Unified Preferences editor (Edit → Preferences…, Ctrl+, / ⌘,).
//
// A tabbed QDialog surfacing every setting that has a wired backend
// today: General, Files, Machine Learning, Advanced. The dialog holds
// a reference to the live Settings object and mutates it only on OK —
// Cancel writes nothing. Setters on Settings do not auto-save (see
// docs/CONVENTIONS.md discrepancy note), so accept() batches every
// changed control value into the Settings object and issues a single
// save().
//
// OK persists only what the user actually changed: a per-control
// baseline is snapshotted whenever the controls are (re)synced from
// Settings, and applyToSettings() writes a setter only when the current
// widget value differs from that baseline. This keeps a clamped display
// (e.g. an out-of-range recent_max) from being written back on an
// untouched OK.
//
// Two scattered menu actions are co-located here as buttons via
// injected callbacks (Manage ML Models…, Reset Trailer Settings…);
// when a callback is unset its button is disabled rather than lying
// about being actionable. See docs/decisions/0001..0002.
//
// DESIGN §2.4.2 reset mandate: every editable control carries an inline,
// right-aligned "Reset to default" revert icon (enabled only when the
// control differs from its default), plus a dialog-level Restore
// Defaults button. Resets change the UI only; nothing persists until OK.
class PreferencesDialog : public QDialog {
    Q_OBJECT

  public:
    explicit PreferencesDialog(Settings &settings, QWidget *parent = nullptr);

    // Wire the "Manage models…" (Machine Learning tab) and "Reset all
    // Trailer settings and data…" (Advanced tab) buttons to the host's
    // existing actions. When a callback is empty the button stays
    // disabled. Safe to call before or after construction.
    void setManageModelsCallback(std::function<void()> cb);
    void setResetAllCallback(std::function<void()> cb);

  signals:
    // Emitted after accept() has written and saved the settings, so the
    // host can re-apply settings that are not read live (e.g. the
    // Recent Files cap).
    void settingsApplied();

  public:
    void accept() override;

  private:
    // Populate every control from the live m_settings and re-snapshot the
    // per-control baseline used by applyToSettings(). Called from the
    // constructor and after any in-dialog reset (including "Reset all…").
    void syncControlsFromSettings();
    // Copy each control value into m_settings via its setter, but only
    // when the value differs from the baseline captured at the last sync.
    void applyToSettings();
    // Revert every editable control to its default value (UI only). The
    // disabled Theme control is intentionally left untouched.
    void restoreEditableDefaults();
    // Enable each per-row reset button only when its control currently
    // differs from that control's default.
    void refreshResetButtons();

    // Register a per-row reset button: wires its click to `onReset` and
    // records `atDefault` so refreshResetButtons() can grey it out when
    // the control already holds its default value.
    QToolButton *makeResetButton(QWidget *parent, const QString &objectName,
                                 std::function<void()> onReset,
                                 std::function<bool()> atDefault);

    Settings &m_settings;
    // A path-less, in-memory Settings (never loaded/saved) read purely as
    // the source of default values for the reset affordances — it can
    // never touch the real settings file.
    Settings m_defaults{QString()};

    QComboBox *m_themeCombo = nullptr;
    QComboBox *m_openFilesInCombo = nullptr;
    QCheckBox *m_restoreWindowsCheck = nullptr;

    QCheckBox *m_autoSaveCheck = nullptr;
    QSpinBox *m_recentMaxSpin = nullptr;

    QCheckBox *m_mlRecognizeTextCheck = nullptr;
    QCheckBox *m_mlPreloadSegCheck = nullptr;
    QCheckBox *m_mlRunOnBatteryCheck = nullptr;
    QPushButton *m_manageModelsButton = nullptr;

    QPushButton *m_resetAllButton = nullptr;

    // Per-control baseline (the value actually placed INTO each widget at
    // the last sync). applyToSettings() writes a setter only when the
    // current widget value diverges from this.
    struct Baseline {
        Theme theme = Theme::System;
        OpenFilesIn openFilesIn = OpenFilesIn::NewWindow;
        bool restoreWindows = true;
        bool autoSave = true;
        int recentMax = 50;
        bool mlRecognizeText = true;
        bool mlPreloadSeg = true;
        bool mlRunOnBattery = false;
    } m_baseline;

    // Reset button + "is this control at its default?" predicate, so a
    // single refreshResetButtons() pass can update every button's enabled
    // state after a change, a sync, or a reset.
    struct ResetEntry {
        QToolButton *button = nullptr;
        std::function<bool()> atDefault;
    };
    std::vector<ResetEntry> m_resets;

    std::function<void()> m_manageModelsCallback;
    std::function<void()> m_resetAllCallback;
};

} // namespace trailer
