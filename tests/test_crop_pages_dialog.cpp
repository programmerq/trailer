// Regression guard for the Crop Pages "Apply to all pages" checkbox
// (audit 2026-07-15, Fix B). On a single-page document the checkbox
// offers no meaningful choice — there is nothing else to apply the crop
// to — so it must not be shown. On a multi-page document it is shown,
// checked by default, and reflected by applyToAllPages().
//
// Threshold (G1): CropPagesDialog(1) hides the "Apply to all pages"
// checkbox and reports applyToAllPages()==false; CropPagesDialog(>1)
// shows it and reports applyToAllPages()==true by default.
//
// Setting TRAILER_GRAB_DIR makes the test also write offscreen G2
// evidence grabs (crop-singlepage-after.png, crop-multipage-after.png).

#include "ui/CropPagesDialog.h"

#include <QCheckBox>
#include <QDir>
#include <QPixmap>
#include <QtTest/QtTest>

#include <cstdlib>

class TestCropPagesDialog : public QObject {
    Q_OBJECT
  private slots:
    void checkboxHiddenOnSinglePageDocument();
    void checkboxShownOnMultiPageDocument();

  private:
    static void maybeGrab(trailer::CropPagesDialog &dlg, const QString &name);
};

void TestCropPagesDialog::maybeGrab(trailer::CropPagesDialog &dlg,
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

void TestCropPagesDialog::checkboxHiddenOnSinglePageDocument() {
    trailer::CropPagesDialog dlg(/*pageCount=*/1);
    QVERIFY(dlg.applyToAllCheckBox() != nullptr);
    QVERIFY2(dlg.applyToAllCheckBox()->isHidden(),
             "single-page crop dialog must hide the 'Apply to all pages' "
             "checkbox — it offers no meaningful choice");
    QVERIFY2(!dlg.applyToAllPages(),
             "single-page crop must not report apply-to-all (falls to the "
             "current-page crop path)");
    maybeGrab(dlg, QStringLiteral("crop-singlepage-after.png"));
}

void TestCropPagesDialog::checkboxShownOnMultiPageDocument() {
    trailer::CropPagesDialog dlg(/*pageCount=*/5);
    QVERIFY(dlg.applyToAllCheckBox() != nullptr);
    QVERIFY2(!dlg.applyToAllCheckBox()->isHidden(),
             "multi-page crop dialog must show the 'Apply to all pages' "
             "checkbox");
    QVERIFY2(dlg.applyToAllPages(),
             "multi-page crop dialog defaults to apply-to-all");
    maybeGrab(dlg, QStringLiteral("crop-multipage-after.png"));
}

QTEST_MAIN(TestCropPagesDialog)
#include "test_crop_pages_dialog.moc"
