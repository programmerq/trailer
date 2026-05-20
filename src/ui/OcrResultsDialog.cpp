#include "OcrResultsDialog.h"

#include <algorithm>

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace trailer {

RecognizeTextDialog::RecognizeTextDialog(int pageCount, int currentPage, bool hasTextLayer,
                                         const QStringList &languageOptions, QWidget *parent)
    : QDialog(parent), m_pageCount(std::max(1, pageCount)),
      m_currentPage(std::clamp(currentPage, 0, std::max(0, pageCount - 1))) {
    setWindowTitle(tr("Recognize Text"));
    setModal(true);

    auto *layout = new QVBoxLayout(this);

    auto *form = new QFormLayout();

    m_scopeGroup = new QButtonGroup(this);

    auto *scopeWidget = new QWidget(this);
    auto *scopeLayout = new QVBoxLayout(scopeWidget);
    scopeLayout->setContentsMargins(0, 0, 0, 0);
    m_currentRadio = new QRadioButton(tr("Current page"), scopeWidget);
    m_allRadio = new QRadioButton(tr("All pages"), scopeWidget);
    m_rangeRadio = new QRadioButton(tr("Page range"), scopeWidget);
    m_scopeGroup->addButton(m_currentRadio, static_cast<int>(PageScope::Current));
    m_scopeGroup->addButton(m_allRadio, static_cast<int>(PageScope::All));
    m_scopeGroup->addButton(m_rangeRadio, static_cast<int>(PageScope::Range));
    scopeLayout->addWidget(m_currentRadio);
    scopeLayout->addWidget(m_allRadio);

    auto *rangeRow = new QHBoxLayout();
    rangeRow->setContentsMargins(0, 0, 0, 0);
    rangeRow->addWidget(m_rangeRadio);
    rangeRow->addSpacing(8);
    rangeRow->addWidget(new QLabel(tr("from"), scopeWidget));
    m_fromSpin = new QSpinBox(scopeWidget);
    m_fromSpin->setRange(1, m_pageCount);
    m_fromSpin->setValue(1);
    rangeRow->addWidget(m_fromSpin);
    rangeRow->addWidget(new QLabel(tr("to"), scopeWidget));
    m_toSpin = new QSpinBox(scopeWidget);
    m_toSpin->setRange(1, m_pageCount);
    m_toSpin->setValue(m_pageCount);
    rangeRow->addWidget(m_toSpin);
    rangeRow->addStretch(1);
    scopeLayout->addLayout(rangeRow);

    form->addRow(tr("Pages:"), scopeWidget);

    // Default scope: current page for >50-page docs (large-doc
    // guard), all pages otherwise.
    if (m_pageCount > 50) {
        m_currentRadio->setChecked(true);
    } else {
        m_allRadio->setChecked(true);
    }

    // Language row. Hidden when only a single option is available
    // (we don't make the user pick "English" out of "English") — the
    // current ship state has only the Latin recognizer.
    m_languageCombo = new QComboBox(this);
    QStringList opts = languageOptions;
    if (opts.isEmpty()) {
        opts << QStringLiteral("auto");
    }
    if (!opts.contains(QStringLiteral("auto"), Qt::CaseInsensitive)) {
        opts.prepend(QStringLiteral("auto"));
    }
    for (const QString &lang : opts) {
        const QString label = (lang.compare(QStringLiteral("auto"), Qt::CaseInsensitive) == 0)
                                  ? tr("Auto-detect")
                                  : lang;
        m_languageCombo->addItem(label, lang);
    }
    // Auto-detect is index 0 by construction; make it the default.
    m_languageCombo->setCurrentIndex(0);
    auto *languageLabel = new QLabel(tr("Language:"), this);
    if (opts.size() <= 1) {
        m_languageCombo->setVisible(false);
        languageLabel->setVisible(false);
    } else {
        form->addRow(languageLabel, m_languageCombo);
    }

    m_forceRerunCheck =
        new QCheckBox(tr("Force re-run even if a text layer exists"), this);
    m_forceRerunCheck->setChecked(false);
    // Hide for pure-raster docs (no layer to bypass).
    m_forceRerunCheck->setVisible(hasTextLayer);
    form->addRow(QString(), m_forceRerunCheck);

    layout->addLayout(form);

    auto *runBtn = new QDialogButtonBox(this);
    auto *runButton = runBtn->addButton(tr("Run"), QDialogButtonBox::AcceptRole);
    runBtn->addButton(QDialogButtonBox::Cancel);
    runButton->setDefault(true);
    connect(runBtn, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(runBtn, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(runBtn);

    connect(m_scopeGroup, &QButtonGroup::idClicked, this,
            &RecognizeTextDialog::onScopeChanged);
    onScopeChanged();
}

void RecognizeTextDialog::onScopeChanged() {
    const bool rangeOn = m_rangeRadio && m_rangeRadio->isChecked();
    if (m_fromSpin)
        m_fromSpin->setEnabled(rangeOn);
    if (m_toSpin)
        m_toSpin->setEnabled(rangeOn);
}

RecognizeTextDialog::PageScope RecognizeTextDialog::scope() const {
    if (m_allRadio && m_allRadio->isChecked())
        return PageScope::All;
    if (m_rangeRadio && m_rangeRadio->isChecked())
        return PageScope::Range;
    return PageScope::Current;
}

std::vector<int> RecognizeTextDialog::resolvedPages() const {
    std::vector<int> out;
    if (result() != QDialog::Accepted)
        return out;
    switch (scope()) {
    case PageScope::Current:
        out.push_back(m_currentPage);
        break;
    case PageScope::All:
        out.reserve(static_cast<size_t>(m_pageCount));
        for (int i = 0; i < m_pageCount; ++i) {
            out.push_back(i);
        }
        break;
    case PageScope::Range: {
        const int from = std::max(0, (m_fromSpin ? m_fromSpin->value() : 1) - 1);
        const int to = std::min(m_pageCount - 1, (m_toSpin ? m_toSpin->value() : m_pageCount) - 1);
        for (int i = from; i <= to; ++i) {
            out.push_back(i);
        }
        break;
    }
    }
    return out;
}

QString RecognizeTextDialog::language() const {
    if (!m_languageCombo || !m_languageCombo->isVisible())
        return {};
    return m_languageCombo->currentData().toString();
}

bool RecognizeTextDialog::forceRerun() const {
    return m_forceRerunCheck && m_forceRerunCheck->isChecked();
}

} // namespace trailer
