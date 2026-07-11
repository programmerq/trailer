#include "PreferencesDialog.h"

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

} // namespace

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
    tabs->setObjectName(QStringLiteral("tabWidget"));

    // A labelled row whose field control sits next to a compact,
    // right-aligned revert icon so every row shares one right edge.
    // `resetName` is the stable objectName the tests drive; `onReset`
    // reverts just this control; `atDefault` greys the icon out when the
    // control already holds its default.
    const auto addFieldRow = [this](QFormLayout *form, const QString &label,
                                    QWidget *control, const QString &resetName,
                                    std::function<void()> onReset,
                                    std::function<bool()> atDefault) {
        auto *rowWidget = new QWidget(form->parentWidget());
        auto *rowLayout = new QHBoxLayout(rowWidget);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->addWidget(control, 1);
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
                                    std::function<bool()> atDefault) {
        auto *rowWidget = new QWidget(form->parentWidget());
        auto *rowLayout = new QHBoxLayout(rowWidget);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->addWidget(check, 1);
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
        // Honest control: the theme key is persisted but nothing applies
        // it yet, so the combo is visibly disabled rather than a no-op
        // that erodes trust (docs/decisions/0004). A disabled widget never
        // receives ToolTip events, so the explanation also lives in a
        // visible helper label beneath the combo.
        m_themeCombo->setEnabled(false);
        m_themeCombo->setToolTip(
            tr("Theme selection isn't applied yet (planned for a future release)."));

        // Theme has no reset (it is disabled); reserve the reset-column
        // width so its right edge lines up with the resettable rows, and
        // stack a muted helper label underneath.
        auto *themeField = new QWidget(page);
        auto *themeCol = new QVBoxLayout(themeField);
        themeCol->setContentsMargins(0, 0, 0, 0);
        themeCol->setSpacing(2);
        auto *themeRow = new QHBoxLayout();
        themeRow->setContentsMargins(0, 0, 0, 0);
        themeRow->addWidget(m_themeCombo, 1);
        themeRow->addSpacing(kResetButtonSize);
        themeCol->addLayout(themeRow);

        auto *themeHelp = new QLabel(tr("Not applied yet — planned for a future release."),
                                     themeField);
        themeHelp->setObjectName(QStringLiteral("themeHelpLabel"));
        themeHelp->setWordWrap(true);
        QFont helpFont = themeHelp->font();
        helpFont.setPointSizeF(helpFont.pointSizeF() * 0.9);
        themeHelp->setFont(helpFont);
        QPalette helpPalette = themeHelp->palette();
        helpPalette.setColor(QPalette::WindowText,
                             helpPalette.color(QPalette::Disabled, QPalette::WindowText));
        themeHelp->setPalette(helpPalette);
        themeCol->addWidget(themeHelp);

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
            });

        m_restoreWindowsCheck = new QCheckBox(tr("Restore previous windows on launch"), page);
        m_restoreWindowsCheck->setObjectName(QStringLiteral("restoreWindowsCheck"));
        connect(m_restoreWindowsCheck, &QCheckBox::toggled, this,
                &PreferencesDialog::refreshResetButtons);
        addCheckRow(
            form, m_restoreWindowsCheck, QStringLiteral("reset_restoreWindows"),
            [this]() { m_restoreWindowsCheck->setChecked(m_defaults.restorePreviousWindows()); },
            [this]() {
                return m_restoreWindowsCheck->isChecked() == m_defaults.restorePreviousWindows();
            });

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
            [this]() { return m_autoSaveCheck->isChecked() == m_defaults.autoSave(); });

        m_recentMaxSpin = new QSpinBox(page);
        m_recentMaxSpin->setObjectName(QStringLiteral("recentMaxSpin"));
        m_recentMaxSpin->setRange(kRecentMaxMin, kRecentMaxMax);
        connect(m_recentMaxSpin, &QSpinBox::valueChanged, this,
                &PreferencesDialog::refreshResetButtons);
        addFieldRow(
            form, tr("Recent files to remember"), m_recentMaxSpin,
            QStringLiteral("reset_recentMax"),
            [this]() { m_recentMaxSpin->setValue(m_defaults.recentMax()); },
            [this]() { return m_recentMaxSpin->value() == m_defaults.recentMax(); });

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
            });

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
            });

        m_mlRunOnBatteryCheck = new QCheckBox(tr("Run ML on battery power"), page);
        m_mlRunOnBatteryCheck->setObjectName(QStringLiteral("mlRunOnBatteryCheck"));
        connect(m_mlRunOnBatteryCheck, &QCheckBox::toggled, this,
                &PreferencesDialog::refreshResetButtons);
        addCheckRow(
            form, m_mlRunOnBatteryCheck, QStringLiteral("reset_mlRunOnBattery"),
            [this]() { m_mlRunOnBatteryCheck->setChecked(m_defaults.mlRunOnBattery()); },
            [this]() {
                return m_mlRunOnBatteryCheck->isChecked() == m_defaults.mlRunOnBattery();
            });

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
}

void PreferencesDialog::restoreEditableDefaults() {
    // Theme is disabled and reflects the stored (hand-editable) value —
    // Restore Defaults must not silently overwrite it.
    selectComboByData(m_openFilesInCombo, static_cast<int>(m_defaults.openFilesIn()));
    m_restoreWindowsCheck->setChecked(m_defaults.restorePreviousWindows());

    m_autoSaveCheck->setChecked(m_defaults.autoSave());
    m_recentMaxSpin->setValue(m_defaults.recentMax());

    m_mlRecognizeTextCheck->setChecked(m_defaults.mlRecognizeTextInBackground());
    m_mlPreloadSegCheck->setChecked(m_defaults.mlPreloadSegmentationOnToolActivation());
    m_mlRunOnBatteryCheck->setChecked(m_defaults.mlRunOnBattery());

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
