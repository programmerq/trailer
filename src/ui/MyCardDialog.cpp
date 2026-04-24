#include "MyCardDialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QVBoxLayout>

namespace trailer {

MyCardDialog::MyCardDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("My Card"));

    auto* outer = new QVBoxLayout(this);
    auto* form = new QFormLayout;

    m_label = new QLineEdit(this);
    m_label->setPlaceholderText(tr("e.g. Personal, Work"));
    form->addRow(tr("Label"), m_label);

    m_givenName  = new QLineEdit(this);
    m_familyName = new QLineEdit(this);
    m_fullName   = new QLineEdit(this);
    m_fullName->setPlaceholderText(
        tr("Optional override for fields that want one line"));
    form->addRow(tr("Given name"), m_givenName);
    form->addRow(tr("Family name"), m_familyName);
    form->addRow(tr("Full name"), m_fullName);

    m_email = new QLineEdit(this);
    m_phone = new QLineEdit(this);
    form->addRow(tr("Email"), m_email);
    form->addRow(tr("Phone"), m_phone);

    m_organization = new QLineEdit(this);
    m_jobTitle     = new QLineEdit(this);
    form->addRow(tr("Organization"), m_organization);
    form->addRow(tr("Job title"), m_jobTitle);

    m_addressLine1 = new QLineEdit(this);
    m_addressLine2 = new QLineEdit(this);
    m_city         = new QLineEdit(this);
    m_state        = new QLineEdit(this);
    m_postalCode   = new QLineEdit(this);
    m_country      = new QLineEdit(this);
    form->addRow(tr("Address line 1"), m_addressLine1);
    form->addRow(tr("Address line 2"), m_addressLine2);
    form->addRow(tr("City"), m_city);
    form->addRow(tr("State / Province"), m_state);
    form->addRow(tr("Postal code"), m_postalCode);
    form->addRow(tr("Country"), m_country);

    outer->addLayout(form);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    outer->addWidget(buttons);
}

void MyCardDialog::setCard(const MyCard& c) {
    m_label->setText(c.label);
    m_givenName->setText(c.givenName);
    m_familyName->setText(c.familyName);
    m_fullName->setText(c.fullName);
    m_email->setText(c.email);
    m_phone->setText(c.phone);
    m_organization->setText(c.organization);
    m_jobTitle->setText(c.jobTitle);
    m_addressLine1->setText(c.addressLine1);
    m_addressLine2->setText(c.addressLine2);
    m_city->setText(c.city);
    m_state->setText(c.state);
    m_postalCode->setText(c.postalCode);
    m_country->setText(c.country);
}

MyCard MyCardDialog::card() const {
    MyCard c;
    c.label         = m_label->text().trimmed();
    c.givenName     = m_givenName->text().trimmed();
    c.familyName    = m_familyName->text().trimmed();
    c.fullName      = m_fullName->text().trimmed();
    c.email         = m_email->text().trimmed();
    c.phone         = m_phone->text().trimmed();
    c.organization  = m_organization->text().trimmed();
    c.jobTitle      = m_jobTitle->text().trimmed();
    c.addressLine1  = m_addressLine1->text().trimmed();
    c.addressLine2  = m_addressLine2->text().trimmed();
    c.city          = m_city->text().trimmed();
    c.state         = m_state->text().trimmed();
    c.postalCode    = m_postalCode->text().trimmed();
    c.country       = m_country->text().trimmed();
    return c;
}

}  // namespace trailer
