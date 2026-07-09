#include "PreferencesDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
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
// (Settings.h), so the Preferences UI is where a sane range is imposed;
// 0 disables the recent list, 200 is a generous upper bound.
constexpr int kRecentMaxMin = 0;
constexpr int kRecentMaxMax = 200;

void selectComboByData(QComboBox *combo, int value) {
    const int index = combo->findData(value);
    if (index >= 0)
        combo->setCurrentIndex(index);
}

} // namespace

PreferencesDialog::PreferencesDialog(Settings &settings, QWidget *parent)
    : QDialog(parent), m_settings(settings) {
    setWindowTitle(tr("Preferences"));

    auto *outer = new QVBoxLayout(this);
    auto *tabs = new QTabWidget(this);
    tabs->setObjectName(QStringLiteral("tabWidget"));

    // Build a form row whose editable control sits next to a compact
    // inline "Reset to default" affordance. `resetName` is the stable
    // objectName the tests drive; `onReset` reverts just this control.
    const auto addResettableRow = [](QFormLayout *form, const QString &label,
                                     QWidget *control, const QString &resetName,
                                     std::function<void()> onReset) {
        auto *rowWidget = new QWidget(form->parentWidget());
        auto *rowLayout = new QHBoxLayout(rowWidget);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->addWidget(control, 1);

        auto *reset = new QToolButton(rowWidget);
        reset->setObjectName(resetName);
        reset->setText(tr("Reset"));
        reset->setToolTip(tr("Reset to default"));
        reset->setAutoRaise(true);
        QObject::connect(reset, &QToolButton::clicked, rowWidget,
                         [onReset = std::move(onReset)]() { onReset(); });
        rowLayout->addWidget(reset, 0);

        form->addRow(label, rowWidget);
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
        // that erodes trust (docs/decisions/0004).
        m_themeCombo->setEnabled(false);
        m_themeCombo->setToolTip(
            tr("Theme selection isn't applied yet (planned for a future release)."));
        form->addRow(tr("Theme"), m_themeCombo);

        m_openFilesInCombo = new QComboBox(page);
        m_openFilesInCombo->setObjectName(QStringLiteral("openFilesInCombo"));
        m_openFilesInCombo->addItem(tr("New tab"), static_cast<int>(OpenFilesIn::NewTab));
        m_openFilesInCombo->addItem(tr("New window"), static_cast<int>(OpenFilesIn::NewWindow));
        m_openFilesInCombo->addItem(tr("Same window"), static_cast<int>(OpenFilesIn::SameWindow));
        addResettableRow(form, tr("Open files in"), m_openFilesInCombo,
                         QStringLiteral("reset_openFilesIn"), [this]() {
                             selectComboByData(m_openFilesInCombo,
                                               static_cast<int>(m_defaults.openFilesIn()));
                         });

        m_restoreWindowsCheck = new QCheckBox(page);
        m_restoreWindowsCheck->setObjectName(QStringLiteral("restoreWindowsCheck"));
        addResettableRow(form, tr("Restore previous windows on launch"),
                         m_restoreWindowsCheck, QStringLiteral("reset_restoreWindows"),
                         [this]() {
                             m_restoreWindowsCheck->setChecked(
                                 m_defaults.restorePreviousWindows());
                         });

        tabs->addTab(page, tr("General"));
    }

    // ---- Files -----------------------------------------------------
    {
        auto *page = new QWidget(this);
        auto *form = new QFormLayout(page);

        m_autoSaveCheck = new QCheckBox(page);
        m_autoSaveCheck->setObjectName(QStringLiteral("autoSaveCheck"));
        addResettableRow(form, tr("Auto-save"), m_autoSaveCheck,
                         QStringLiteral("reset_autoSave"),
                         [this]() { m_autoSaveCheck->setChecked(m_defaults.autoSave()); });

        m_recentMaxSpin = new QSpinBox(page);
        m_recentMaxSpin->setObjectName(QStringLiteral("recentMaxSpin"));
        m_recentMaxSpin->setRange(kRecentMaxMin, kRecentMaxMax);
        addResettableRow(form, tr("Recent files to remember"), m_recentMaxSpin,
                         QStringLiteral("reset_recentMax"),
                         [this]() { m_recentMaxSpin->setValue(m_defaults.recentMax()); });

        tabs->addTab(page, tr("Files"));
    }

    // ---- Machine Learning ------------------------------------------
    {
        auto *page = new QWidget(this);
        auto *form = new QFormLayout(page);

        m_mlRecognizeTextCheck = new QCheckBox(page);
        m_mlRecognizeTextCheck->setObjectName(QStringLiteral("mlRecognizeTextCheck"));
        addResettableRow(form, tr("Recognize text in the background"),
                         m_mlRecognizeTextCheck, QStringLiteral("reset_mlRecognizeText"),
                         [this]() {
                             m_mlRecognizeTextCheck->setChecked(
                                 m_defaults.mlRecognizeTextInBackground());
                         });

        m_mlPreloadSegCheck = new QCheckBox(page);
        m_mlPreloadSegCheck->setObjectName(QStringLiteral("mlPreloadSegCheck"));
        addResettableRow(form, tr("Preload segmentation on tool activation"),
                         m_mlPreloadSegCheck, QStringLiteral("reset_mlPreloadSeg"),
                         [this]() {
                             m_mlPreloadSegCheck->setChecked(
                                 m_defaults.mlPreloadSegmentationOnToolActivation());
                         });

        m_mlRunOnBatteryCheck = new QCheckBox(page);
        m_mlRunOnBatteryCheck->setObjectName(QStringLiteral("mlRunOnBatteryCheck"));
        addResettableRow(form, tr("Run ML on battery power"), m_mlRunOnBatteryCheck,
                         QStringLiteral("reset_mlRunOnBattery"), [this]() {
                             m_mlRunOnBatteryCheck->setChecked(m_defaults.mlRunOnBattery());
                         });

        m_manageModelsButton = new QPushButton(tr("Manage models…"), page);
        m_manageModelsButton->setObjectName(QStringLiteral("manageModelsButton"));
        m_manageModelsButton->setEnabled(false); // enabled once a callback is set
        connect(m_manageModelsButton, &QPushButton::clicked, this, [this]() {
            if (m_manageModelsCallback)
                m_manageModelsCallback();
        });
        form->addRow(QString(), m_manageModelsButton);

        tabs->addTab(page, tr("Machine Learning"));
    }

    // ---- Advanced --------------------------------------------------
    {
        auto *page = new QWidget(this);
        auto *form = new QFormLayout(page);

        auto *note = new QLabel(
            tr("Erase all Trailer settings, recent files, saved cards, "
               "signatures and downloaded models. This cannot be undone."),
            page);
        note->setWordWrap(true);
        form->addRow(note);

        m_resetAllButton = new QPushButton(tr("Reset all Trailer settings and data…"), page);
        m_resetAllButton->setObjectName(QStringLiteral("resetAllButton"));
        m_resetAllButton->setEnabled(false); // enabled once a callback is set
        connect(m_resetAllButton, &QPushButton::clicked, this, [this]() {
            if (m_resetAllCallback)
                m_resetAllCallback();
        });
        form->addRow(QString(), m_resetAllButton);

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

    loadFromSettings(m_settings);
}

void PreferencesDialog::setManageModelsCallback(std::function<void()> cb) {
    m_manageModelsCallback = std::move(cb);
    if (m_manageModelsButton)
        m_manageModelsButton->setEnabled(static_cast<bool>(m_manageModelsCallback));
}

void PreferencesDialog::setResetAllCallback(std::function<void()> cb) {
    m_resetAllCallback = std::move(cb);
    if (m_resetAllButton)
        m_resetAllButton->setEnabled(static_cast<bool>(m_resetAllCallback));
}

void PreferencesDialog::loadFromSettings(const Settings &src) {
    selectComboByData(m_themeCombo, static_cast<int>(src.theme()));
    selectComboByData(m_openFilesInCombo, static_cast<int>(src.openFilesIn()));
    m_restoreWindowsCheck->setChecked(src.restorePreviousWindows());

    m_autoSaveCheck->setChecked(src.autoSave());
    m_recentMaxSpin->setValue(src.recentMax());

    m_mlRecognizeTextCheck->setChecked(src.mlRecognizeTextInBackground());
    m_mlPreloadSegCheck->setChecked(src.mlPreloadSegmentationOnToolActivation());
    m_mlRunOnBatteryCheck->setChecked(src.mlRunOnBattery());
}

void PreferencesDialog::applyToSettings() {
    m_settings.setTheme(
        static_cast<Theme>(m_themeCombo->currentData().toInt()));
    m_settings.setOpenFilesIn(
        static_cast<OpenFilesIn>(m_openFilesInCombo->currentData().toInt()));
    m_settings.setRestorePreviousWindows(m_restoreWindowsCheck->isChecked());

    m_settings.setAutoSave(m_autoSaveCheck->isChecked());
    m_settings.setRecentMax(m_recentMaxSpin->value());

    m_settings.setMlRecognizeTextInBackground(m_mlRecognizeTextCheck->isChecked());
    m_settings.setMlPreloadSegmentationOnToolActivation(m_mlPreloadSegCheck->isChecked());
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
}

void PreferencesDialog::accept() {
    applyToSettings();
    m_settings.save();
    emit settingsApplied();
    QDialog::accept();
}

} // namespace trailer
