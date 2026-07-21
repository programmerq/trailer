#pragma once

#include <QDialog>

class QLabel;
class QLineEdit;
class QPushButton;

namespace trailer {

// Modal dialog that collects (and confirms) the password for the
// "Export as Password-Protected PDF" flow.
//
// Validation is inline and live, per PHILOSOPHY → *No popup that just says
// "no"* and *How Trailer reduces friction* (backlog
// 2026-07-15-password-export-inline-validation): the OK button is disabled
// until both fields are non-empty and equal, and an inline hint explains why
// it is disabled. There is no warn-and-abort dialog — the earlier flow popped
// a QMessageBox and returned on mismatch/empty, which threw away the
// already-chosen save destination and forced the user back through the file
// picker. Because the destination is chosen before this dialog and OK cannot
// be pressed while the input is invalid, correcting a typo keeps the same
// destination without reopening the Save dialog.
//
// The hint doubles as the G3 "why" text: the disabled OK button carries a
// tooltip mirroring the current hint so the reason is reachable on hover as
// well as inline.
class PasswordExportDialog : public QDialog {
    Q_OBJECT

  public:
    explicit PasswordExportDialog(QWidget *parent = nullptr);

    // The confirmed password. Only meaningful after the dialog is accepted;
    // acceptance is only reachable when isInputValid() is true.
    QString password() const;

    // True when both fields are non-empty and equal — i.e. the state in which
    // OK is enabled and acceptance is allowed.
    bool isInputValid() const;

    // Exposed for regression tests and G2 evidence grabs.
    QPushButton *okButton() const { return m_okButton; }
    QLabel *hintLabel() const { return m_hint; }
    QLineEdit *passwordEdit() const { return m_pwEdit; }
    QLineEdit *confirmEdit() const { return m_confirmEdit; }

  private:
    // Recompute validity and update the OK button + hint. Called on every
    // keystroke in either field so the feedback is live.
    void revalidate();

    QLineEdit *m_pwEdit = nullptr;
    QLineEdit *m_confirmEdit = nullptr;
    QLabel *m_hint = nullptr;
    QPushButton *m_okButton = nullptr;
};

} // namespace trailer
