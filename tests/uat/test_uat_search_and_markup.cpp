// UAT harness — Search & Markup
//
// Extends the in-process UAT runner with coverage for two areas that
// users hit first on a real document: Find-in-PDF and the markup-undo
// loop. Each slot maps to a case in docs/uat; the slot name ends with
// the spec ID so a failing test points straight at the prose.
//
//   uat_vwr_061_findMatchesInPdfText     → docs/uat/02-viewer.md UAT-VWR-061
//   uat_ann_010_rectangleToolCreates…    → docs/uat/05-annotations.md UAT-ANN-010
//   uat_ann_012_lineToolCreates…         → docs/uat/05-annotations.md UAT-ANN-012
//   uat_ann_060_undoAddRectangle         → docs/uat/05-annotations.md UAT-ANN-060
//
// These cases are deliberately thin: they drive the same public entry
// points a user would hit (menu actions, toolbar actions, mouse drags
// on the overlay) and assert on observable state (the document's
// AnnotationStore, the QPdfView's search model). When a case starts
// failing, the fix lives in the matching source unit, not here.

#include "annotation/AnnotationStore.h"
#include "app/Application.h"
#include "document/IDocument.h"
#include "ui/AnnotationOverlay.h"
#include "ui/DocumentView.h"
#include "ui/MainWindow.h"
#include "ui/MarkupToolbar.h"

#include <QAction>
#include <QFont>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPageSize>
#include <QPainter>
#include <QPdfSearchModel>
#include <QPdfView>
#include <QPdfWriter>
#include <QTemporaryDir>
#include <QToolBar>
#include <QtTest/QtTest>

using namespace trailer;

namespace {

MainWindow* currentMainWindow() {
    for (auto* w : QApplication::topLevelWidgets()) {
        if (auto* mw = qobject_cast<MainWindow*>(w)) return mw;
    }
    return nullptr;
}

QAction* findMenuAction(QMenuBar* bar, const QString& topText, const QString& itemText) {
    for (QAction* topAction : bar->actions()) {
        if (topAction->text() == topText) {
            QMenu* menu = topAction->menu();
            if (!menu) return nullptr;
            for (QAction* action : menu->actions()) {
                if (action->text() == itemText) return action;
            }
        }
    }
    return nullptr;
}

QAction* findToolAction(MarkupToolbar* bar, const QString& label) {
    for (QAction* a : bar->actions()) {
        if (a->text() == label) return a;
    }
    return nullptr;
}

// Writes a one-page A4 PDF containing `keyword` as real (selectable,
// searchable) text. QPdfWriter emits Tj operators for QPainter::drawText,
// so QPdfSearchModel finds the text the same way it would in a PDF
// produced by a print-to-PDF tool.
QString writePdfWithKeyword(const QString& path, const QString& keyword) {
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    QPainter p(&writer);
    QFont font(QStringLiteral("Helvetica"));
    font.setPointSize(24);
    p.setFont(font);
    p.drawText(300, 400,
               QStringLiteral("Trailer UAT fixture — keyword: ") + keyword);
    p.end();
    return path;
}

// Writes a one-page PDF that imitates the structure an OCR tool
// (Tesseract → pdfsandwich, Acrobat OCR, etc.) produces: a scanned
// raster on top, with an invisible text layer behind it carrying the
// machine-readable text for search and copy. The trick is a pen with
// alpha = 0, which makes QPainter emit real Tj text operators but
// with fully-transparent ink — visually invisible, textually
// searchable. If QPdfSearchModel still finds the keyword here, the
// search-on-OCR path works end to end; if not, we've reproduced the
// user's reported regression in a regression-gated fixture.
QString writeOcrLayerPdf(const QString& path, const QString& keyword) {
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    QPainter p(&writer);

    // 1. Raster: a light-grey rectangle stands in for a scanned page.
    //    Real scans would be a QImage, but the shape of the PDF object
    //    graph is the same — image first, text on top.
    p.fillRect(0, 0, writer.width(), writer.height(), QColor(240, 240, 240));

    // 2. Invisible text layer.
    QFont font(QStringLiteral("Helvetica"));
    font.setPointSize(24);
    p.setFont(font);
    p.setPen(QColor(0, 0, 0, 0));  // fully transparent — the OCR trick
    p.drawText(300, 400,
               QStringLiteral("OCR fixture — keyword: ") + keyword);
    p.end();
    return path;
}

// Sends a synthesized QMouseEvent directly to `target`. QTest::mouseClick
// style helpers require the widget to be visible on a real display —
// offscreen is finicky about that — but sendEvent is happy as long as
// the widget exists, which is exactly what we need for the overlay.
void sendMouse(QWidget* target, QEvent::Type type, QPoint pos, Qt::MouseButton button) {
    const QPoint globalPos = target->mapToGlobal(pos);
    const Qt::MouseButtons buttonsHeld =
        (type == QEvent::MouseButtonRelease) ? Qt::NoButton : Qt::MouseButtons(button);
    QMouseEvent ev(type, pos, globalPos, button, buttonsHeld, Qt::NoModifier);
    QApplication::sendEvent(target, &ev);
}

// Simulates a click-drag on the overlay: press at `start`, two move
// events (enough to register motion for tools that accumulate points
// like Ink), release at `end`.
void dragOnOverlay(AnnotationOverlay* overlay, QPoint start, QPoint end) {
    sendMouse(overlay, QEvent::MouseButtonPress, start, Qt::LeftButton);
    const QPoint mid((start.x() + end.x()) / 2, (start.y() + end.y()) / 2);
    sendMouse(overlay, QEvent::MouseMove, mid, Qt::LeftButton);
    sendMouse(overlay, QEvent::MouseMove, end, Qt::LeftButton);
    sendMouse(overlay, QEvent::MouseButtonRelease, end, Qt::LeftButton);
    QApplication::processEvents();
}

}  // namespace

class TestUatSearchAndMarkup : public QObject {
    Q_OBJECT
private slots:
    void init();

    void uat_vwr_061_findMatchesInPdfText();
    void uat_vwr_061b_findMatchesInOcrLayerPdf();
    void uat_ann_010_rectangleToolCreatesAnnotation();
    void uat_ann_012_lineToolCreatesAnnotation();
    void uat_ann_060_undoAddRectangle();

private:
    QTemporaryDir m_scratch;
};

void TestUatSearchAndMarkup::init() {
    for (auto* w : QApplication::topLevelWidgets()) {
        if (qobject_cast<MainWindow*>(w)) w->close();
    }
    QApplication::processEvents();
}

// UAT-VWR-061 — Find matches in a PDF.
//
// Open a PDF containing a known keyword. Trigger Edit > Find. Put the
// keyword in the search bar. Expect the PDF view's search model to
// report at least one match. QPdfSearchModel runs its search on a
// worker thread, so poll via QTRY_VERIFY.
void TestUatSearchAndMarkup::uat_vwr_061_findMatchesInPdfText() {
    QVERIFY(m_scratch.isValid());
    const QString keyword = QStringLiteral("zebranaut");
    const QString pdfPath = writePdfWithKeyword(
        m_scratch.filePath(QStringLiteral("uat_vwr_061.pdf")), keyword);

    auto* app = qobject_cast<Application*>(qApp);
    QVERIFY(app);
    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow* mw = currentMainWindow();
    QVERIFY(mw);

    auto* dv = mw->findChild<DocumentView*>();
    QVERIFY(dv);
    IDocument* doc = dv->currentDocument();
    QVERIFY(doc);
    QVERIFY2(doc->supportsSearch(), "PDF document should report supportsSearch()");

    QAction* findAction = findMenuAction(mw->menuBar(), QStringLiteral("&Edit"),
                                         QStringLiteral("&Find…"));
    QVERIFY2(findAction, "Edit > Find… action not found");
    findAction->trigger();
    QApplication::processEvents();

    // Drive the query via the real SearchBar QLineEdit so we exercise
    // the same wire-up a user hits. setText() fires textChanged, which
    // SearchBar forwards as queryChanged → doc->setSearchQuery.
    auto* lineEdit = mw->findChild<QLineEdit*>();
    QVERIFY2(lineEdit, "SearchBar QLineEdit not found");
    lineEdit->setText(keyword);
    QApplication::processEvents();

    auto* view = mw->findChild<QPdfView*>();
    QVERIFY2(view, "QPdfView not found in MainWindow children");

    // QPdfSearchModel::setSearchString kicks off an async search. Wait
    // up to five seconds for at least one match to land. If this times
    // out with a PDF that contains the literal keyword as selectable
    // text, search is broken — which is what the user reported.
    QTRY_VERIFY_WITH_TIMEOUT(
        view->searchModel() != nullptr
            && view->searchModel()->rowCount(QModelIndex()) > 0,
        5000);

    // The model having matches isn't enough: the view also has to be
    // told which match is current so it scrolls to and emphasises the
    // first hit. If currentSearchResultIndex stays at -1 after the
    // async search populates, the user sees "nothing" — which is the
    // exact regression this case guards against. Poll so a late assign
    // from a rowsInserted slot still counts.
    QTRY_VERIFY_WITH_TIMEOUT(view->currentSearchResultIndex() >= 0, 5000);
}

// UAT-VWR-061b — Find matches in a PDF whose text layer is invisible
// (i.e. an OCR'd scan). This is what the user actually hit: searching
// in a Tesseract/pdfsandwich output returned zero highlights even
// though the text was selectable. The fixture paints the keyword with
// a fully-transparent pen on top of a grey "scan" rectangle, which
// reproduces the object-graph shape of a real OCR PDF: invisible Tj
// operators over a raster. If QPdfSearchModel picks this up, the
// search path is sound end to end.
void TestUatSearchAndMarkup::uat_vwr_061b_findMatchesInOcrLayerPdf() {
    QVERIFY(m_scratch.isValid());
    const QString keyword = QStringLiteral("ocrphant");
    const QString pdfPath = writeOcrLayerPdf(
        m_scratch.filePath(QStringLiteral("uat_vwr_061b.pdf")), keyword);

    auto* app = qobject_cast<Application*>(qApp);
    QVERIFY(app);
    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow* mw = currentMainWindow();
    QVERIFY(mw);

    auto* dv = mw->findChild<DocumentView*>();
    QVERIFY(dv);
    IDocument* doc = dv->currentDocument();
    QVERIFY(doc);
    QVERIFY2(doc->supportsSearch(), "PDF document should report supportsSearch()");

    QAction* findAction = findMenuAction(mw->menuBar(), QStringLiteral("&Edit"),
                                         QStringLiteral("&Find…"));
    QVERIFY2(findAction, "Edit > Find… action not found");
    findAction->trigger();
    QApplication::processEvents();

    auto* lineEdit = mw->findChild<QLineEdit*>();
    QVERIFY2(lineEdit, "SearchBar QLineEdit not found");
    lineEdit->setText(keyword);
    QApplication::processEvents();

    auto* view = mw->findChild<QPdfView*>();
    QVERIFY2(view, "QPdfView not found in MainWindow children");

    QTRY_VERIFY_WITH_TIMEOUT(
        view->searchModel() != nullptr
            && view->searchModel()->rowCount(QModelIndex()) > 0,
        5000);

    QTRY_VERIFY_WITH_TIMEOUT(view->currentSearchResultIndex() >= 0, 5000);
}

// UAT-ANN-010 — Rectangle tool creates an annotation on click-drag.
void TestUatSearchAndMarkup::uat_ann_010_rectangleToolCreatesAnnotation() {
    QVERIFY(m_scratch.isValid());
    const QString pdfPath = writePdfWithKeyword(
        m_scratch.filePath(QStringLiteral("uat_ann_010.pdf")),
        QStringLiteral("fixture"));

    auto* app = qobject_cast<Application*>(qApp);
    QVERIFY(app);
    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow* mw = currentMainWindow();
    QVERIFY(mw);
    mw->resize(1100, 750);
    QApplication::processEvents();

    auto* dv = mw->findChild<DocumentView*>();
    QVERIFY(dv);
    IDocument* doc = dv->currentDocument();
    QVERIFY(doc);
    AnnotationStore* store = doc->annotations();
    QVERIFY(store);
    const int before = store->count();

    auto* markup = mw->findChild<MarkupToolbar*>();
    QVERIFY(markup);
    QAction* rectAction = findToolAction(markup, QStringLiteral("Rectangle"));
    QVERIFY2(rectAction, "Markup toolbar Rectangle action not found");
    rectAction->setChecked(true);  // exclusive group → emits toggled(true)
    QApplication::processEvents();

    auto* overlay = mw->findChild<AnnotationOverlay*>();
    QVERIFY2(overlay, "AnnotationOverlay not found as child of MainWindow");
    QCOMPARE(overlay->activeTool(), AnnotationTool::Rectangle);

    dragOnOverlay(overlay, QPoint(200, 250), QPoint(320, 340));

    QCOMPARE(store->count(), before + 1);
    QVERIFY2(store->canUndo(),
             "Store should report canUndo() after adding a rectangle");
    QCOMPARE(store->annotations().back().type, AnnotationType::Rectangle);
}

// UAT-ANN-012 — Line tool creates a Line annotation with two endpoints.
void TestUatSearchAndMarkup::uat_ann_012_lineToolCreatesAnnotation() {
    QVERIFY(m_scratch.isValid());
    const QString pdfPath = writePdfWithKeyword(
        m_scratch.filePath(QStringLiteral("uat_ann_012.pdf")),
        QStringLiteral("fixture"));

    auto* app = qobject_cast<Application*>(qApp);
    QVERIFY(app);
    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow* mw = currentMainWindow();
    QVERIFY(mw);
    mw->resize(1100, 750);
    QApplication::processEvents();

    auto* dv = mw->findChild<DocumentView*>();
    IDocument* doc = dv->currentDocument();
    QVERIFY(doc);
    AnnotationStore* store = doc->annotations();
    const int before = store->count();

    auto* markup = mw->findChild<MarkupToolbar*>();
    QVERIFY(markup);
    QAction* lineAction = findToolAction(markup, QStringLiteral("Line"));
    QVERIFY(lineAction);
    lineAction->setChecked(true);
    QApplication::processEvents();

    auto* overlay = mw->findChild<AnnotationOverlay*>();
    QVERIFY(overlay);
    QCOMPARE(overlay->activeTool(), AnnotationTool::Line);

    dragOnOverlay(overlay, QPoint(180, 240), QPoint(340, 360));

    QCOMPARE(store->count(), before + 1);
    const Annotation& added = store->annotations().back();
    QCOMPARE(added.type, AnnotationType::Line);
    QCOMPARE(added.points.size(), size_t{2});
    QVERIFY2(store->canUndo(),
             "Store should report canUndo() after adding a line");
}

// UAT-ANN-060 — Undo removes the most recent rectangle add.
//
// Drives the same flow as UAT-ANN-010 and then invokes Edit > Undo,
// expecting the store to return to its prior count. This is the user
// escape hatch when a markup tool drag goes wrong.
void TestUatSearchAndMarkup::uat_ann_060_undoAddRectangle() {
    QVERIFY(m_scratch.isValid());
    const QString pdfPath = writePdfWithKeyword(
        m_scratch.filePath(QStringLiteral("uat_ann_060.pdf")),
        QStringLiteral("fixture"));

    auto* app = qobject_cast<Application*>(qApp);
    QVERIFY(app);
    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow* mw = currentMainWindow();
    QVERIFY(mw);
    mw->resize(1100, 750);
    QApplication::processEvents();

    auto* dv = mw->findChild<DocumentView*>();
    IDocument* doc = dv->currentDocument();
    QVERIFY(doc);
    AnnotationStore* store = doc->annotations();
    const int baseline = store->count();

    auto* markup = mw->findChild<MarkupToolbar*>();
    QAction* rectAction = findToolAction(markup, QStringLiteral("Rectangle"));
    QVERIFY(rectAction);
    rectAction->setChecked(true);
    QApplication::processEvents();

    auto* overlay = mw->findChild<AnnotationOverlay*>();
    QVERIFY(overlay);

    dragOnOverlay(overlay, QPoint(200, 250), QPoint(320, 340));
    QCOMPARE(store->count(), baseline + 1);

    QAction* undoAction = findMenuAction(mw->menuBar(), QStringLiteral("&Edit"),
                                         QStringLiteral("&Undo"));
    QVERIFY2(undoAction, "Edit > Undo action not found");
    QVERIFY2(undoAction->isEnabled(),
             "Undo action should be enabled after adding an annotation");
    undoAction->trigger();
    QApplication::processEvents();

    QCOMPARE(store->count(), baseline);
}

// Custom main mirrors test_uat_foundations.cpp: sandbox HOME / XDG
// so Settings and RecentFiles don't touch the user's real config.
int main(int argc, char** argv) {
    QTemporaryDir fakeHome;
    if (!fakeHome.isValid()) return 1;
    qputenv("HOME", fakeHome.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (fakeHome.path() + "/.config").toUtf8());
    qputenv("XDG_DATA_HOME", (fakeHome.path() + "/.local/share").toUtf8());
    QDir().mkpath(fakeHome.path() + "/.config/trailer");
    QDir().mkpath(fakeHome.path() + "/.local/share/trailer");

    Application app(argc, argv);
    TestUatSearchAndMarkup tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_uat_search_and_markup.moc"
