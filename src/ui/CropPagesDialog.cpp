#include "ui/CropPagesDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>

namespace trailer {

CropPagesDialog::CropPagesDialog(int pageCount, QWidget *parent)
    : QDialog(parent), m_multiPage(pageCount > 1) {
    setWindowTitle(tr("Crop Pages"));
    auto *form = new QFormLayout(this);

    auto makeSpin = [this]() {
        auto *s = new QDoubleSpinBox(this);
        s->setRange(0.0, 500.0);
        s->setDecimals(1);
        s->setSuffix(QStringLiteral(" mm"));
        return s;
    };
    m_left = makeSpin();
    m_top = makeSpin();
    m_right = makeSpin();
    m_bottom = makeSpin();
    form->addRow(tr("Left margin"), m_left);
    form->addRow(tr("Top margin"), m_top);
    form->addRow(tr("Right margin"), m_right);
    form->addRow(tr("Bottom margin"), m_bottom);

    m_allPagesCheck = new QCheckBox(tr("Apply to all pages"), this);
    m_allPagesCheck->setChecked(true);
    // A one-page document offers no meaningful "all pages" choice; hide the
    // control rather than presenting an inert toggle. Cropping then falls to
    // the current-page path, which for a single-page document is the sole
    // page.
    m_allPagesCheck->setVisible(m_multiPage);
    form->addRow(m_allPagesCheck);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    form->addRow(buttons);
}

double CropPagesDialog::leftMm() const { return m_left->value(); }
double CropPagesDialog::topMm() const { return m_top->value(); }
double CropPagesDialog::rightMm() const { return m_right->value(); }
double CropPagesDialog::bottomMm() const { return m_bottom->value(); }

bool CropPagesDialog::applyToAllPages() const {
    return m_multiPage && m_allPagesCheck->isChecked();
}

} // namespace trailer
