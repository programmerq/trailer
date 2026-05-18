// UAT harness — PDF page operations (undo / redo)
//
// Drives the Application + MainWindow in-process under
// QT_QPA_PLATFORM=offscreen. Each slot maps to a case in
// docs/uat/03-pdf-pages.md. The cases here all exercise the
// undo/redo loop for page-level qpdf mutations — the gap noted
// at UAT-PDF-014 / UAT-PDF-090 in that document. Specifically:
//
//   uat_pdf_014_deleteUndoRedo  → docs/uat/03-pdf-pages.md UAT-PDF-014
//   uat_pdf_024_moveUndoRedo    → new case (claims UAT-PDF-024 slot)
//   uat_pdf_035_insertUndoRedo  → new case (claims UAT-PDF-035 slot)
//   uat_pdf_056_cropUndoRedo    → new case (claims UAT-PDF-056 slot)
//
// The IDs were picked from gaps in the spec's numbering — Delete is
// the 010 block, Move is 020, Insert is 030, Crop is 050. Each new
// numeric slot ends in 4-6 to avoid colliding with anything the spec
// already calls out. When this file lands the spec will be updated
// in a follow-up pass to enumerate these cases explicitly.

#include "app/Application.h"
#include "document/IDocument.h"
#include "ui/DocumentView.h"
#include "ui/MainWindow.h"

#include <QDir>
#include <QPageSize>
#include <QPainter>
#include <QPdfDocument>
#include <QPdfView>
#include <QPdfWriter>
#include <QRect>
#include <QSizeF>
#include <QString>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <cmath>

using namespace trailer;

namespace {

MainWindow *currentMainWindow() {
    for (auto *w : QApplication::topLevelWidgets()) {
        if (auto *mw = qobject_cast<MainWindow *>(w))
            return mw;
    }
    return nullptr;
}

// Build a multi-page PDF with one page-numbered line per page so we
// can tell pages apart on a rendered thumbnail if necessary. Most
// assertions here are at the page-count level, but the writer is
// shared with the unit tests in test_pdf_editor.cpp so cropping a
// page that exists is non-trivial (i.e. has content to render).
QString writeSamplePdf(const QString &path, int pages) {
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    QPainter p(&writer);
    for (int i = 0; i < pages; ++i) {
        p.drawText(QRect(100, 100, 800, 200), Qt::AlignCenter,
                   QStringLiteral("Page %1").arg(i + 1));
        if (i < pages - 1)
            writer.newPage();
    }
    p.end();
    return path;
}

} // namespace

class TestUatPdfPages : public QObject {
    Q_OBJECT
  private slots:
    void init();

    void uat_pdf_014_deleteUndoRedo();
    void uat_pdf_024_moveUndoRedo();
    void uat_pdf_035_insertUndoRedo();
    void uat_pdf_056_cropUndoRedo();

  private:
    QTemporaryDir m_scratch;
};

void TestUatPdfPages::init() {
    for (auto *w : QApplication::topLevelWidgets()) {
        if (qobject_cast<MainWindow *>(w))
            w->close();
    }
    QApplication::processEvents();
}

// UAT-PDF-014 — Delete is undoable.
//
// Spec text marks this as a Known gap. This test pins the fix:
// open a 4-page PDF, delete pages 1 and 2 (indices), undo, verify
// the document returns to 4 pages; redo, verify it's 2 again.
void TestUatPdfPages::uat_pdf_014_deleteUndoRedo() {
    QVERIFY(m_scratch.isValid());
    const QString pdfPath =
        writeSamplePdf(m_scratch.filePath(QStringLiteral("uat_pdf_014.pdf")), 4);

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    IDocument *doc = dv->currentDocument();
    QVERIFY(doc);
    QCOMPARE(doc->pageCount(), 4);
    QVERIFY2(!doc->canUndo(), "Fresh doc should have nothing to undo");

    // Delete pages 1 and 2 (0-indexed). Removes the middle two of a
    // 4-page doc.
    doc->deletePages({1, 2});
    QApplication::processEvents();
    QCOMPARE(doc->pageCount(), 2);
    QVERIFY2(doc->canUndo(), "Delete must land on the PDF undo stack");
    QVERIFY2(doc->isDirty(), "Delete must mark the document dirty");

    // Undo restores the deleted pages.
    doc->undo();
    QApplication::processEvents();
    QCOMPARE(doc->pageCount(), 4);
    QVERIFY2(doc->canRedo(), "After undo the command must be in the redo stack");

    // Redo re-applies the delete.
    doc->redo();
    QApplication::processEvents();
    QCOMPARE(doc->pageCount(), 2);
}

// UAT-PDF-024 — Move is undoable.
//
// Open a 4-page PDF, move page 0 to position 3, undo, verify the
// document's page count is unchanged (4) and the redo stack is
// populated.
void TestUatPdfPages::uat_pdf_024_moveUndoRedo() {
    QVERIFY(m_scratch.isValid());
    const QString pdfPath =
        writeSamplePdf(m_scratch.filePath(QStringLiteral("uat_pdf_024.pdf")), 4);

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    IDocument *doc = dv->currentDocument();
    QVERIFY(doc);
    QCOMPARE(doc->pageCount(), 4);

    doc->movePage(0, 3);
    QApplication::processEvents();
    QCOMPARE(doc->pageCount(), 4); // move preserves count
    QVERIFY2(doc->canUndo(), "Move must land on the PDF undo stack");
    QVERIFY2(doc->isDirty(), "Move must mark the document dirty");

    doc->undo();
    QApplication::processEvents();
    QCOMPARE(doc->pageCount(), 4);
    QVERIFY2(doc->canRedo(), "Undo must push the command into the redo stack");

    doc->redo();
    QApplication::processEvents();
    QCOMPARE(doc->pageCount(), 4);
}

// UAT-PDF-035 — Insert pages from file is undoable.
//
// Open a 2-page PDF, insert a 3-page PDF at index 1, undo, verify
// the document returns to 2 pages.
void TestUatPdfPages::uat_pdf_035_insertUndoRedo() {
    QVERIFY(m_scratch.isValid());
    const QString basePath =
        writeSamplePdf(m_scratch.filePath(QStringLiteral("uat_pdf_035_base.pdf")), 2);
    const QString extraPath =
        writeSamplePdf(m_scratch.filePath(QStringLiteral("uat_pdf_035_extra.pdf")), 3);

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({basePath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    IDocument *doc = dv->currentDocument();
    QVERIFY(doc);
    QCOMPARE(doc->pageCount(), 2);

    QVERIFY(doc->insertPagesFrom(extraPath, 1));
    QApplication::processEvents();
    QCOMPARE(doc->pageCount(), 5);
    QVERIFY2(doc->canUndo(), "Insert must land on the PDF undo stack");
    QVERIFY2(doc->isDirty(), "Insert must mark the document dirty");

    doc->undo();
    QApplication::processEvents();
    QCOMPARE(doc->pageCount(), 2);
    QVERIFY2(doc->canRedo(), "Undo must push the command into the redo stack");

    doc->redo();
    QApplication::processEvents();
    QCOMPARE(doc->pageCount(), 5);
}

// UAT-PDF-056 — Crop is undoable.
//
// Open a PDF, crop a page, undo, verify the rendered page geometry
// matches the original. QPdfDocument::pagePointSize returns the
// /CropBox dimensions (falling back to /MediaBox), so a crop changes
// pagePointSize and the undo must restore it.
void TestUatPdfPages::uat_pdf_056_cropUndoRedo() {
    QVERIFY(m_scratch.isValid());
    const QString pdfPath =
        writeSamplePdf(m_scratch.filePath(QStringLiteral("uat_pdf_056.pdf")), 2);

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    IDocument *doc = dv->currentDocument();
    QVERIFY(doc);
    QCOMPARE(doc->pageCount(), 2);

    // Pull the QPdfDocument out via the QPdfView the adapter
    // installs in MainWindow. QPdfDocument::pagePointSize reflects
    // /CropBox (falling back to /MediaBox), so cropping changes
    // pagePointSize and the undo must restore it.
    auto *view = mw->findChild<QPdfView *>();
    QVERIFY2(view && view->document(),
             "MainWindow should host a QPdfView with a document");
    QPdfDocument *qpdf = view->document();

    const QSizeF originalSize = qpdf->pagePointSize(0);
    QVERIFY(!originalSize.isEmpty());

    // Crop 20 points off each edge. Resulting visible area should
    // shrink by 40pt in each dimension.
    QVERIFY(doc->cropPage(0, 20.0, 20.0, 20.0, 20.0));
    QApplication::processEvents();
    const QSizeF croppedSize = qpdf->pagePointSize(0);
    QVERIFY2(croppedSize.width() < originalSize.width() - 30.0,
             "Crop must shrink the visible width by ~40pt");
    QVERIFY2(croppedSize.height() < originalSize.height() - 30.0,
             "Crop must shrink the visible height by ~40pt");
    QVERIFY2(doc->canUndo(), "Crop must land on the PDF undo stack");
    QVERIFY2(doc->isDirty(), "Crop must mark the document dirty");

    doc->undo();
    QApplication::processEvents();
    const QSizeF afterUndo = qpdf->pagePointSize(0);
    // After undo the page should be back to its original geometry —
    // allow 1pt tolerance because QPdfWriter may quantise differently
    // on the reload path through the temp file.
    QVERIFY2(std::abs(afterUndo.width() - originalSize.width()) < 1.0,
             qPrintable(QStringLiteral("undo width %1, expected %2")
                            .arg(afterUndo.width())
                            .arg(originalSize.width())));
    QVERIFY2(std::abs(afterUndo.height() - originalSize.height()) < 1.0,
             qPrintable(QStringLiteral("undo height %1, expected %2")
                            .arg(afterUndo.height())
                            .arg(originalSize.height())));
    QVERIFY2(doc->canRedo(), "Undo must push the command into the redo stack");

    doc->redo();
    QApplication::processEvents();
    const QSizeF afterRedo = qpdf->pagePointSize(0);
    QVERIFY2(afterRedo.width() < originalSize.width() - 30.0,
             "Redo must re-apply the crop");
}

int main(int argc, char **argv) {
    QTemporaryDir fakeHome;
    if (!fakeHome.isValid())
        return 1;
    qputenv("HOME", fakeHome.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (fakeHome.path() + "/.config").toUtf8());
    qputenv("XDG_DATA_HOME", (fakeHome.path() + "/.local/share").toUtf8());
    QDir().mkpath(fakeHome.path() + "/.config/trailer");
    QDir().mkpath(fakeHome.path() + "/.local/share/trailer");

    Application app(argc, argv);
    TestUatPdfPages tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_uat_pdf_pages.moc"
