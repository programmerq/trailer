#pragma once

#include "settings/Settings.h"

#include <QDialog>
#include <functional>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QPushButton;
class QSpinBox;

namespace trailer {

// Unified Preferences editor (Edit → Preferences…, Ctrl+, / ⌘,).
//
// A tabbed QDialog surfacing every setting that has a wired backend
// today: General, Files, Machine Learning, Advanced. The dialog holds
// a reference to the live Settings object and mutates it only on OK —
// Cancel writes nothing. Setters on Settings do not auto-save (see
// docs/CONVENTIONS.md discrepancy note), so accept() batches every
// control value into the Settings object and issues a single save().
//
// Two scattered menu actions are co-located here as buttons via
// injected callbacks (Manage ML Models…, Reset Trailer Settings…);
// when a callback is unset its button is disabled rather than lying
// about being actionable. See docs/decisions/0001..0002.
//
// DESIGN §2.4.2 reset mandate: every editable control carries an inline
// "Reset to default" affordance, plus a dialog-level Restore Defaults
// button. Resets change the UI only; nothing persists until OK.
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
    // Populate every control from `src`.
    void loadFromSettings(const Settings &src);
    // Copy every control value into m_settings via its setters.
    void applyToSettings();
    // Revert every editable control to its default value (UI only). The
    // disabled Theme control is intentionally left untouched.
    void restoreEditableDefaults();

    Settings &m_settings;
    // A default-constructed Settings, read (never loaded/saved) purely
    // as the source of default values for the reset affordances.
    Settings m_defaults;

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

    std::function<void()> m_manageModelsCallback;
    std::function<void()> m_resetAllCallback;
};

} // namespace trailer
