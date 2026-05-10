#pragma once

#include "cards/MyCard.h"

#include <QDialog>

class QLineEdit;

namespace trailer {

// Single-card editor. Pop it up with an existing MyCard (or a blank
// one for "create new"); accept() populates the card and the caller
// reads it back via card().
//
// Deliberately minimal — one tab, plain QLineEdits. Address fields
// are optional; no validation beyond "trim whitespace on save".
class MyCardDialog : public QDialog {
    Q_OBJECT

  public:
    explicit MyCardDialog(QWidget *parent = nullptr);

    void setCard(const MyCard &card);
    MyCard card() const;

  private:
    QLineEdit *m_label = nullptr;

    QLineEdit *m_givenName = nullptr;
    QLineEdit *m_familyName = nullptr;
    QLineEdit *m_fullName = nullptr;

    QLineEdit *m_email = nullptr;
    QLineEdit *m_phone = nullptr;

    QLineEdit *m_organization = nullptr;
    QLineEdit *m_jobTitle = nullptr;

    QLineEdit *m_addressLine1 = nullptr;
    QLineEdit *m_addressLine2 = nullptr;
    QLineEdit *m_city = nullptr;
    QLineEdit *m_state = nullptr;
    QLineEdit *m_postalCode = nullptr;
    QLineEdit *m_country = nullptr;
};

} // namespace trailer
