#include "PreferencesDialog.h"

#include "update/UpdateManager.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>

namespace trailer {

namespace {

// Recent-files cap bounds. The backing field was previously unclamped
// (Settings.h), so the Preferences UI is where a sane range is imposed.
// RecentFiles::setMaxEntries maps anything < 1 up to 1, so the smallest
// honest value is 1 (there is no "disable the recent list" state); 200
// is a generous upper bound.
constexpr int kRecentMaxMin = 1;
constexpr int kRecentMaxMax = 200;

// Compact square footprint for the inline revert (↺) tool-buttons, so
// every row shares a consistent right edge.
constexpr int kResetButtonSize = 22;

// Revert glyph (↺) used as flat tool-button text — dependency-free, no
// icon theme required.
const QChar kRevertGlyph(0x21BA);

void selectComboByData(QComboBox *combo, int value) {
    const int index = combo->findData(value);
    if (index >= 0)
        combo->setCurrentIndex(index);
}

void selectComboByData(QComboBox *combo, const QString &value) {
    const int index = combo->findData(value);
    if (index >= 0)
        combo->setCurrentIndex(index);
}

} // namespace

QLabel *PreferencesDialog::makeRestartHint(QAnyStringView key, QWidget *parent) {
    // Live (or unregistered) keys get no hint. std::optional's comparison
    // with the enum value is false for std::nullopt, so an unknown key
    // renders nothing rather than a spurious hint.
    if (Settings::volatilityOf(key) != Settings::Volatility::RestartRequired)
        return nullptr;

    auto *hint = new QLabel(tr("Requires restart to take effect"), parent);
    hint->setObjectName(QStringLiteral("restartHint"));
    hint->setWordWrap(true);
    // Match the muted helper-label style used elsewhere in this dialog
    // (see the Theme helper): slightly smaller, disabled-text colour.
    QFont hintFont = hint->font();
    hintFont.setPointSizeF(hintFont.pointSizeF() * 0.9);
    hint->setFont(hintFont);
    QPalette hintPalette = hint->palette();
    hintPalette.setColor(QPalette::WindowText,
                         hintPalette.color(QPalette::Disabled, QPalette::WindowText));
    hint->setPalette(hintPalette);
    return hint;
}

QToolButton *PreferencesDialog::makeResetButton(QWidget *parent, const QString &objectName,
                                                std::function<void()> onReset,
                                                std::function<bool()> atDefault) {
    auto *reset = new QToolButton(parent);
    reset->setObjectName(objectName);
    reset->setText(kRevertGlyph);
    reset->setAccessibleName(tr("Reset to default"));
    reset->setToolTip(tr("Reset to default"));
    reset->setAutoRaise(true);
    reset->setFocusPolicy(Qt::NoFocus);
    reset->setFixedSize(kResetButtonSize, kResetButtonSize);
    connect(reset, &QToolButton::clicked, this,
            [onReset = std::move(onReset)]() { onReset(); });
    m_resets.push_back({reset, std::move(atDefault)});
    return reset;
}

PreferencesDialog::PreferencesDialog(Settings &settings, QWidget *parent)
    : QDialog(parent), m_settings(settings) {
    setWindowTitle(tr("Preferences"));

    auto *outer = new QVBoxLayout(this);
    auto *tabs = new QTabWidget(this);
    m_tabs = tabs;
    tabs->setObjectName(QStringLiteral("tabWidget"));
    // Tabs grow with settings: only add a tab once it has >=1 real, wired,
    // operable control — never an empty placeholder. Each addTab() below
    // gates on that (General / Files / Machine Learning / Advanced all
    // carry live controls today; a Forms tab, for instance, waits until a
    // Forms setting is wired). The rule is locked by
    // tests/uat/test_uat_preferences.cpp
    // (uat_pref_020_everyVisibleTabHasEnabledOperableControl) and noted in
    // docs/CONVENTIONS.md §15.

    // A labelled row whose field control sits next to a compact,
    // right-aligned revert icon so every row shares one right edge.
    // `resetName` is the stable objectName the tests drive; `onReset`
    // reverts just this control; `atDefault` greys the icon out when the
    // control already holds its default.
    const auto addFieldRow = [this](QFormLayout *form, const QString &label,
                                    QWidget *control, const QString &resetName,
                                    std::function<void()> onReset,
                                    std::function<bool()> atDefault, QAnyStringView settingsKey) {
        auto *rowWidget = new QWidget(form->parentWidget());
        auto *rowLayout = new QHBoxLayout(rowWidget);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->addWidget(control, 1);
        // A RestartRequired key appends a muted hint between the control
        // and its revert icon. Live keys (all of them today) add nothing.
        if (QLabel *hint = makeRestartHint(settingsKey, rowWidget))
            rowLayout->addWidget(hint, 0);
        rowLayout->addWidget(makeResetButton(rowWidget, resetName, std::move(onReset),
                                             std::move(atDefault)),
                             0);
        form->addRow(label, rowWidget);
    };

    // A self-labelled checkbox that spans the whole row: [checkbox text]
    // + stretch + revert icon. No separate left label column, so the
    // indicator never floats mid-row.
    const auto addCheckRow = [this](QFormLayout *form, QCheckBox *check,
                                    const QString &resetName, std::function<void()> onReset,
                                    std::function<bool()> atDefault, QAnyStringView settingsKey) {
        auto *rowWidget = new QWidget(form->parentWidget());
        auto *rowLayout = new QHBoxLayout(rowWidget);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->addWidget(check, 1);
        // A RestartRequired key appends a muted hint next to the checkbox.
        // Live keys (all of them today) add nothing.
        if (QLabel *hint = makeRestartHint(settingsKey, rowWidget))
            rowLayout->addWidget(hint, 0);
        rowLayout->addStretch(0);
        rowLayout->addWidget(makeResetButton(rowWidget, resetName, std::move(onReset),
                                             std::move(atDefault)),
                             0);
        form->addRow(rowWidget);
    };

    // ---- General ---------------------------------------------------
    {
        auto *page = new QWidget(this);
        auto *form = new QFormLayout(page);

        m_themeCombo = new QComboBox(page);
        m_themeCombo->setObjectName(QStringLiteral("themeCombo"));
        m_themeCombo->addItem(tr("System"), static_cast<int>(Theme::System));
        m_themeCombo->addItem(tr("Light"), static_cast<int>(Theme::Light));
        m_themeCombo->addItem(tr("Dark"), static_cast<int>(Theme::Dark));
        // Live control: the selection is applied app-wide on OK via
        // Application::applyTheme (System follows the OS; Light/Dark force
        // the scheme), so the combo is enabled and honest — no "not applied
        // yet" note. Supersedes docs/decisions/0004 (see
        // docs/decision-records/2026-07-20-theme-applies-live.md). Theme has
        // no per-row revert control by design; reserve the reset-column
        // width so its right edge lines up with the resettable rows below.
        auto *themeField = new QWidget(page);
        auto *themeRow = new QHBoxLayout(themeField);
        themeRow->setContentsMargins(0, 0, 0, 0);
        themeRow->addWidget(m_themeCombo, 1);
        themeRow->addSpacing(kResetButtonSize);

        form->addRow(tr("Theme"), themeField);

        m_openFilesInCombo = new QComboBox(page);
        m_openFilesInCombo->setObjectName(QStringLiteral("openFilesInCombo"));
        m_openFilesInCombo->addItem(tr("New tab"), static_cast<int>(OpenFilesIn::NewTab));
        m_openFilesInCombo->addItem(tr("New window"), static_cast<int>(OpenFilesIn::NewWindow));
        m_openFilesInCombo->addItem(tr("Same window"), static_cast<int>(OpenFilesIn::SameWindow));
        connect(m_openFilesInCombo, &QComboBox::currentIndexChanged, this,
                &PreferencesDialog::refreshResetButtons);
        addFieldRow(
            form, tr("Open files in"), m_openFilesInCombo,
            QStringLiteral("reset_openFilesIn"),
            [this]() {
                selectComboByData(m_openFilesInCombo,
                                  static_cast<int>(m_defaults.openFilesIn()));
            },
            [this]() {
                return m_openFilesInCombo->currentData().toInt() ==
                       static_cast<int>(m_defaults.openFilesIn());
            },
            SettingsKeys::OpenFilesIn);

        m_restoreWindowsCheck = new QCheckBox(tr("Restore previous windows on launch"), page);
        m_restoreWindowsCheck->setObjectName(QStringLiteral("restoreWindowsCheck"));
        connect(m_restoreWindowsCheck, &QCheckBox::toggled, this,
                &PreferencesDialog::refreshResetButtons);
        addCheckRow(
            form, m_restoreWindowsCheck, QStringLiteral("reset_restoreWindows"),
            [this]() { m_restoreWindowsCheck->setChecked(m_defaults.restorePreviousWindows()); },
            [this]() {
                return m_restoreWindowsCheck->isChecked() == m_defaults.restorePreviousWindows();
            },
            SettingsKeys::RestorePreviousWindows);

        tabs->addTab(page, tr("General"));
    }

    // ---- Files -----------------------------------------------------
    {
        auto *page = new QWidget(this);
        auto *form = new QFormLayout(page);

        m_autoSaveCheck = new QCheckBox(tr("Auto-save changes"), page);
        m_autoSaveCheck->setObjectName(QStringLiteral("autoSaveCheck"));
        connect(m_autoSaveCheck, &QCheckBox::toggled, this,
                &PreferencesDialog::refreshResetButtons);
        addCheckRow(
            form, m_autoSaveCheck, QStringLiteral("reset_autoSave"),
            [this]() { m_autoSaveCheck->setChecked(m_defaults.autoSave()); },
            [this]() { return m_autoSaveCheck->isChecked() == m_defaults.autoSave(); },
            SettingsKeys::AutoSave);

        m_recentMaxSpin = new QSpinBox(page);
        m_recentMaxSpin->setObjectName(QStringLiteral("recentMaxSpin"));
        m_recentMaxSpin->setRange(kRecentMaxMin, kRecentMaxMax);
        connect(m_recentMaxSpin, &QSpinBox::valueChanged, this,
                &PreferencesDialog::refreshResetButtons);
        addFieldRow(
            form, tr("Recent files to remember"), m_recentMaxSpin,
            QStringLiteral("reset_recentMax"),
            [this]() { m_recentMaxSpin->setValue(m_defaults.recentMax()); },
            [this]() { return m_recentMaxSpin->value() == m_defaults.recentMax(); },
            SettingsKeys::RecentMax);

        tabs->addTab(page, tr("Files"));
    }

    // ---- Machine Learning ------------------------------------------
    {
        auto *page = new QWidget(this);
        auto *form = new QFormLayout(page);

        m_mlRecognizeTextCheck = new QCheckBox(tr("Recognize text in the background"), page);
        m_mlRecognizeTextCheck->setObjectName(QStringLiteral("mlRecognizeTextCheck"));
        connect(m_mlRecognizeTextCheck, &QCheckBox::toggled, this,
                &PreferencesDialog::refreshResetButtons);
        addCheckRow(
            form, m_mlRecognizeTextCheck, QStringLiteral("reset_mlRecognizeText"),
            [this]() {
                m_mlRecognizeTextCheck->setChecked(m_defaults.mlRecognizeTextInBackground());
            },
            [this]() {
                return m_mlRecognizeTextCheck->isChecked() ==
                       m_defaults.mlRecognizeTextInBackground();
            },
            SettingsKeys::MlRecognizeTextInBackground);

        m_mlPreloadSegCheck = new QCheckBox(tr("Preload segmentation on tool activation"), page);
        m_mlPreloadSegCheck->setObjectName(QStringLiteral("mlPreloadSegCheck"));
        connect(m_mlPreloadSegCheck, &QCheckBox::toggled, this,
                &PreferencesDialog::refreshResetButtons);
        addCheckRow(
            form, m_mlPreloadSegCheck, QStringLiteral("reset_mlPreloadSeg"),
            [this]() {
                m_mlPreloadSegCheck->setChecked(
                    m_defaults.mlPreloadSegmentationOnToolActivation());
            },
            [this]() {
                return m_mlPreloadSegCheck->isChecked() ==
                       m_defaults.mlPreloadSegmentationOnToolActivation();
            },
            SettingsKeys::MlPreloadSegmentationOnToolActivation);

        m_mlRunOnBatteryCheck = new QCheckBox(tr("Run ML on battery power"), page);
        m_mlRunOnBatteryCheck->setObjectName(QStringLiteral("mlRunOnBatteryCheck"));
        connect(m_mlRunOnBatteryCheck, &QCheckBox::toggled, this,
                &PreferencesDialog::refreshResetButtons);
        addCheckRow(
            form, m_mlRunOnBatteryCheck, QStringLiteral("reset_mlRunOnBattery"),
            [this]() { m_mlRunOnBatteryCheck->setChecked(m_defaults.mlRunOnBattery()); },
            [this]() {
                return m_mlRunOnBatteryCheck->isChecked() == m_defaults.mlRunOnBattery();
            },
            SettingsKeys::MlRunOnBattery);

        m_manageModelsButton = new QPushButton(tr("Manage models…"), page);
        m_manageModelsButton->setObjectName(QStringLiteral("manageModelsButton"));
        m_manageModelsButton->setEnabled(false); // enabled once a callback is set
        // Defensive: in the shipped path a callback is always wired, but if
        // one is never set the button stays disabled — explain why rather
        // than presenting a dead control with no affordance (latent G3).
        m_manageModelsButton->setToolTip(
            tr("Model management is unavailable in this context."));
        connect(m_manageModelsButton, &QPushButton::clicked, this, [this]() {
            if (m_manageModelsCallback)
                m_manageModelsCallback();
        });
        form->addRow(m_manageModelsButton);

        tabs->addTab(page, tr("Machine Learning"));
    }

    // ---- Updates -----------------------------------------------------
    // See DESIGN.md §6.13 (Updates row added 2026-07-30) and
    // docs/decision-records/2026-07-30-nightly-auto-update-channel.md.
    // Auto-check defaults OFF (PHILOSOPHY: no new outbound network call
    // without an explicit, off-by-default toggle); the channel and
    // auto-check checkbox are ordinary batched-on-OK settings, but Check
    // Now / Download & Install / Install & Relaunch are LIVE actions
    // (like Manage Models…) that call straight into UpdateManager —
    // wired via setUpdateManager(), disabled with an explanatory tooltip
    // until then (G3).
    {
        auto *page = new QWidget(this);
        auto *form = new QFormLayout(page);

        m_updatesChannelCombo = new QComboBox(page);
        m_updatesChannelCombo->setObjectName(QStringLiteral("updatesChannelCombo"));
        m_updatesChannelCombo->addItem(tr("Nightly"), QStringLiteral("nightly"));
        m_updatesChannelCombo->addItem(tr("Stable"), QStringLiteral("stable"));
        // Stable has no signed feed yet (release.yml doesn't publish one
        // — only nightly.yml does today). Present-but-disabled rather
        // than absent, so the control's shape doesn't need to change
        // when the stable feed is wired (G3: the tooltip states why AND
        // where the status lives).
        const auto *stableModel = m_updatesChannelCombo->model();
        const int stableIndex = m_updatesChannelCombo->findData(QStringLiteral("stable"));
        if (stableIndex >= 0) {
            auto *itemModel = qobject_cast<QStandardItemModel *>(
                const_cast<QAbstractItemModel *>(stableModel));
            if (itemModel) {
                if (QStandardItem *item = itemModel->item(stableIndex)) {
                    item->setEnabled(false);
                    item->setToolTip(tr("Stable-channel updates aren't published yet — "
                                        "see ROADMAP.md \"Signed-update channel\". "
                                        "Nightly is available today."));
                }
            }
        }
        connect(m_updatesChannelCombo, &QComboBox::currentIndexChanged, this,
                &PreferencesDialog::refreshResetButtons);
        addFieldRow(
            form, tr("Channel"), m_updatesChannelCombo, QStringLiteral("reset_updatesChannel"),
            [this]() {
                selectComboByData(m_updatesChannelCombo, m_defaults.updatesChannel());
            },
            [this]() {
                return m_updatesChannelCombo->currentData().toString() ==
                       m_defaults.updatesChannel();
            },
            SettingsKeys::UpdatesChannel);

        m_updatesAutoCheckCheck =
            new QCheckBox(tr("Automatically check for updates (opt-in; off by default)"), page);
        m_updatesAutoCheckCheck->setObjectName(QStringLiteral("updatesAutoCheckCheck"));
        m_updatesAutoCheckCheck->setToolTip(
            tr("When on, Trailer checks the nightly update feed at startup and at most "
               "once every 24 hours. This never downloads or installs anything "
               "automatically — it only checks. Downloading and installing an update "
               "always requires clicking a button below."));
        connect(m_updatesAutoCheckCheck, &QCheckBox::toggled, this,
                &PreferencesDialog::refreshResetButtons);
        addCheckRow(
            form, m_updatesAutoCheckCheck, QStringLiteral("reset_updatesAutoCheck"),
            [this]() {
                m_updatesAutoCheckCheck->setChecked(m_defaults.updatesAutoCheckEnabled());
            },
            [this]() {
                return m_updatesAutoCheckCheck->isChecked() ==
                       m_defaults.updatesAutoCheckEnabled();
            },
            SettingsKeys::UpdatesAutoCheckEnabled);

        m_updatesStatusLabel = new QLabel(page);
        m_updatesStatusLabel->setObjectName(QStringLiteral("updatesStatusLabel"));
        m_updatesStatusLabel->setWordWrap(true);
        form->addRow(m_updatesStatusLabel);

        m_updatesActionButton = new QPushButton(tr("Check Now"), page);
        m_updatesActionButton->setObjectName(QStringLiteral("updatesActionButton"));
        m_updatesActionButton->setEnabled(false); // enabled once setUpdateManager() is called
        m_updatesActionButton->setToolTip(tr("Updates are unavailable in this context."));
        connect(m_updatesActionButton, &QPushButton::clicked, this, [this]() {
            if (!m_updateManager)
                return;
            using State = Update::UpdateManager::State;
            switch (m_updateManager->state()) {
            case State::UpdateAvailable:
                m_updateManager->startDownload();
                break;
            case State::ReadyToInstall:
                m_updateManager->installAndRelaunch();
                break;
            default:
                m_updateManager->checkNow();
                break;
            }
        });
        form->addRow(m_updatesActionButton);

        m_updatesTabIndex = tabs->addTab(page, tr("Updates"));
        refreshUpdatesStatus();
    }

    // ---- Advanced --------------------------------------------------
    {
        auto *page = new QWidget(this);
        auto *form = new QFormLayout(page);

        auto *note = new QLabel(
            tr("Erase all Trailer settings, recent files, saved AutoFill cards, "
               "saved signatures and downloaded models, and return every "
               "preference to its default. This deletes real files on disk and "
               "cannot be undone."),
            page);
        note->setObjectName(QStringLiteral("resetAllNote"));
        note->setWordWrap(true);
        form->addRow(note);

        m_resetAllButton = new QPushButton(tr("Reset all Trailer settings and data…"), page);
        m_resetAllButton->setObjectName(QStringLiteral("resetAllButton"));
        m_resetAllButton->setEnabled(false); // enabled once a callback is set
        // Defensive: in the shipped path a callback is always wired, but if
        // one is never set the button stays disabled — explain why rather
        // than presenting a dead control with no affordance (latent G3).
        m_resetAllButton->setToolTip(
            tr("Resetting all settings and data is unavailable in this context."));
        connect(m_resetAllButton, &QPushButton::clicked, this, [this]() {
            if (!m_resetAllCallback)
                return;
            m_resetAllCallback();
            // The callback may have wiped settings.toml and reloaded the
            // live Settings to defaults. Re-read it into the controls (and
            // re-snapshot the baseline) so a subsequent OK cannot re-save
            // the stale pre-reset values. If the user cancelled the
            // callback's own confirmation, Settings is unchanged and this
            // resync is a harmless no-op. The dialog deliberately stays
            // open.
            syncControlsFromSettings();
        });
        form->addRow(m_resetAllButton);

        tabs->addTab(page, tr("Advanced"));
    }

    outer->addWidget(tabs);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
                                             QDialogButtonBox::RestoreDefaults,
                                         this);
    buttons->setObjectName(QStringLiteral("buttonBox"));
    connect(buttons, &QDialogButtonBox::accepted, this, &PreferencesDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this,
            &PreferencesDialog::restoreEditableDefaults);
    outer->addWidget(buttons);

    syncControlsFromSettings();
}

void PreferencesDialog::selectUpdatesTab() {
    if (m_tabs && m_updatesTabIndex >= 0)
        m_tabs->setCurrentIndex(m_updatesTabIndex);
}

void PreferencesDialog::setUpdateManager(Update::UpdateManager *manager) {
    m_updateManager = manager;
    if (m_updatesActionButton) {
        const bool enabled = m_updateManager != nullptr;
        m_updatesActionButton->setEnabled(enabled);
        if (enabled)
            m_updatesActionButton->setToolTip(QString());
    }
    if (m_updateManager) {
        connect(m_updateManager, &Update::UpdateManager::stateChanged, this,
                &PreferencesDialog::refreshUpdatesStatus);
    }
    refreshUpdatesStatus();
}

void PreferencesDialog::refreshUpdatesStatus() {
    if (!m_updatesStatusLabel || !m_updatesActionButton)
        return;
    if (!m_updateManager) {
        m_updatesStatusLabel->setText(tr("Updates are unavailable in this context."));
        return;
    }
    using State = Update::UpdateManager::State;
    switch (m_updateManager->state()) {
    case State::Idle:
        m_updatesStatusLabel->setText(tr("Never checked."));
        m_updatesActionButton->setText(tr("Check Now"));
        m_updatesActionButton->setEnabled(true);
        break;
    case State::Checking:
        // Discloses the exact URL about to be fetched, before the fetch
        // completes — same consent framing as ModelDownloader's
        // show-the-URL-before-downloading dialog (AGENTS.md
        // "Networking"). checkStarted (which sets lastCheckUrl) fires
        // before UpdateChecker issues the actual GET, so this text is
        // in place before any bytes move.
        m_updatesStatusLabel->setText(
            tr("Checking for updates…\nFetching: %1").arg(m_updateManager->lastCheckUrl()));
        m_updatesActionButton->setEnabled(false);
        break;
    case State::UpToDate:
        m_updatesStatusLabel->setText(tr("You're up to date."));
        m_updatesActionButton->setText(tr("Check Now"));
        m_updatesActionButton->setEnabled(true);
        break;
    case State::UpdateAvailable:
        m_updatesStatusLabel->setText(
            tr("Update available: %1").arg(m_updateManager->latestEntry().tag));
        m_updatesActionButton->setText(tr("Download && Install"));
        m_updatesActionButton->setEnabled(true);
        break;
    case State::Downloading:
        m_updatesStatusLabel->setText(
            tr("Downloading update…\nFetching: %1").arg(m_updateManager->lastDownloadUrl()));
        m_updatesActionButton->setEnabled(false);
        break;
    case State::ReadyToInstall:
        m_updatesStatusLabel->setText(
            tr("Downloaded and verified: %1. Ready to install.")
                .arg(m_updateManager->latestEntry().tag));
        m_updatesActionButton->setText(tr("Install && Relaunch"));
        m_updatesActionButton->setEnabled(true);
        break;
    case State::Error:
        m_updatesStatusLabel->setText(
            tr("Could not check for updates: %1").arg(m_updateManager->lastError()));
        m_updatesActionButton->setText(tr("Check Now"));
        m_updatesActionButton->setEnabled(true);
        break;
    }
}

void PreferencesDialog::setManageModelsCallback(std::function<void()> cb) {
    m_manageModelsCallback = std::move(cb);
    if (m_manageModelsButton) {
        const bool enabled = static_cast<bool>(m_manageModelsCallback);
        m_manageModelsButton->setEnabled(enabled);
        // Clear the disabled-state explanation once the control is live.
        if (enabled)
            m_manageModelsButton->setToolTip(QString());
    }
}

void PreferencesDialog::setResetAllCallback(std::function<void()> cb) {
    m_resetAllCallback = std::move(cb);
    if (m_resetAllButton) {
        const bool enabled = static_cast<bool>(m_resetAllCallback);
        m_resetAllButton->setEnabled(enabled);
        // Clear the disabled-state explanation once the control is live.
        if (enabled)
            m_resetAllButton->setToolTip(QString());
    }
}

void PreferencesDialog::syncControlsFromSettings() {
    selectComboByData(m_themeCombo, static_cast<int>(m_settings.theme()));
    selectComboByData(m_openFilesInCombo, static_cast<int>(m_settings.openFilesIn()));
    m_restoreWindowsCheck->setChecked(m_settings.restorePreviousWindows());

    m_autoSaveCheck->setChecked(m_settings.autoSave());
    m_recentMaxSpin->setValue(m_settings.recentMax());

    m_mlRecognizeTextCheck->setChecked(m_settings.mlRecognizeTextInBackground());
    m_mlPreloadSegCheck->setChecked(m_settings.mlPreloadSegmentationOnToolActivation());
    m_mlRunOnBatteryCheck->setChecked(m_settings.mlRunOnBattery());

    selectComboByData(m_updatesChannelCombo, m_settings.updatesChannel());
    m_updatesAutoCheckCheck->setChecked(m_settings.updatesAutoCheckEnabled());

    // Snapshot the values as they now sit IN the widgets (post-clamp), so
    // an untouched OK writes nothing back for a field the user never
    // edited.
    m_baseline.theme = static_cast<Theme>(m_themeCombo->currentData().toInt());
    m_baseline.openFilesIn = static_cast<OpenFilesIn>(m_openFilesInCombo->currentData().toInt());
    m_baseline.restoreWindows = m_restoreWindowsCheck->isChecked();
    m_baseline.autoSave = m_autoSaveCheck->isChecked();
    m_baseline.recentMax = m_recentMaxSpin->value();
    m_baseline.mlRecognizeText = m_mlRecognizeTextCheck->isChecked();
    m_baseline.mlPreloadSeg = m_mlPreloadSegCheck->isChecked();
    m_baseline.mlRunOnBattery = m_mlRunOnBatteryCheck->isChecked();
    m_baseline.updatesChannel = m_updatesChannelCombo->currentData().toString();
    m_baseline.updatesAutoCheck = m_updatesAutoCheckCheck->isChecked();

    refreshResetButtons();
}

void PreferencesDialog::applyToSettings() {
    // Persist only controls whose value diverges from the baseline
    // captured at the last sync — an untouched (possibly clamped-on-
    // display) field is left exactly as it was on disk.
    const auto theme = static_cast<Theme>(m_themeCombo->currentData().toInt());
    if (theme != m_baseline.theme)
        m_settings.setTheme(theme);

    const auto openFilesIn =
        static_cast<OpenFilesIn>(m_openFilesInCombo->currentData().toInt());
    if (openFilesIn != m_baseline.openFilesIn)
        m_settings.setOpenFilesIn(openFilesIn);

    if (m_restoreWindowsCheck->isChecked() != m_baseline.restoreWindows)
        m_settings.setRestorePreviousWindows(m_restoreWindowsCheck->isChecked());

    if (m_autoSaveCheck->isChecked() != m_baseline.autoSave)
        m_settings.setAutoSave(m_autoSaveCheck->isChecked());

    if (m_recentMaxSpin->value() != m_baseline.recentMax)
        m_settings.setRecentMax(m_recentMaxSpin->value());

    if (m_mlRecognizeTextCheck->isChecked() != m_baseline.mlRecognizeText)
        m_settings.setMlRecognizeTextInBackground(m_mlRecognizeTextCheck->isChecked());

    if (m_mlPreloadSegCheck->isChecked() != m_baseline.mlPreloadSeg)
        m_settings.setMlPreloadSegmentationOnToolActivation(m_mlPreloadSegCheck->isChecked());

    if (m_mlRunOnBatteryCheck->isChecked() != m_baseline.mlRunOnBattery)
        m_settings.setMlRunOnBattery(m_mlRunOnBatteryCheck->isChecked());

    const QString channel = m_updatesChannelCombo->currentData().toString();
    if (channel != m_baseline.updatesChannel)
        m_settings.setUpdatesChannel(channel);

    if (m_updatesAutoCheckCheck->isChecked() != m_baseline.updatesAutoCheck)
        m_settings.setUpdatesAutoCheckEnabled(m_updatesAutoCheckCheck->isChecked());
}

void PreferencesDialog::restoreEditableDefaults() {
    // Theme is now a live editable control, so Restore Defaults resets it to
    // the default (System) alongside every other control — UI only, nothing
    // persists until OK.
    selectComboByData(m_themeCombo, static_cast<int>(m_defaults.theme()));
    selectComboByData(m_openFilesInCombo, static_cast<int>(m_defaults.openFilesIn()));
    m_restoreWindowsCheck->setChecked(m_defaults.restorePreviousWindows());

    m_autoSaveCheck->setChecked(m_defaults.autoSave());
    m_recentMaxSpin->setValue(m_defaults.recentMax());

    m_mlRecognizeTextCheck->setChecked(m_defaults.mlRecognizeTextInBackground());
    m_mlPreloadSegCheck->setChecked(m_defaults.mlPreloadSegmentationOnToolActivation());
    m_mlRunOnBatteryCheck->setChecked(m_defaults.mlRunOnBattery());

    selectComboByData(m_updatesChannelCombo, m_defaults.updatesChannel());
    m_updatesAutoCheckCheck->setChecked(m_defaults.updatesAutoCheckEnabled());

    refreshResetButtons();
}

void PreferencesDialog::refreshResetButtons() {
    for (const auto &entry : m_resets) {
        if (entry.button && entry.atDefault)
            entry.button->setEnabled(!entry.atDefault());
    }
}

void PreferencesDialog::accept() {
    applyToSettings();
    m_settings.save();
    emit settingsApplied();
    QDialog::accept();
}

} // namespace trailer
