// UAT harness — unified undo log past the annotation history cap
//
// Drives the Application + MainWindow in-process under
// QT_QPA_PLATFORM=offscreen, following the pattern of
// test_uat_pdf_pages.cpp.
//
//   uat_und_150_overCapInterleavedUndoRedo → regression guard for the
//   cap-desync bug: AnnotationStore bounds its snapshot history while
//   the document's chronological undo log used to grow without bound.
//   After more annotation edits than the (old, 64-frame) cap, undo-all
//   silently no-opped the excess — annotations the user "undid" were
//   stranded — and pushed phantom redo entries. The fix keeps the log
//   and the store in lockstep via AnnotationStore::historyEvicted().
//
// The scenario: 70 annotation edits + 1 page rotate on a live
// MainWindow, undo-all (every offered press must be real), redo-all
// (exactly as many presses as undos — no phantoms). When
// TRAILER_UAT_EVIDENCE_DIR is set, the test saves QWidget::grab()
// screenshots of the mid-sequence and end states there as PNG
// evidence for review.

#include "annotation/Annotation.h"
#include "annotation/AnnotationStore.h"
#include "app/Application.h"
#include "document/IDocument.h"
#include "ui/DocumentView.h"
#include "ui/MainWindow.h"

#include <QDir>
#include <QMenu>
#include <QMenuBar>
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

using namespace trailer;

namespace {

MainWindow *currentMainWindow() {
    for (auto *w : QApplication::topLevelWidgets()) {
        if (auto *mw = qobject_cast<MainWindow *>(w))
            return mw;
    }
    return nullptr;
}

QAction *findMenuAction(QMenuBar *bar, const QString &menuTitle, const QString &actionText) {
    for (QAction *menuAction : bar->actions()) {
        if (menuAction->text() != menuTitle || !menuAction->menu())
            continue;
        for (QAction *action : menuAction->menu()->actions()) {
            if (action->text() == actionText)
                return action;
        }
    }
    return nullptr;
}

// Save a screenshot of the window when evidence collection is enabled
// (TRAILER_UAT_EVIDENCE_DIR set). Off by default so a plain ctest run
// leaves no artefacts behind.
void saveEvidence(QWidget *w, const QString &fileName) {
    const QString dir = qEnvironmentVariable("TRAILER_UAT_EVIDENCE_DIR");
    if (dir.isEmpty() || !w)
        return;
    QDir().mkpath(dir);
    const QString path = QDir(dir).filePath(fileName);
    if (!w->grab().save(path, "PNG"))
        qWarning("uat_und_150: failed to save evidence screenshot %s", qPrintable(path));
}

} // namespace

class TestUatUndoInterleavedCap : public QObject {
    Q_OBJECT
  private slots:
    void init();

    void uat_und_150_overCapInterleavedUndoRedo();

  private:
    QTemporaryDir m_scratch;
};

void TestUatUndoInterleavedCap::init() {
    for (auto *w : QApplication::topLevelWidgets()) {
        if (qobject_cast<MainWindow *>(w))
            w->close();
    }
    QApplication::processEvents();
}

// UAT-UND-150 — undo-all / redo-all stays exact past the annotation
// history cap regime, interleaved with a page rotate.
void TestUatUndoInterleavedCap::uat_und_150_overCapInterleavedUndoRedo() {
    QVERIFY(m_scratch.isValid());
    const QString pdfPath = m_scratch.filePath(QStringLiteral("uat_und_150.pdf"));
    {
        QPdfWriter writer(pdfPath);
        writer.setPageSize(QPageSize(QPageSize::A4));
        QPainter p(&writer);
        p.drawText(QRect(100, 100, 800, 200), Qt::AlignCenter, QStringLiteral("Over-cap undo"));
        p.end();
    }

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    mw->resize(1100, 750);
    QApplication::processEvents();

    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    IDocument *doc = dv->currentDocument();
    QVERIFY(doc);
    QCOMPARE(doc->pageCount(), 1);
    AnnotationStore *store = doc->annotations();
    QVERIFY(store);
    QCOMPARE(store->count(), 0);
    QVERIFY(!doc->canUndo());

    // The user-facing undo path exists — the loop below drives the
    // same IDocument::undo / redo those actions invoke.
    QVERIFY2(findMenuAction(mw->menuBar(), QStringLiteral("&Edit"), QStringLiteral("&Undo")),
             "Edit > Undo action not found");
    QVERIFY2(findMenuAction(mw->menuBar(), QStringLiteral("&Edit"), QStringLiteral("&Redo")),
             "Edit > Redo action not found");

    // Rotation observable: QPdfDocument::pagePointSize follows the
    // page's effective orientation through the reload path.
    auto *view = mw->findChild<QPdfView *>();
    QVERIFY2(view && view->document(), "MainWindow should host a QPdfView with a document");
    QPdfDocument *qpdf = view->document();
    const QSizeF portrait = qpdf->pagePointSize(0);
    QVERIFY(portrait.height() > portrait.width());

    // 70 annotation edits (past the old 64-frame cap), then a rotate.
    for (int i = 0; i < 70; ++i) {
        Annotation a;
        a.page = 0;
        a.type = AnnotationType::Rectangle;
        a.bounds = QRectF(20 + (i % 10) * 55, 30 + (i / 10) * 55, 45, 40);
        store->add(a);
    }
    QCOMPARE(store->count(), 70);
    doc->rotatePage(0, 90);
    QApplication::processEvents();
    const QSizeF landscape = qpdf->pagePointSize(0);
    QVERIFY2(landscape.width() > landscape.height(), "rotate must land the page landscape");

    saveEvidence(mw, QStringLiteral("uat-undo-cap-evidence-mid.png"));

    // Undo-all: every press offered by canUndo() must actually revert
    // something. 71 total (70 annotations + 1 rotate); a silent no-op
    // press or an extra offered entry is the cap-desync bug.
    int undos = 0;
    while (doc->canUndo()) {
        QVERIFY2(doc->undo(), "undo() returned false while canUndo() was true");
        ++undos;
        QVERIFY2(undos <= 71, "undo log offered more entries than operations performed");
    }
    QApplication::processEvents();
    QCOMPARE(undos, 71);
    QCOMPARE(store->count(), 0);
    const QSizeF afterUndo = qpdf->pagePointSize(0);
    QVERIFY2(afterUndo.height() > afterUndo.width(), "page must be back to portrait");
    QVERIFY(!doc->canUndo());
    QVERIFY2(!doc->undo(), "undo() must refuse (return false) once canUndo() is false");
    QCOMPARE(store->count(), 0);

    // Redo-all: exactly as many presses as undos — no phantom entries.
    int redos = 0;
    while (doc->canRedo()) {
        QVERIFY2(doc->redo(), "redo() returned false while canRedo() was true");
        ++redos;
        QVERIFY2(redos <= undos, "phantom redo entries beyond the number of undos");
    }
    QApplication::processEvents();
    QCOMPARE(redos, 71);
    QCOMPARE(store->count(), 70);
    const QSizeF afterRedo = qpdf->pagePointSize(0);
    QVERIFY2(afterRedo.width() > afterRedo.height(), "redo-all must restore the rotation");
    QVERIFY(!doc->canRedo());

    saveEvidence(mw, QStringLiteral("uat-undo-cap-evidence.png"));
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
    TestUatUndoInterleavedCap tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_uat_undo_interleaved_cap.moc"
