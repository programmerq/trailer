// Regression guard for inline validation in the "Export as Password-Protected
// PDF" dialog (backlog 2026-07-15-password-export-inline-validation). The
// earlier flow accepted any input and popped a QMessageBox::warning on
// mismatch/empty, throwing away the already-chosen destination. The new
// dialog validates inline: OK is disabled until both fields are non-empty and
// equal, with a hint (and matching tooltip) explaining why.
//
// Threshold (G1, from the backlog item): the OK button is disabled until both
// password fields are non-empty and equal, with an inline hint explaining
// why. Mismatched/empty passwords cannot be submitted.
//
// Setting TRAILER_GRAB_DIR makes the test also write offscreen G2 evidence
// grabs of each validation state.

#include "ui/PasswordExportDialog.h"

#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QtTest/QtTest>

#include <cstdlib>

class TestPasswordExportDialog : public QObject {
    Q_OBJECT
  private slots:
    void emptyBothFieldsIsInvalid();
    void passwordSetConfirmEmptyIsInvalid();
    void mismatchIsInvalid();
    void matchNonEmptyIsValid();
    void okTracksValidityLive();
    void hintMirroredInOkTooltip();

  private:
    static void maybeGrab(trailer::PasswordExportDialog &dlg,
                          const QString &name);
};

void TestPasswordExportDialog::maybeGrab(trailer::PasswordExportDialog &dlg,
                                         const QString &name) {
    const char *dir = std::getenv("TRAILER_GRAB_DIR");
    if (!dir || !*dir)
        return;
    QDir().mkpath(QString::fromLocal8Bit(dir));
    // Realise geometry offscreen so the grab reflects laid-out contents.
    dlg.show();
    dlg.grab().save(QDir(QString::fromLocal8Bit(dir)).filePath(name));
    dlg.hide();
}

void TestPasswordExportDialog::emptyBothFieldsIsInvalid() {
    trailer::PasswordExportDialog dlg;
    QVERIFY(dlg.okButton() != nullptr);
    QVERIFY2(!dlg.isInputValid(),
             "empty password + empty confirm must be invalid");
    QVERIFY2(!dlg.okButton()->isEnabled(),
             "OK must be disabled when both fields are empty");
    QVERIFY2(!dlg.hintLabel()->text().isEmpty(),
             "an inline hint must explain why OK is disabled");
    maybeGrab(dlg, QStringLiteral("password-export-empty-after.png"));
}

void TestPasswordExportDialog::passwordSetConfirmEmptyIsInvalid() {
    trailer::PasswordExportDialog dlg;
    dlg.passwordEdit()->setText(QStringLiteral("hunter2"));
    QVERIFY2(!dlg.isInputValid(),
             "password set but confirm empty must be invalid");
    QVERIFY2(!dlg.okButton()->isEnabled(),
             "OK must be disabled while confirm is empty");
}

void TestPasswordExportDialog::mismatchIsInvalid() {
    trailer::PasswordExportDialog dlg;
    dlg.passwordEdit()->setText(QStringLiteral("hunter2"));
    dlg.confirmEdit()->setText(QStringLiteral("hunter3"));
    QVERIFY2(!dlg.isInputValid(), "mismatched passwords must be invalid");
    QVERIFY2(!dlg.okButton()->isEnabled(),
             "OK must be disabled on mismatch");
    QVERIFY2(!dlg.hintLabel()->text().isEmpty(),
             "an inline hint must explain the mismatch");
    maybeGrab(dlg, QStringLiteral("password-export-mismatch-after.png"));
}

void TestPasswordExportDialog::matchNonEmptyIsValid() {
    trailer::PasswordExportDialog dlg;
    dlg.passwordEdit()->setText(QStringLiteral("hunter2"));
    dlg.confirmEdit()->setText(QStringLiteral("hunter2"));
    QVERIFY2(dlg.isInputValid(),
             "matching non-empty passwords must be valid");
    QVERIFY2(dlg.okButton()->isEnabled(),
             "OK must be enabled when both fields match and are non-empty");
    QCOMPARE(dlg.password(), QStringLiteral("hunter2"));
    maybeGrab(dlg, QStringLiteral("password-export-valid-after.png"));
}

void TestPasswordExportDialog::okTracksValidityLive() {
    trailer::PasswordExportDialog dlg;
    // Type a matching pair -> becomes valid.
    dlg.passwordEdit()->setText(QStringLiteral("s3cret"));
    dlg.confirmEdit()->setText(QStringLiteral("s3cret"));
    QVERIFY(dlg.okButton()->isEnabled());
    // Break the confirm -> OK must disable again without any submit.
    dlg.confirmEdit()->setText(QStringLiteral("s3cre"));
    QVERIFY2(!dlg.okButton()->isEnabled(),
             "editing a field back to an invalid state must re-disable OK "
             "live, without a submit round-trip");
    // Repair it -> valid again.
    dlg.confirmEdit()->setText(QStringLiteral("s3cret"));
    QVERIFY(dlg.okButton()->isEnabled());
}

void TestPasswordExportDialog::hintMirroredInOkTooltip() {
    trailer::PasswordExportDialog dlg;
    // While disabled, the OK button carries the "why" as a tooltip (G3).
    QVERIFY2(!dlg.okButton()->toolTip().isEmpty(),
             "disabled OK must carry a tooltip explaining why (G3)");
    QCOMPARE(dlg.okButton()->toolTip(), dlg.hintLabel()->text());
    // When valid, there is nothing to explain -> tooltip clears.
    dlg.passwordEdit()->setText(QStringLiteral("pw"));
    dlg.confirmEdit()->setText(QStringLiteral("pw"));
    QVERIFY2(dlg.okButton()->toolTip().isEmpty(),
             "enabled OK needs no disabled-reason tooltip");
}

QTEST_MAIN(TestPasswordExportDialog)
#include "test_password_export_dialog.moc"
