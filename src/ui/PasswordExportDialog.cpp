#include "ui/PasswordExportDialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

namespace trailer {

PasswordExportDialog::PasswordExportDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("Set PDF Password"));
    auto *form = new QFormLayout(this);

    m_pwEdit = new QLineEdit(this);
    m_pwEdit->setEchoMode(QLineEdit::Password);
    m_pwEdit->setPlaceholderText(tr("Enter password"));

    m_confirmEdit = new QLineEdit(this);
    m_confirmEdit->setEchoMode(QLineEdit::Password);
    m_confirmEdit->setPlaceholderText(tr("Confirm password"));

    form->addRow(tr("Password:"), m_pwEdit);
    form->addRow(tr("Confirm:"), m_confirmEdit);

    // Inline validation hint. Lives below the fields and updates live; it is
    // the sole feedback surface (no warn-and-abort dialog) and doubles as the
    // G3 "why" text mirrored into the disabled OK button's tooltip.
    m_hint = new QLabel(this);
    m_hint->setWordWrap(true);
    form->addRow(m_hint);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_okButton = buttons->button(QDialogButtonBox::Ok);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    form->addRow(buttons);

    connect(m_pwEdit, &QLineEdit::textChanged, this,
            &PasswordExportDialog::revalidate);
    connect(m_confirmEdit, &QLineEdit::textChanged, this,
            &PasswordExportDialog::revalidate);

    revalidate();
}

QString PasswordExportDialog::password() const { return m_pwEdit->text(); }

bool PasswordExportDialog::isInputValid() const {
    return !m_pwEdit->text().isEmpty() &&
           m_pwEdit->text() == m_confirmEdit->text();
}

void PasswordExportDialog::revalidate() {
    const bool valid = isInputValid();

    // Choose the most actionable hint for the current state. Empty password is
    // the first thing to fix; a non-empty password with a non-matching confirm
    // is the mismatch case.
    QString hint;
    if (m_pwEdit->text().isEmpty())
        hint = tr("Enter a password to protect the PDF.");
    else if (m_pwEdit->text() != m_confirmEdit->text())
        hint = tr("Passwords do not match.");

    m_hint->setText(hint);
    m_okButton->setEnabled(valid);
    // G3: a disabled control states why. When valid there is nothing to
    // explain, so the tooltip clears.
    m_okButton->setToolTip(valid ? QString() : hint);
}

} // namespace trailer
