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
#include "recent/RecentFiles.h"
#include "settings/DocumentTypeDefaults.h"
#include "ui/AnnotationOverlay.h"
#include "ui/DocumentView.h"
#include "ui/MainWindow.h"
#include "ui/MarkupToolbar.h"
#include "ui/SearchBar.h"
#include "ui/Sidebar.h"

#include <QAction>
#include <QColorDialog>
#include <QDockWidget>
#include <QFont>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPageSize>
#include <QPainter>
#include <QPdfSearchModel>
#include <QPdfView>
#include <QPdfWriter>
#include <QPlainTextEdit>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QtTest/QtTest>

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>

#include <memory>

using namespace trailer;

namespace {

MainWindow *currentMainWindow() {
    for (auto *w : QApplication::topLevelWidgets()) {
        if (auto *mw = qobject_cast<MainWindow *>(w))
            return mw;
    }
    return nullptr;
}

QAction *findMenuAction(QMenuBar *bar, const QString &topText, const QString &itemText) {
    for (QAction *topAction : bar->actions()) {
        if (topAction->text() == topText) {
            QMenu *menu = topAction->menu();
            if (!menu)
                return nullptr;
            for (QAction *action : menu->actions()) {
                if (action->text() == itemText)
                    return action;
            }
        }
    }
    return nullptr;
}

QAction *findToolAction(MarkupToolbar *bar, const QString &label) {
    for (QAction *a : bar->actions()) {
        if (a->text() == label)
            return a;
    }
    return nullptr;
}

// Builds a 3-page PDF with an /Outlines tree pointing one bookmark
// at each page. Uses qpdf directly because QPdfWriter doesn't expose
// outline construction. The structure matches the minimal /Outlines
// shape from PDF spec §12.3.3:
//   Catalog → /Outlines → root dict { /First /Last /Count }
//   root.First → item1 { /Title /Parent /Next /Dest [page /XYZ] }
//   item1.Next → item2 { /Title /Parent /Prev /Next /Dest [page] }
//   item2.Next → item3 { /Title /Parent /Prev /Dest [page] }
QString writePdfWithOutline(const QString& path,
                            const QStringList& titles) {
    QPDF pdf;
    pdf.emptyPDF();

    auto makeRect = [](double x0, double y0, double x1, double y1) {
        QPDFObjectHandle r = QPDFObjectHandle::newArray();
        for (double v : {x0, y0, x1, y1}) {
            r.appendItem(QPDFObjectHandle::newReal(v));
        }
        return r;
    };

    const int pageCount = static_cast<int>(titles.size());

    // Build /Pages root first so each page's /Parent can point at it.
    QPDFObjectHandle pagesDict = QPDFObjectHandle::newDictionary();
    pagesDict.replaceKey("/Type", QPDFObjectHandle::newName("/Pages"));
    QPDFObjectHandle pagesObj = pdf.makeIndirectObject(pagesDict);

    // Build N empty pages and collect their indirect handles.
    std::vector<QPDFObjectHandle> pageObjs;
    QPDFObjectHandle kids = QPDFObjectHandle::newArray();
    for (int i = 0; i < pageCount; ++i) {
        QPDFObjectHandle page = QPDFObjectHandle::newDictionary();
        page.replaceKey("/Type", QPDFObjectHandle::newName("/Page"));
        page.replaceKey("/MediaBox", makeRect(0, 0, 612, 792));
        page.replaceKey("/Resources", QPDFObjectHandle::newDictionary());
        page.replaceKey("/Parent", pagesObj);
        QPDFObjectHandle pageObj = pdf.makeIndirectObject(page);
        pageObjs.push_back(pageObj);
        kids.appendItem(pageObj);
    }
    pagesDict.replaceKey("/Kids", kids);
    pagesDict.replaceKey("/Count", QPDFObjectHandle::newInteger(pageCount));

    QPDFObjectHandle root = pdf.getRoot();
    root.replaceKey("/Pages", pagesObj);

    // Build the /Outlines tree. Spec §12.3.3:
    //   root → /First, /Last, /Count
    //   each item → /Title, /Parent, /Prev?, /Next?, /Dest
    QPDFObjectHandle outlinesRoot = QPDFObjectHandle::newDictionary();
    outlinesRoot.replaceKey("/Type", QPDFObjectHandle::newName("/Outlines"));
    QPDFObjectHandle outlinesObj = pdf.makeIndirectObject(outlinesRoot);

    std::vector<QPDFObjectHandle> itemObjs;
    for (int i = 0; i < pageCount; ++i) {
        QPDFObjectHandle item = QPDFObjectHandle::newDictionary();
        item.replaceKey("/Title",
            QPDFObjectHandle::newUnicodeString(titles[i].toStdString()));
        item.replaceKey("/Parent", outlinesObj);
        // Destination: jump to top of page at current zoom.
        QPDFObjectHandle dest = QPDFObjectHandle::newArray();
        dest.appendItem(pageObjs[static_cast<size_t>(i)]);
        dest.appendItem(QPDFObjectHandle::newName("/XYZ"));
        dest.appendItem(QPDFObjectHandle::newInteger(0));
        dest.appendItem(QPDFObjectHandle::newInteger(792));
        dest.appendItem(QPDFObjectHandle::newNull());
        item.replaceKey("/Dest", dest);
        itemObjs.push_back(pdf.makeIndirectObject(item));
    }
    for (size_t i = 0; i < itemObjs.size(); ++i) {
        if (i > 0) itemObjs[i].replaceKey("/Prev", itemObjs[i - 1]);
        if (i + 1 < itemObjs.size()) {
            itemObjs[i].replaceKey("/Next", itemObjs[i + 1]);
        }
    }
    if (!itemObjs.empty()) {
        outlinesObj.replaceKey("/First", itemObjs.front());
        outlinesObj.replaceKey("/Last",  itemObjs.back());
        outlinesObj.replaceKey("/Count",
            QPDFObjectHandle::newInteger(int(itemObjs.size())));
    }
    root.replaceKey("/Outlines", outlinesObj);

    QPDFWriter writer(pdf, path.toLocal8Bit().constData());
    writer.write();
    return path;
}

// Writes a one-page A4 PDF containing `keyword` as real (selectable,
// searchable) text. QPdfWriter emits Tj operators for QPainter::drawText,
// so QPdfSearchModel finds the text the same way it would in a PDF
// produced by a print-to-PDF tool.
QString writePdfWithKeyword(const QString &path, const QString &keyword) {
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    QPainter p(&writer);
    QFont font(QStringLiteral("Helvetica"));
    font.setPointSize(24);
    p.setFont(font);
    p.drawText(300, 400, QStringLiteral("Trailer UAT fixture — keyword: ") + keyword);
    p.end();
    return path;
}

// Like writePdfWithKeyword, but stamps the keyword `count` times at
// vertically staggered positions so there are multiple distinct
// matches for Find Next / Find Previous to walk through.
QString writePdfWithKeywordTimes(const QString &path, const QString &keyword, int count) {
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    QPainter p(&writer);
    QFont font(QStringLiteral("Helvetica"));
    font.setPointSize(24);
    p.setFont(font);
    for (int i = 0; i < count; ++i) {
        p.drawText(300, 400 + i * 800,
                   QStringLiteral("Trailer fixture hit #%1: %2").arg(i).arg(keyword));
    }
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
QString writeOcrLayerPdf(const QString &path, const QString &keyword) {
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
    p.setPen(QColor(0, 0, 0, 0)); // fully transparent — the OCR trick
    p.drawText(300, 400, QStringLiteral("OCR fixture — keyword: ") + keyword);
    p.end();
    return path;
}

// Sends a synthesized QKeyEvent directly to `target`. Same rationale
// as sendMouse below — offscreen is happier with sendEvent than the
// QTest::keyClick helpers.
void sendKey(QWidget *target, Qt::Key key) {
    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
    QApplication::sendEvent(target, &press);
    QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
    QApplication::sendEvent(target, &release);
}

// Sends a synthesized QMouseEvent directly to `target`. QTest::mouseClick
// style helpers require the widget to be visible on a real display —
// offscreen is finicky about that — but sendEvent is happy as long as
// the widget exists, which is exactly what we need for the overlay.
void sendMouse(QWidget *target, QEvent::Type type, QPoint pos, Qt::MouseButton button) {
    const QPoint globalPos = target->mapToGlobal(pos);
    const Qt::MouseButtons buttonsHeld =
        (type == QEvent::MouseButtonRelease) ? Qt::NoButton : Qt::MouseButtons(button);
    QMouseEvent ev(type, pos, globalPos, button, buttonsHeld, Qt::NoModifier);
    QApplication::sendEvent(target, &ev);
}

// Simulates a click-drag on the overlay: press at `start`, two move
// events (enough to register motion for tools that accumulate points
// like Ink), release at `end`.
void dragOnOverlay(AnnotationOverlay *overlay, QPoint start, QPoint end) {
    sendMouse(overlay, QEvent::MouseButtonPress, start, Qt::LeftButton);
    const QPoint mid((start.x() + end.x()) / 2, (start.y() + end.y()) / 2);
    sendMouse(overlay, QEvent::MouseMove, mid, Qt::LeftButton);
    sendMouse(overlay, QEvent::MouseMove, end, Qt::LeftButton);
    sendMouse(overlay, QEvent::MouseButtonRelease, end, Qt::LeftButton);
    QApplication::processEvents();
}

} // namespace

class TestUatSearchAndMarkup : public QObject {
    Q_OBJECT
  private slots:
    void init();

    void uat_vwr_061_findMatchesInPdfText();
    void uat_vwr_061b_findMatchesInOcrLayerPdf();
    void uat_vwr_062_findNextPrevWrap();
    void uat_vwr_063_escapeClosesSearch();
    void uat_vwr_064_searchHighlightsFillOverlay();
    void uat_vwr_065_searchWithNoMatches();
    void uat_vwr_066_searchOpensSidebarWithMatchPages();
    void uat_vwr_067_searchShowsMatchCounter();
    void uat_vwr_083_magnifierEscapeDeactivates();
    void uat_ann_010_rectangleToolCreatesAnnotation();
    void uat_ann_012_lineToolCreatesAnnotation();
    void uat_ann_017_selectToolDragDoesNotCreateAnnotation();
    void uat_ann_060_undoAddRectangle();
    void uat_ann_063_redoAfterUndo();
    void uat_ann_070_hidingToolbarRestoresTextSelection();
    void uat_ann_080_markupToolbarAutoShownOnEditableDoc();
    void uat_ann_081_markupToolbarRespectsExplicitHide();
    void uat_ann_082_textCentricToolsDisabledOnPlainImage();
    void uat_ann_100_textDropOpensInlineEditor();
    void uat_ann_101_emptyTextDropIsRemovedOnFocusOut();
    void uat_ann_110_annotationsSurviveSaveReopen();
    void uat_ann_120_clickSelectsExistingAnnotation();
    void uat_ann_121_deleteRemovesSelectedAnnotation();
    void uat_ann_122_arrowKeyNudgesSelectedAnnotation();
    void uat_ann_123_inspectorTracksSelectedAnnotation();
    void uat_ann_124_dragHandleResizesSelectedAnnotation();
    void uat_ann_125_selectAllSelectsEveryAnnotation();
    void uat_ann_126_selectAllThenDeleteRemovesAllInOneUndo();
    void uat_ann_127_dragGeneratesOneUndoStep();
    void uat_ann_128_clickOnAnnotationWithDrawingToolSelects();
    void uat_ann_130_strokeDialogSurvivesStoreMutation();
    void uat_ann_131_toolSwitchesToSelectAfterShapeCommit();
    void uat_toc_010_outlineDisabledOnPlainPdf();
    void uat_toc_011_outlineExposedForPdfWithBookmarks();
    void uat_toc_012_clickingOutlineEntryNavigatesToPage();
    void uat_hn_010_highlightsModeDisabledForEmptyDoc();
    void uat_hn_011_highlightsModeEnabledAfterAddingNote();
    void uat_hn_012_listFiltersToTextContentTypes();

  private:
    QTemporaryDir m_scratch;
};

void TestUatSearchAndMarkup::init() {
    for (auto *w : QApplication::topLevelWidgets()) {
        if (qobject_cast<MainWindow *>(w))
            w->close();
    }
    QApplication::processEvents();
    // Reset persisted state so per-test "default open" assertions
    // aren't poisoned by prior tests that closed docs with chrome
    // showing. DocumentTypeDefaults' last-closed-wins behaviour is the
    // production contract; the harness opts each case in explicitly by
    // starting from a clean slate here.
    if (auto *app = qobject_cast<Application *>(qApp)) {
        app->recentFiles().clear();
        app->documentTypeDefaults().setForType(DocumentType::Pdf, {});
        app->documentTypeDefaults().setForType(DocumentType::Image, {});
    }
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
    const QString pdfPath =
        writePdfWithKeyword(m_scratch.filePath(QStringLiteral("uat_vwr_061.pdf")), keyword);

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
    QVERIFY2(doc->supportsSearch(), "PDF document should report supportsSearch()");

    QAction *findAction =
        findMenuAction(mw->menuBar(), QStringLiteral("&Edit"), QStringLiteral("&Find…"));
    QVERIFY2(findAction, "Edit > Find… action not found");
    findAction->trigger();
    QApplication::processEvents();

    // Drive the query via the real SearchBar QLineEdit so we exercise
    // the same wire-up a user hits. setText() fires textChanged, which
    // SearchBar forwards as queryChanged → doc->setSearchQuery.
    auto *searchBar = mw->findChild<SearchBar *>();
    QVERIFY(searchBar);
    auto *lineEdit = searchBar->findChild<QLineEdit *>();
    QVERIFY2(lineEdit, "SearchBar QLineEdit not found");
    lineEdit->setText(keyword);
    QApplication::processEvents();

    auto *view = mw->findChild<QPdfView *>();
    QVERIFY2(view, "QPdfView not found in MainWindow children");

    // QPdfSearchModel::setSearchString kicks off an async search. Wait
    // up to five seconds for at least one match to land. If this times
    // out with a PDF that contains the literal keyword as selectable
    // text, search is broken — which is what the user reported.
    QTRY_VERIFY_WITH_TIMEOUT(
        view->searchModel() != nullptr && view->searchModel()->rowCount(QModelIndex()) > 0, 5000);

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
    const QString pdfPath =
        writeOcrLayerPdf(m_scratch.filePath(QStringLiteral("uat_vwr_061b.pdf")), keyword);

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
    QVERIFY2(doc->supportsSearch(), "PDF document should report supportsSearch()");

    QAction *findAction =
        findMenuAction(mw->menuBar(), QStringLiteral("&Edit"), QStringLiteral("&Find…"));
    QVERIFY2(findAction, "Edit > Find… action not found");
    findAction->trigger();
    QApplication::processEvents();

    auto *searchBar = mw->findChild<SearchBar *>();
    QVERIFY(searchBar);
    auto *lineEdit = searchBar->findChild<QLineEdit *>();
    QVERIFY2(lineEdit, "SearchBar QLineEdit not found");
    lineEdit->setText(keyword);
    QApplication::processEvents();

    auto *view = mw->findChild<QPdfView *>();
    QVERIFY2(view, "QPdfView not found in MainWindow children");

    QTRY_VERIFY_WITH_TIMEOUT(
        view->searchModel() != nullptr && view->searchModel()->rowCount(QModelIndex()) > 0, 5000);

    QTRY_VERIFY_WITH_TIMEOUT(view->currentSearchResultIndex() >= 0, 5000);
}

// UAT-VWR-062 — Find Next / Find Previous advance and wrap.
//
// With several matches in the document, verify that:
//   * Return / Next bumps currentSearchResultIndex forward.
//   * Pressing Next past the last match wraps back to 0.
//   * Previous walks back and wraps at the beginning to rowCount-1.
//
// Drives the SearchBar signals directly (findNextRequested /
// findPreviousRequested) — same path as the toolbar buttons and the
// Edit > Find Next / Find Previous menu actions.
void TestUatSearchAndMarkup::uat_vwr_062_findNextPrevWrap() {
    QVERIFY(m_scratch.isValid());
    const QString keyword = QStringLiteral("polaron");
    const int copies = 3;
    const QString pdfPath = writePdfWithKeywordTimes(
        m_scratch.filePath(QStringLiteral("uat_vwr_062.pdf")), keyword, copies);

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);

    QAction *findAction =
        findMenuAction(mw->menuBar(), QStringLiteral("&Edit"), QStringLiteral("&Find…"));
    QVERIFY(findAction);
    findAction->trigger();
    QApplication::processEvents();

    auto *searchBar = mw->findChild<SearchBar *>();
    QVERIFY(searchBar);
    auto *lineEdit = searchBar->findChild<QLineEdit *>();
    QVERIFY(lineEdit);
    lineEdit->setText(keyword);

    auto *view = mw->findChild<QPdfView *>();
    QVERIFY(view);
    // Wait for all `copies` matches to land. QPdfSearchModel streams
    // rowsInserted as the worker finds each hit; if we only wait for
    // rowCount > 0, the wrap assertion below races the worker.
    QTRY_VERIFY_WITH_TIMEOUT(view->searchModel() != nullptr &&
                                 view->searchModel()->rowCount(QModelIndex()) >= copies,
                             5000);
    QTRY_VERIFY_WITH_TIMEOUT(view->currentSearchResultIndex() >= 0, 5000);

    auto *bar = mw->findChild<SearchBar *>();
    QVERIFY(bar);
    const int startIdx = view->currentSearchResultIndex();
    QCOMPARE(startIdx, 0);

    emit bar->findNextRequested();
    QApplication::processEvents();
    QCOMPARE(view->currentSearchResultIndex(), 1);

    emit bar->findNextRequested();
    QApplication::processEvents();
    QCOMPARE(view->currentSearchResultIndex(), 2);

    // Wrap forward.
    emit bar->findNextRequested();
    QApplication::processEvents();
    QCOMPARE(view->currentSearchResultIndex(), 0);

    // Wrap backward.
    emit bar->findPreviousRequested();
    QApplication::processEvents();
    QCOMPARE(view->currentSearchResultIndex(), copies - 1);
}

// UAT-VWR-067 — The search bar shows an "X of Y" match counter.
//
// Users need to know how many hits a query has and where they are in
// them. After a multi-match search lands, the SearchBar's counter
// label becomes visible and reads "<current> of <total>"
// (currentSearchMatchIndex() is 1-based, per PdfDocument).
void TestUatSearchAndMarkup::uat_vwr_067_searchShowsMatchCounter() {
    QVERIFY(m_scratch.isValid());
    const QString keyword = QStringLiteral("polaron");
    const int copies = 3;
    const QString pdfPath = writePdfWithKeywordTimes(
        m_scratch.filePath(QStringLiteral("uat_vwr_067.pdf")), keyword, copies);

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);

    QAction *findAction =
        findMenuAction(mw->menuBar(), QStringLiteral("&Edit"), QStringLiteral("&Find…"));
    QVERIFY(findAction);
    findAction->trigger();
    QApplication::processEvents();

    auto *searchBar = mw->findChild<SearchBar *>();
    QVERIFY(searchBar);
    searchBar->setQuery(keyword);

    auto *view = mw->findChild<QPdfView *>();
    QVERIFY(view);
    // Let the search worker stream in all matches.
    QTRY_VERIFY_WITH_TIMEOUT(view->searchModel() != nullptr &&
                                 view->searchModel()->rowCount(QModelIndex()) >= copies,
                             5000);

    // The counter is the SearchBar's only QLabel; it stays hidden until
    // the document reports matches, then shows "<n> of <total>".
    auto *counter = searchBar->findChild<QLabel *>();
    QVERIFY2(counter, "SearchBar match-counter label not found");
    QTRY_VERIFY_WITH_TIMEOUT(counter->isVisible() &&
                                 counter->text().endsWith(QStringLiteral("of %1").arg(copies)),
                             5000);
}

// UAT-VWR-063 — Escape clears the active query and any match
// highlights. The 2026-05-02 top-bar refactor moved the search
// field into an always-visible main toolbar — Escape no longer
// hides the bar (it lives in the toolbar permanently); it just
// soft-dismisses the query and returns focus to the document.
void TestUatSearchAndMarkup::uat_vwr_063_escapeClosesSearch() {
    QVERIFY(m_scratch.isValid());
    const QString keyword = QStringLiteral("hippogryph");
    const QString pdfPath =
        writePdfWithKeyword(m_scratch.filePath(QStringLiteral("uat_vwr_063.pdf")), keyword);

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);

    QAction *findAction =
        findMenuAction(mw->menuBar(), QStringLiteral("&Edit"), QStringLiteral("&Find…"));
    findAction->trigger();
    QApplication::processEvents();

    auto *bar = mw->findChild<SearchBar *>();
    QVERIFY(bar);
    auto *lineEdit = bar->findChild<QLineEdit *>();
    QVERIFY(lineEdit);
    lineEdit->setText(keyword);

    auto *view = mw->findChild<QPdfView *>();
    QVERIFY(view);
    QTRY_VERIFY_WITH_TIMEOUT(view->currentSearchResultIndex() >= 0, 5000);

    sendKey(bar, Qt::Key_Escape);
    QApplication::processEvents();

    QCOMPARE(view->currentSearchResultIndex(), -1);
}

// UAT-VWR-064 — Each search hit shows up in the AnnotationOverlay's
// search-highlight pass (the yellow "highlighter marker" siblings
// the 2026-04-30 HITL pass asked for, separate from QPdfView's own
// current-match painting). The overlay holds at least one rect per
// match while the query is active; clearing the query drops every
// rect back to zero.
void TestUatSearchAndMarkup::uat_vwr_064_searchHighlightsFillOverlay() {
    QVERIFY(m_scratch.isValid());
    const QString keyword = QStringLiteral("manticore");
    const int copies = 3;
    const QString pdfPath = writePdfWithKeywordTimes(
        m_scratch.filePath(QStringLiteral("uat_vwr_064.pdf")), keyword, copies);

    auto* app = qobject_cast<Application*>(qApp);
    QVERIFY(app);
    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow* mw = currentMainWindow();
    QVERIFY(mw);
    auto* overlay = mw->findChild<AnnotationOverlay*>();
    QVERIFY(overlay);
    QCOMPARE(overlay->searchHighlightCountForTest(), 0);

    QAction* findAction = findMenuAction(mw->menuBar(), QStringLiteral("&Edit"),
                                         QStringLiteral("&Find…"));
    QVERIFY(findAction);
    findAction->trigger();
    QApplication::processEvents();

    auto* bar = mw->findChild<SearchBar*>();
    QVERIFY(bar);
    auto* lineEdit = bar->findChild<QLineEdit*>();
    QVERIFY(lineEdit);
    lineEdit->setText(keyword);

    // QPdfSearchModel populates asynchronously. Wait for the overlay's
    // highlight list to catch up; it's pushed from the same
    // rowsInserted handler the view uses.
    QTRY_VERIFY_WITH_TIMEOUT(
        overlay->searchHighlightCountForTest() >= copies, 5000);

    // Clearing the query (empty string) drops the highlights back to
    // zero so a stale yellow wash doesn't linger on the page.
    lineEdit->clear();
    QApplication::processEvents();
    QTRY_COMPARE_WITH_TIMEOUT(overlay->searchHighlightCountForTest(), 0, 2000);
}

// UAT-VWR-065 — Typing a non-matching query is a harmless no-op.
//
// The search bar stays open, nothing crashes, the model reports zero
// results, and the view does not promote any row to current. findNext
// and findPrevious also no-op against an empty result set — this
// exercises the count == 0 guard in PdfDocument::findNext/Previous.
void TestUatSearchAndMarkup::uat_vwr_065_searchWithNoMatches() {
    QVERIFY(m_scratch.isValid());
    const QString pdfPath = writePdfWithKeyword(
        m_scratch.filePath(QStringLiteral("uat_vwr_065.pdf")), QStringLiteral("unicorn"));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);

    QAction *findAction =
        findMenuAction(mw->menuBar(), QStringLiteral("&Edit"), QStringLiteral("&Find…"));
    findAction->trigger();
    QApplication::processEvents();

    auto *searchBar = mw->findChild<SearchBar *>();
    QVERIFY(searchBar);
    auto *lineEdit = searchBar->findChild<QLineEdit *>();
    QVERIFY(lineEdit);
    lineEdit->setText(QStringLiteral("zzzz_no_such_word_zzzz"));

    auto *view = mw->findChild<QPdfView *>();
    QVERIFY(view);

    // Give the async search a beat to run, then confirm it turned up
    // nothing and the view wasn't nudged into a current-result state.
    // QTRY_VERIFY with a shorter budget — we want to observe the
    // steady state, not wait for something to happen.
    QTest::qWait(500);
    QVERIFY(view->searchModel() != nullptr);
    QCOMPARE(view->searchModel()->rowCount(QModelIndex()), 0);
    QCOMPARE(view->currentSearchResultIndex(), -1);

    // Find Next / Previous against zero matches must stay -1 (not -1
    // accidentally wrapped modulo zero, which would crash or go to an
    // invalid index).
    auto *bar = mw->findChild<SearchBar *>();
    QVERIFY(bar);
    emit bar->findNextRequested();
    QApplication::processEvents();
    QCOMPARE(view->currentSearchResultIndex(), -1);
    emit bar->findPreviousRequested();
    QApplication::processEvents();
    QCOMPARE(view->currentSearchResultIndex(), -1);
}

// UAT-VWR-066 — Cmd-F opens the sidebar in Search Results mode and
// the polling timer pushes pages-with-matches into the filter so
// the user sees just those pages while typing.
void TestUatSearchAndMarkup::uat_vwr_066_searchOpensSidebarWithMatchPages() {
    QVERIFY(m_scratch.isValid());
    const QString keyword = QStringLiteral("zebranaut");
    const QString pdfPath =
        writePdfWithKeyword(m_scratch.filePath(QStringLiteral("uat_vwr_066.pdf")), keyword);

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);

    // Sidebar starts hidden after the 2026-04-30 default flip.
    auto *sidebar = mw->findChild<Sidebar *>();
    QVERIFY(sidebar);
    QVERIFY(!sidebar->isVisible());

    QAction *findAction =
        findMenuAction(mw->menuBar(), QStringLiteral("&Edit"), QStringLiteral("&Find…"));
    QVERIFY(findAction);
    findAction->trigger();
    QApplication::processEvents();

    // Cmd-F opens the sidebar in Search Results mode.
    QCOMPARE(sidebar->mode(), Sidebar::Mode::SearchResults);
    QVERIFY(sidebar->isVisible());

    auto *bar = mw->findChild<SearchBar *>();
    QVERIFY(bar);
    auto *lineEdit = bar->findChild<QLineEdit *>();
    QVERIFY(lineEdit);
    lineEdit->setText(keyword);
    QApplication::processEvents();

    // The polling timer fires every 150 ms; wait for it to push
    // the pagesWithSearchMatches list into the sidebar's filter.
    auto *doc = mw->findChild<DocumentView *>()->currentDocument();
    QVERIFY(doc);
    QTRY_VERIFY_WITH_TIMEOUT(!doc->pagesWithSearchMatches().empty(), 5000);
    QTest::qWait(300); // give the polling timer at least one tick
    QVERIFY(!doc->pagesWithSearchMatches().empty());
}

// UAT-VWR-083 — Esc deactivates the Magnifier transient mode.
//
// The Magnifier is a sticky "mode" with no on-screen exit affordance,
// so Esc must turn it off (MainWindow::keyPressEvent). The same
// un-check happens on app-deactivate (Cmd-Tab); that path needs a real
// platform state change and is covered manually for now.
void TestUatSearchAndMarkup::uat_vwr_083_magnifierEscapeDeactivates() {
    QVERIFY(m_scratch.isValid());
    const QString pdfPath = writePdfWithKeyword(
        m_scratch.filePath(QStringLiteral("uat_vwr_083.pdf")), QStringLiteral("fixture"));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);

    QAction *magnifier =
        findMenuAction(mw->menuBar(), QStringLiteral("&View"), QStringLiteral("&Magnifier"));
    QVERIFY2(magnifier, "View > Magnifier action not found");
    QVERIFY2(magnifier->isEnabled(), "Magnifier must be enabled when a document is open");

    magnifier->trigger(); // checkable → turns the lens on
    QApplication::processEvents();
    QVERIFY2(magnifier->isChecked(), "Triggering Magnifier should activate it");

    // Esc routes through MainWindow::keyPressEvent. Send it directly so
    // delivery doesn't depend on focus/show state under offscreen.
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(mw, &escape);
    QApplication::processEvents();

    QVERIFY2(!magnifier->isChecked(),
             "Esc must deactivate the Magnifier so the user isn't stuck in the lens mode");
}

// UAT-ANN-010 — Rectangle tool creates an annotation on click-drag.
void TestUatSearchAndMarkup::uat_ann_010_rectangleToolCreatesAnnotation() {
    QVERIFY(m_scratch.isValid());
    const QString pdfPath = writePdfWithKeyword(
        m_scratch.filePath(QStringLiteral("uat_ann_010.pdf")), QStringLiteral("fixture"));

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
    AnnotationStore *store = doc->annotations();
    QVERIFY(store);
    const int before = store->count();

    auto *markup = mw->findChild<MarkupToolbar *>();
    QVERIFY(markup);
    QAction *rectAction = findToolAction(markup, QStringLiteral("Rectangle"));
    QVERIFY2(rectAction, "Markup toolbar Rectangle action not found");
    rectAction->setChecked(true); // exclusive group → emits toggled(true)
    QApplication::processEvents();

    auto *overlay = mw->findChild<AnnotationOverlay *>();
    QVERIFY2(overlay, "AnnotationOverlay not found as child of MainWindow");
    QCOMPARE(overlay->activeTool(), AnnotationTool::Rectangle);

    dragOnOverlay(overlay, QPoint(200, 250), QPoint(320, 340));

    QCOMPARE(store->count(), before + 1);
    QVERIFY2(store->canUndo(), "Store should report canUndo() after adding a rectangle");
    QCOMPARE(store->annotations().back().type, AnnotationType::Rectangle);
}

// UAT-ANN-012 — Line tool creates a Line annotation with two endpoints.
void TestUatSearchAndMarkup::uat_ann_012_lineToolCreatesAnnotation() {
    QVERIFY(m_scratch.isValid());
    const QString pdfPath = writePdfWithKeyword(
        m_scratch.filePath(QStringLiteral("uat_ann_012.pdf")), QStringLiteral("fixture"));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    mw->resize(1100, 750);
    QApplication::processEvents();

    auto *dv = mw->findChild<DocumentView *>();
    IDocument *doc = dv->currentDocument();
    QVERIFY(doc);
    AnnotationStore *store = doc->annotations();
    const int before = store->count();

    auto *markup = mw->findChild<MarkupToolbar *>();
    QVERIFY(markup);
    QAction *lineAction = findToolAction(markup, QStringLiteral("Line"));
    QVERIFY(lineAction);
    lineAction->setChecked(true);
    QApplication::processEvents();

    auto *overlay = mw->findChild<AnnotationOverlay *>();
    QVERIFY(overlay);
    QCOMPARE(overlay->activeTool(), AnnotationTool::Line);

    dragOnOverlay(overlay, QPoint(180, 240), QPoint(340, 360));

    QCOMPARE(store->count(), before + 1);
    const Annotation &added = store->annotations().back();
    QCOMPARE(added.type, AnnotationType::Line);
    QCOMPARE(added.points.size(), size_t{2});
    QVERIFY2(store->canUndo(), "Store should report canUndo() after adding a line");
}

// UAT-ANN-017 — The Select tool must not create a shape on click-drag.
//
// Regression guard for the HITL fix where a drag drew a stray rectangle
// even with Select active. With Select active and nothing under the
// drag, the store must be untouched and there must be nothing to undo.
void TestUatSearchAndMarkup::uat_ann_017_selectToolDragDoesNotCreateAnnotation() {
    QVERIFY(m_scratch.isValid());
    const QString pdfPath = writePdfWithKeyword(
        m_scratch.filePath(QStringLiteral("uat_ann_017.pdf")), QStringLiteral("fixture"));

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
    AnnotationStore *store = doc->annotations();
    QVERIFY(store);

    auto *markup = mw->findChild<MarkupToolbar *>();
    QVERIFY(markup);
    QAction *selectAction = findToolAction(markup, QStringLiteral("Select"));
    QVERIFY2(selectAction, "Markup toolbar Select action not found");
    selectAction->setChecked(true); // exclusive group → emits toggled(true)
    QApplication::processEvents();

    auto *overlay = mw->findChild<AnnotationOverlay *>();
    QVERIFY2(overlay, "AnnotationOverlay not found as child of MainWindow");
    QCOMPARE(overlay->activeTool(), AnnotationTool::Select);

    // The same drag that creates a Rectangle under the Rectangle tool
    // (UAT-ANN-010) must create nothing under Select.
    const int before = store->count();
    dragOnOverlay(overlay, QPoint(200, 250), QPoint(320, 340));

    QCOMPARE(store->count(), before);
    QVERIFY2(!store->canUndo(),
             "A Select-tool drag must not push an undoable annotation onto the store");
}

// UAT-ANN-060 — Undo removes the most recent rectangle add.
//
// Drives the same flow as UAT-ANN-010 and then invokes Edit > Undo,
// expecting the store to return to its prior count. This is the user
// escape hatch when a markup tool drag goes wrong.
void TestUatSearchAndMarkup::uat_ann_060_undoAddRectangle() {
    QVERIFY(m_scratch.isValid());
    const QString pdfPath = writePdfWithKeyword(
        m_scratch.filePath(QStringLiteral("uat_ann_060.pdf")), QStringLiteral("fixture"));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    mw->resize(1100, 750);
    QApplication::processEvents();

    auto *dv = mw->findChild<DocumentView *>();
    IDocument *doc = dv->currentDocument();
    QVERIFY(doc);
    AnnotationStore *store = doc->annotations();
    const int baseline = store->count();

    auto *markup = mw->findChild<MarkupToolbar *>();
    QAction *rectAction = findToolAction(markup, QStringLiteral("Rectangle"));
    QVERIFY(rectAction);
    rectAction->setChecked(true);
    QApplication::processEvents();

    auto *overlay = mw->findChild<AnnotationOverlay *>();
    QVERIFY(overlay);

    dragOnOverlay(overlay, QPoint(200, 250), QPoint(320, 340));
    QCOMPARE(store->count(), baseline + 1);

    QAction *undoAction =
        findMenuAction(mw->menuBar(), QStringLiteral("&Edit"), QStringLiteral("&Undo"));
    QVERIFY2(undoAction, "Edit > Undo action not found");
    QVERIFY2(undoAction->isEnabled(), "Undo action should be enabled after adding an annotation");
    undoAction->trigger();
    QApplication::processEvents();

    QCOMPARE(store->count(), baseline);
}

// UAT-ANN-063 — Redo after undo reapplies the change.
//
// Extends UAT-ANN-060 one step further: after the rectangle has been
// undone, Edit > Redo must bring it back. This exercises the same
// MainWindow::updateUndoRedoActions wiring that UAT-ANN-060 proved is
// hooked up — Redo's enabled state flips to true as a side effect of
// the AnnotationStore::changed signal from undo().
void TestUatSearchAndMarkup::uat_ann_063_redoAfterUndo() {
    QVERIFY(m_scratch.isValid());
    const QString pdfPath = writePdfWithKeyword(
        m_scratch.filePath(QStringLiteral("uat_ann_063.pdf")), QStringLiteral("fixture"));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    mw->resize(1100, 750);
    QApplication::processEvents();

    auto *dv = mw->findChild<DocumentView *>();
    IDocument *doc = dv->currentDocument();
    QVERIFY(doc);
    AnnotationStore *store = doc->annotations();
    const int baseline = store->count();

    auto *markup = mw->findChild<MarkupToolbar *>();
    QAction *rectAction = findToolAction(markup, QStringLiteral("Rectangle"));
    QVERIFY(rectAction);
    rectAction->setChecked(true);
    QApplication::processEvents();

    auto *overlay = mw->findChild<AnnotationOverlay *>();
    QVERIFY(overlay);

    dragOnOverlay(overlay, QPoint(200, 250), QPoint(320, 340));
    QCOMPARE(store->count(), baseline + 1);

    QAction *undoAction =
        findMenuAction(mw->menuBar(), QStringLiteral("&Edit"), QStringLiteral("&Undo"));
    QAction *redoAction =
        findMenuAction(mw->menuBar(), QStringLiteral("&Edit"), QStringLiteral("&Redo"));
    QVERIFY(undoAction);
    QVERIFY(redoAction);

    undoAction->trigger();
    QApplication::processEvents();
    QCOMPARE(store->count(), baseline);

    QVERIFY2(redoAction->isEnabled(), "Redo action should be enabled after an undo");
    redoAction->trigger();
    QApplication::processEvents();
    QCOMPARE(store->count(), baseline + 1);
    QCOMPARE(store->annotations().back().type, AnnotationType::Rectangle);
}

// UAT-ANN-070 — Hiding the markup toolbar restores text selection.
//
// Regression test for the 2026-04-24 HITL walkthrough: with the
// Rectangle tool active, hiding the markup toolbar left the overlay
// in Rectangle mode so click-drag drew shapes instead of selecting
// text. The fix resets the active tool to Select on hide so the
// overlay's Select branch (which forwards drags to
// QPdfDocument::getSelection) runs instead. We assert both ends:
// (1) after hide, the overlay reports Select as the active tool, and
// (2) a drag after the hide produces no new shape annotation.
void TestUatSearchAndMarkup::uat_ann_070_hidingToolbarRestoresTextSelection() {
    QVERIFY(m_scratch.isValid());
    const QString pdfPath = writePdfWithKeyword(
        m_scratch.filePath(QStringLiteral("uat_ann_070.pdf")), QStringLiteral("fixture"));

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
    AnnotationStore *store = doc->annotations();
    QVERIFY(store);

    auto *markup = mw->findChild<MarkupToolbar *>();
    QVERIFY(markup);

    // 1. Show the toolbar and pick Rectangle — simulates a user who
    //    has just drawn a shape and is now putting the toolbar away.
    markup->show();
    QApplication::processEvents();
    QAction *rectAction = findToolAction(markup, QStringLiteral("Rectangle"));
    QVERIFY(rectAction);
    rectAction->setChecked(true);
    QApplication::processEvents();
    QCOMPARE(markup->activeTool(), AnnotationTool::Rectangle);

    auto *overlay = mw->findChild<AnnotationOverlay *>();
    QVERIFY(overlay);
    QCOMPARE(overlay->activeTool(), AnnotationTool::Rectangle);

    // 2. Hide the toolbar. Bug: overlay stayed in Rectangle.
    //    Fix: MainWindow reacts to visibilityChanged(false) and
    //    bounces the toolbar to Select, which propagates through
    //    activeToolChanged → doc->setAnnotationTool(Select).
    const int shapesBefore = store->count();
    markup->hide();
    QApplication::processEvents();
    QCOMPARE(markup->activeTool(), AnnotationTool::Select);
    QCOMPARE(overlay->activeTool(), AnnotationTool::Select);

    // 3. A click-drag with Select active must not create a shape
    //    annotation. (The Select branch routes to the text-selection
    //    callback; in this test fixture it returns empty, but the
    //    store count is still the reliable signal.)
    dragOnOverlay(overlay, QPoint(200, 250), QPoint(320, 340));
    QCOMPARE(store->count(), shapesBefore);
}

// UAT-ANN-080 — Opening an annotatable document does NOT auto-show
// the markup toolbar. The toolbar stays hidden until the user opts
// in via View → Toggle Markup Toolbar (Ctrl+Shift+A) or the
// toolbar's own toggle action. The pre-2026-05 auto-show heuristic
// was loud chrome for a document-first workflow.
void TestUatSearchAndMarkup::uat_ann_080_markupToolbarAutoShownOnEditableDoc() {
    QVERIFY(m_scratch.isValid());
    const QString pdfPath = writePdfWithKeyword(
        m_scratch.filePath(QStringLiteral("uat_ann_080.pdf")), QStringLiteral("fixture"));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    auto *markup = mw->findChild<MarkupToolbar *>();
    QVERIFY(markup);
    QVERIFY2(!markup->isVisible(),
             "Markup toolbar must stay hidden by default on a fresh open");

    // The View → Toggle Markup Toolbar action surfaces it on demand.
    QAction *toggle = findMenuAction(mw->menuBar(), QStringLiteral("&View"),
                                     QStringLiteral("Toggle &Markup Toolbar"));
    QVERIFY2(toggle, "View → Toggle Markup Toolbar action not found");
    toggle->trigger();
    QApplication::processEvents();
    QVERIFY2(markup->isVisible(),
             "Toggle action must reveal the markup toolbar on demand");
}

// UAT-ANN-081 — The markup toolbar's hidden default sticks across
// tab/focus switches; refreshing the current document does not
// resurrect a hidden toolbar.
void TestUatSearchAndMarkup::uat_ann_081_markupToolbarRespectsExplicitHide() {
    QVERIFY(m_scratch.isValid());
    const QString pdfPath = writePdfWithKeyword(
        m_scratch.filePath(QStringLiteral("uat_ann_081.pdf")), QStringLiteral("fixture"));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    auto *markup = mw->findChild<MarkupToolbar *>();
    QVERIFY(markup);
    QVERIFY2(!markup->isVisible(), "Toolbar hidden by default on open");

    // Re-trigger the current-document path — same effect as a tab
    // switch in the legacy NewTab mode, or any focus-driven refresh.
    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    IDocument *doc = dv->currentDocument();
    QVERIFY(doc);
    QMetaObject::invokeMethod(dv, "currentDocumentChanged", Qt::DirectConnection,
                              Q_ARG(trailer::IDocument *, doc));
    QApplication::processEvents();

    QVERIFY2(!markup->isVisible(), "Markup toolbar must not auto-show on focus refresh");
}

// UAT-ANN-082 — Underline / Highlight / StrikeOut are text-aware
// tools; they have nothing to bite on for a plain image with no OCR
// run. They should be disabled, but Redact (which is pixel-region
// based) should remain enabled.
void TestUatSearchAndMarkup::uat_ann_082_textCentricToolsDisabledOnPlainImage() {
    QVERIFY(m_scratch.isValid());
    // A simple 200×100 PNG with no text — opens via ImageAdapter.
    const QString imgPath = m_scratch.filePath(QStringLiteral("uat_ann_082.png"));
    {
        QImage img(200, 100, QImage::Format_RGB32);
        img.fill(Qt::white);
        QVERIFY(img.save(imgPath, "PNG"));
    }

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({imgPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    auto *markup = mw->findChild<MarkupToolbar *>();
    QVERIFY(markup);

    auto find = [markup](const QString &label) -> QAction * {
        return findToolAction(markup, label);
    };
    QAction *underline = find(QStringLiteral("Underline"));
    QAction *highlight = find(QStringLiteral("Highlight"));
    QAction *strike = find(QStringLiteral("Strikeout"));
    QAction *redact = find(QStringLiteral("Redact"));
    QAction *rect = find(QStringLiteral("Rectangle"));
    QVERIFY(underline);
    QVERIFY(highlight);
    QVERIFY(strike);
    QVERIFY(redact);
    QVERIFY(rect);

    QVERIFY2(!underline->isEnabled(), "Underline must be disabled on a plain image");
    QVERIFY2(!highlight->isEnabled(), "Highlight must be disabled on a plain image");
    QVERIFY2(!strike->isEnabled(), "Strikeout must be disabled on a plain image");
    QVERIFY2(redact->isEnabled(), "Redact must remain available on a plain image");
    QVERIFY2(rect->isEnabled(), "Rectangle (and other shape tools) remain available on images");
}

// UAT-ANN-100 — Dropping a Text annotation no longer pops a modal
// QInputDialog. Instead, an inline child widget (a QPlainTextEdit
// inside a frame) is focused at the placement rect so the user types
// directly into the document.
void TestUatSearchAndMarkup::uat_ann_100_textDropOpensInlineEditor() {
    QVERIFY(m_scratch.isValid());
    const QString pdfPath = writePdfWithKeyword(
        m_scratch.filePath(QStringLiteral("uat_ann_100.pdf")), QStringLiteral("fixture"));

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
    AnnotationStore *store = dv->currentDocument()->annotations();
    QVERIFY(store);
    const int before = store->count();

    auto *markup = mw->findChild<MarkupToolbar *>();
    QVERIFY(markup);
    QAction *textAction = findToolAction(markup, QStringLiteral("Text"));
    QVERIFY(textAction);
    textAction->setChecked(true);
    QApplication::processEvents();

    auto *overlay = mw->findChild<AnnotationOverlay *>();
    QVERIFY(overlay);

    // Snapshot the QMessageBox/QInputDialog count so we can assert
    // none appeared.
    auto modalCount = []() {
        int n = 0;
        for (auto *w : QApplication::topLevelWidgets()) {
            if (w->inherits("QDialog") || w->inherits("QMessageBox"))
                ++n;
        }
        return n;
    };
    const int modalsBefore = modalCount();

    dragOnOverlay(overlay, QPoint(200, 250), QPoint(320, 340));

    // The drop should have:
    //   1. added a placeholder annotation (count + 1)
    //   2. NOT spawned a modal dialog (no QInputDialog)
    //   3. created an inline editor child of the overlay
    QCOMPARE(store->count(), before + 1);
    QCOMPARE(modalCount(), modalsBefore);
    auto *editor = overlay->findChild<QPlainTextEdit *>();
    QVERIFY2(editor, "Expected an inline QPlainTextEdit anchored at the drop");
}

// UAT-ANN-101 — A Text drop with no typing must not leave a stamp on
// the page. Closing the editor (focus out) without typing removes the
// placeholder annotation.
void TestUatSearchAndMarkup::uat_ann_101_emptyTextDropIsRemovedOnFocusOut() {
    QVERIFY(m_scratch.isValid());
    const QString pdfPath = writePdfWithKeyword(
        m_scratch.filePath(QStringLiteral("uat_ann_101.pdf")), QStringLiteral("fixture"));

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
    AnnotationStore *store = dv->currentDocument()->annotations();
    QVERIFY(store);
    const int before = store->count();

    auto *markup = mw->findChild<MarkupToolbar *>();
    QVERIFY(markup);
    QAction *textAction = findToolAction(markup, QStringLiteral("Text"));
    QVERIFY(textAction);
    textAction->setChecked(true);
    QApplication::processEvents();

    auto *overlay = mw->findChild<AnnotationOverlay *>();
    QVERIFY(overlay);
    dragOnOverlay(overlay, QPoint(200, 250), QPoint(320, 340));

    QCOMPARE(store->count(), before + 1);
    auto *editor = overlay->findChild<QPlainTextEdit *>();
    QVERIFY(editor);

    // Send an Escape — the eventFilter discards a fresh empty
    // annotation so the user is not left with an invisible Text
    // stamp on the page.
    sendKey(editor, Qt::Key_Escape);
    QApplication::processEvents();

    QVERIFY2(store->count() == before, "An empty Text drop cancelled with Escape must not leave "
                                       "an annotation behind");
}

// UAT-ANN-110 — End-to-end check that an annotation drawn in the UI
// survives a save+reopen cycle. PdfEditor's writeAnnotations /
// readAnnotations round-trip is unit-tested in test_pdf_editor.cpp;
// this UAT closes the loop at the user-visible level: open → draw →
// save → close → reopen → annotation is back.
void TestUatSearchAndMarkup::uat_ann_110_annotationsSurviveSaveReopen() {
    QVERIFY(m_scratch.isValid());
    const QString srcPath = writePdfWithKeyword(
        m_scratch.filePath(QStringLiteral("uat_ann_110_src.pdf")), QStringLiteral("fixture"));
    const QString dstPath = m_scratch.filePath(QStringLiteral("uat_ann_110_marked.pdf"));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({srcPath});
    QApplication::processEvents();

    {
        MainWindow *mw = currentMainWindow();
        QVERIFY(mw);
        mw->resize(1100, 750);
        QApplication::processEvents();

        auto *dv = mw->findChild<DocumentView *>();
        QVERIFY(dv);
        IDocument *doc = dv->currentDocument();
        QVERIFY(doc);
        AnnotationStore *store = doc->annotations();
        QVERIFY(store);

        auto *markup = mw->findChild<MarkupToolbar *>();
        QVERIFY(markup);
        QAction *rectAction = findToolAction(markup, QStringLiteral("Rectangle"));
        QVERIFY(rectAction);
        rectAction->setChecked(true);
        QApplication::processEvents();

        auto *overlay = mw->findChild<AnnotationOverlay *>();
        QVERIFY(overlay);
        dragOnOverlay(overlay, QPoint(150, 200), QPoint(280, 300));
        QCOMPARE(store->count(), 1);

        // Save to a side path so the source isn't touched.
        QVERIFY2(doc->save(dstPath), "Save with annotations must succeed");
        mw->close();
        QApplication::processEvents();
    }

    // Reopen the saved file from scratch and confirm the annotation
    // survived the write+reload cycle.
    app->openFiles({dstPath});
    QApplication::processEvents();

    MainWindow *mw2 = currentMainWindow();
    QVERIFY(mw2);
    auto *dv2 = mw2->findChild<DocumentView *>();
    QVERIFY(dv2);
    IDocument *doc2 = dv2->currentDocument();
    QVERIFY(doc2);
    AnnotationStore *store2 = doc2->annotations();
    QVERIFY(store2);

    QVERIFY2(store2->count() >= 1, "Saved-and-reopened PDF must restore the rectangle "
                                   "annotation drawn in the previous session");
    bool hasRect = false;
    for (const auto &a : store2->annotations()) {
        if (a.type == AnnotationType::Rectangle)
            hasRect = true;
    }
    QVERIFY2(hasRect, "Restored annotation should be of type Rectangle");
}

namespace {

// Helper: create a window with a PDF, draw a rectangle annotation
// at viewport coordinates, return (mw, overlay, store, drawnId).
struct AnnEditingFixture {
    MainWindow *mw = nullptr;
    AnnotationOverlay *overlay = nullptr;
    AnnotationStore *store = nullptr;
    int drawnId = 0;
};

AnnEditingFixture buildAnnEditingFixture(QTemporaryDir &scratch, const QString &tag) {
    AnnEditingFixture f;
    const QString pdfPath = writePdfWithKeyword(
        scratch.filePath(QStringLiteral("uat_ann_%1.pdf").arg(tag)), QStringLiteral("fixture"));

    auto *app = qobject_cast<Application *>(qApp);
    if (!app)
        return f;
    app->openFiles({pdfPath});
    QApplication::processEvents();

    f.mw = nullptr;
    for (auto *w : QApplication::topLevelWidgets()) {
        if (auto *m = qobject_cast<MainWindow *>(w))
            f.mw = m;
    }
    if (!f.mw)
        return f;
    f.mw->resize(1100, 750);
    QApplication::processEvents();

    auto *dv = f.mw->findChild<DocumentView *>();
    if (!dv)
        return f;
    f.store = dv->currentDocument()->annotations();
    if (!f.store)
        return f;

    auto *markup = f.mw->findChild<MarkupToolbar *>();
    if (!markup)
        return f;
    QAction *rectAction = findToolAction(markup, QStringLiteral("Rectangle"));
    if (!rectAction)
        return f;
    rectAction->setChecked(true);
    QApplication::processEvents();

    f.overlay = f.mw->findChild<AnnotationOverlay *>();
    if (!f.overlay)
        return f;
    dragOnOverlay(f.overlay, QPoint(200, 250), QPoint(320, 340));
    if (f.store->count() == 0)
        return f;
    f.drawnId = f.store->annotations().back().id;

    // Switch back to Select tool so subsequent clicks select rather
    // than draw new rectangles.
    QAction *selectAction = findToolAction(markup, QStringLiteral("Select"));
    if (selectAction) {
        selectAction->setChecked(true);
        QApplication::processEvents();
    }
    return f;
}

} // namespace

// UAT-ANN-120 — Click on an existing annotation while the Select
// tool is active selects it. The overlay reports the selected id;
// the dashed selection ring around it is a visual affordance the
// user sees but the test asserts on the data layer instead.
void TestUatSearchAndMarkup::uat_ann_120_clickSelectsExistingAnnotation() {
    AnnEditingFixture f = buildAnnEditingFixture(m_scratch, QStringLiteral("120"));
    QVERIFY(f.overlay);
    QVERIFY(f.drawnId != 0);
    QCOMPARE(f.overlay->selectedAnnotationId(), 0);

    // Click somewhere inside the rectangle's view-space bounds. The
    // drag we used to draw it ran from (200,250) to (320,340) in
    // view coords, so (260,295) is well inside.
    sendMouse(f.overlay, QEvent::MouseButtonPress, QPoint(260, 295), Qt::LeftButton);
    sendMouse(f.overlay, QEvent::MouseButtonRelease, QPoint(260, 295), Qt::LeftButton);
    QApplication::processEvents();

    QCOMPARE(f.overlay->selectedAnnotationId(), f.drawnId);
}

// UAT-ANN-121 — Delete on a selected annotation removes it. The
// store's count goes back to where it was before the draw.
void TestUatSearchAndMarkup::uat_ann_121_deleteRemovesSelectedAnnotation() {
    AnnEditingFixture f = buildAnnEditingFixture(m_scratch, QStringLiteral("121"));
    QVERIFY(f.overlay);
    QVERIFY(f.drawnId != 0);
    QCOMPARE(f.store->count(), 1);

    // Click to select.
    sendMouse(f.overlay, QEvent::MouseButtonPress, QPoint(260, 295), Qt::LeftButton);
    sendMouse(f.overlay, QEvent::MouseButtonRelease, QPoint(260, 295), Qt::LeftButton);
    QApplication::processEvents();
    QCOMPARE(f.overlay->selectedAnnotationId(), f.drawnId);

    // Delete key on the focused overlay removes it.
    sendKey(f.overlay, Qt::Key_Delete);
    QApplication::processEvents();

    QCOMPARE(f.store->count(), 0);
    QCOMPARE(f.overlay->selectedAnnotationId(), 0);
}

// UAT-ANN-122 — Arrow keys nudge the selected annotation by 1pt
// (Shift = 10pt) without rebuilding it. The bounds shift; the id
// stays the same.
void TestUatSearchAndMarkup::uat_ann_122_arrowKeyNudgesSelectedAnnotation() {
    AnnEditingFixture f = buildAnnEditingFixture(m_scratch, QStringLiteral("122"));
    QVERIFY(f.overlay);
    QVERIFY(f.drawnId != 0);

    sendMouse(f.overlay, QEvent::MouseButtonPress, QPoint(260, 295), Qt::LeftButton);
    sendMouse(f.overlay, QEvent::MouseButtonRelease, QPoint(260, 295), Qt::LeftButton);
    QApplication::processEvents();
    QCOMPARE(f.overlay->selectedAnnotationId(), f.drawnId);

    const QRectF before = f.store->find(f.drawnId)->bounds;
    sendKey(f.overlay, Qt::Key_Right);
    QApplication::processEvents();
    const QRectF after = f.store->find(f.drawnId)->bounds;

    QVERIFY2(after.x() > before.x(), "Right arrow should shift the bounds rightward in doc space");
    // The id is unchanged: nudging is an in-place update, not a
    // delete+add.
    QCOMPARE(f.overlay->selectedAnnotationId(), f.drawnId);
}

// UAT-ANN-123 — Selecting an annotation tracks the selection in the
// Inspector but does NOT pop the pane open (2026-04-30 reframe: the
// auto-show was noisy for select-and-delete / select-and-nudge).
// Visibility stays under the user's control via ⌘I.
void TestUatSearchAndMarkup::uat_ann_123_inspectorTracksSelectedAnnotation() {
    AnnEditingFixture f = buildAnnEditingFixture(m_scratch, QStringLiteral("123"));
    QVERIFY(f.overlay);
    QVERIFY(f.drawnId != 0);

    QDockWidget *inspectorDock = nullptr;
    for (auto *d : f.mw->findChildren<QDockWidget *>()) {
        if (QString::fromLatin1(d->metaObject()->className())
                .endsWith(QStringLiteral("Inspector"))) {
            inspectorDock = d;
            break;
        }
    }
    QVERIFY(inspectorDock);
    QVERIFY(!inspectorDock->isVisible());

    sendMouse(f.overlay, QEvent::MouseButtonPress, QPoint(260, 295), Qt::LeftButton);
    sendMouse(f.overlay, QEvent::MouseButtonRelease, QPoint(260, 295), Qt::LeftButton);
    QApplication::processEvents();

    QVERIFY2(f.overlay->selectedAnnotationId() == f.drawnId, "Click should select the rectangle");
    QVERIFY2(!inspectorDock->isVisible(), "Inspector visibility is user-controlled — selecting "
                                          "an annotation must not pop the pane open");
}

// UAT-ANN-124 — Dragging the bottom-right resize handle of a
// selected annotation expands its bounds. The id stays the same;
// only the rectangle grows.
void TestUatSearchAndMarkup::uat_ann_124_dragHandleResizesSelectedAnnotation() {
    AnnEditingFixture f = buildAnnEditingFixture(m_scratch, QStringLiteral("124"));
    QVERIFY(f.overlay);
    QVERIFY(f.drawnId != 0);

    // Select the rectangle first.
    sendMouse(f.overlay, QEvent::MouseButtonPress, QPoint(260, 295), Qt::LeftButton);
    sendMouse(f.overlay, QEvent::MouseButtonRelease, QPoint(260, 295), Qt::LeftButton);
    QApplication::processEvents();
    QCOMPARE(f.overlay->selectedAnnotationId(), f.drawnId);

    // Original bounds in doc-space.
    const QRectF before = f.store->find(f.drawnId)->bounds;
    // Compute the bottom-right handle position from the overlay's
    // current view-space mapping rather than assuming the doc→view
    // transform is identity. The overlay exposes the selected
    // annotation's view rect for this purpose.
    const QRectF viewRect = f.overlay->selectedViewRectForTest();
    QVERIFY2(!viewRect.isEmpty(), "The selected annotation should report a non-empty view rect");
    const QPoint brStart = viewRect.bottomRight().toPoint();
    const QPoint brEnd = brStart + QPoint(40, 40);
    sendMouse(f.overlay, QEvent::MouseButtonPress, brStart, Qt::LeftButton);
    QApplication::processEvents();
    QVERIFY2(f.overlay->isResizingForTest(),
             "Press at the bottom-right handle should engage resize mode");
    sendMouse(f.overlay, QEvent::MouseMove, brEnd, Qt::LeftButton);
    sendMouse(f.overlay, QEvent::MouseButtonRelease, brEnd, Qt::LeftButton);
    QApplication::processEvents();

    const QRectF after = f.store->find(f.drawnId)->bounds;
    QVERIFY2(after.width() > before.width(), "Dragging the bottom-right handle outward should "
                                             "increase the bounds width");
    QVERIFY2(after.height() > before.height(), "Dragging the bottom-right handle outward should "
                                               "increase the bounds height");
    // The id is preserved — resize is an in-place update.
    QCOMPARE(f.overlay->selectedAnnotationId(), f.drawnId);
}

// UAT-TOC-010 — plain PDFs (no /Outlines tree) leave the sidebar
// picker's "Table of Contents" entry disabled. The picker still
// shows it (so the user can see the feature exists), but clicking
// it would do nothing useful.
void TestUatSearchAndMarkup::uat_toc_010_outlineDisabledOnPlainPdf() {
    QVERIFY(m_scratch.isValid());
    const QString pdfPath = writePdfWithKeyword(
        m_scratch.filePath(QStringLiteral("uat_toc_010.pdf")),
        QStringLiteral("kraken"));

    auto* app = qobject_cast<Application*>(qApp);
    QVERIFY(app);
    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow* mw = currentMainWindow();
    QVERIFY(mw);

    // The picker entry lives on a QMenu hosted by the sidebar
    // QToolButton on the main toolbar. The menu is attached to the
    // button (not as an action's QAction::menu()), so walk the
    // QToolButton children directly.
    QAction* tocAction = nullptr;
    const auto* mainToolbar =
        mw->findChild<QToolBar*>(QStringLiteral("MainToolbar"));
    QVERIFY(mainToolbar);
    for (auto* btn : mainToolbar->findChildren<QToolButton*>()) {
        QMenu* m = btn->menu();
        if (!m) continue;
        for (QAction* item : m->actions()) {
            if (item->text() == QStringLiteral("Table of Contents")) {
                tocAction = item;
                break;
            }
        }
        if (tocAction) break;
    }
    QVERIFY2(tocAction,
             "Sidebar picker should always offer a TOC menu entry");
    QVERIFY2(!tocAction->isEnabled(),
             "TOC entry must be disabled for a plain PDF without "
             "an /Outlines tree.");
}

// UAT-TOC-011 — a PDF with an /Outlines tree exposes it via the
// document's outlineModel(), and the sidebar's picker entry becomes
// enabled. We don't rely on any rendered UI here — we drive the
// model directly via the document.
void TestUatSearchAndMarkup::uat_toc_011_outlineExposedForPdfWithBookmarks() {
    QVERIFY(m_scratch.isValid());
    const QStringList titles = {
        QStringLiteral("Introduction"),
        QStringLiteral("Methods"),
        QStringLiteral("Results"),
    };
    const QString pdfPath = writePdfWithOutline(
        m_scratch.filePath(QStringLiteral("uat_toc_011.pdf")), titles);

    auto* app = qobject_cast<Application*>(qApp);
    QVERIFY(app);
    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow* mw = currentMainWindow();
    QVERIFY(mw);
    auto* docView = mw->findChild<DocumentView*>();
    QVERIFY(docView);
    IDocument* doc = docView->currentDocument();
    QVERIFY(doc);

    QVERIFY2(doc->hasOutline(),
             "PdfDocument with /Outlines should report hasOutline() true");
    QAbstractItemModel* model = doc->outlineModel();
    QVERIFY(model);
    QCOMPARE(model->rowCount({}), titles.size());
    // Title shows up as DisplayRole via the document's proxy model
    // (the Sidebar's tree view depends on this).
    for (int i = 0; i < titles.size(); ++i) {
        const QString got = model->index(i, 0).data(Qt::DisplayRole).toString();
        QCOMPARE(got, titles[i]);
    }
}

// UAT-TOC-012 — clicking an outline entry navigates the document
// to the entry's destination page. Drives goToOutlineEntry directly
// (the same code path Sidebar's QTreeView::clicked is wired to) so
// we don't have to render the tree view in the offscreen test.
void TestUatSearchAndMarkup::uat_toc_012_clickingOutlineEntryNavigatesToPage() {
    QVERIFY(m_scratch.isValid());
    const QStringList titles = {
        QStringLiteral("Cover"),
        QStringLiteral("Chapter 1"),
        QStringLiteral("Chapter 2"),
    };
    const QString pdfPath = writePdfWithOutline(
        m_scratch.filePath(QStringLiteral("uat_toc_012.pdf")), titles);

    auto* app = qobject_cast<Application*>(qApp);
    QVERIFY(app);
    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow* mw = currentMainWindow();
    QVERIFY(mw);
    auto* docView = mw->findChild<DocumentView*>();
    QVERIFY(docView);
    IDocument* doc = docView->currentDocument();
    QVERIFY(doc);

    QAbstractItemModel* model = doc->outlineModel();
    QVERIFY(model);
    QCOMPARE(model->rowCount({}), 3);

    // The third bookmark points to page index 2 (zero-based).
    const QModelIndex idx2 = model->index(2, 0);
    QVERIFY(idx2.isValid());
    doc->goToOutlineEntry(idx2);
    QApplication::processEvents();
    QTRY_COMPARE_WITH_TIMEOUT(doc->currentPage(), 2, 2000);
}

namespace {

// Helper: find the "Highlights & Notes" picker entry by walking the
// main toolbar's QToolButtons. Same shape as the TOC lookup.
QAction* findHighlightsNotesAction(MainWindow* mw) {
    const auto* mainToolbar =
        mw->findChild<QToolBar*>(QStringLiteral("MainToolbar"));
    if (!mainToolbar) return nullptr;
    for (auto* btn : mainToolbar->findChildren<QToolButton*>()) {
        QMenu* m = btn->menu();
        if (!m) continue;
        for (QAction* item : m->actions()) {
            if (item->text() == QStringLiteral("Highlights && Notes")) {
                return item;
            }
        }
    }
    return nullptr;
}

}  // namespace

// UAT-HN-010 — opening a fresh document with no annotations leaves
// the Highlights & Notes picker entry disabled.
void TestUatSearchAndMarkup::uat_hn_010_highlightsModeDisabledForEmptyDoc() {
    QVERIFY(m_scratch.isValid());
    const QString pdfPath = writePdfWithKeyword(
        m_scratch.filePath(QStringLiteral("uat_hn_010.pdf")),
        QStringLiteral("griffin"));

    auto* app = qobject_cast<Application*>(qApp);
    QVERIFY(app);
    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow* mw = currentMainWindow();
    QVERIFY(mw);
    QAction* hnAction = findHighlightsNotesAction(mw);
    QVERIFY(hnAction);
    QVERIFY2(!hnAction->isEnabled(),
             "H&N entry must start disabled — no annotations yet.");
}

// UAT-HN-011 — adding a Note annotation flips the H&N picker entry
// from disabled to enabled on the next store-changed cycle. Removing
// the only annotation flips it back to disabled.
void TestUatSearchAndMarkup::uat_hn_011_highlightsModeEnabledAfterAddingNote() {
    QVERIFY(m_scratch.isValid());
    const QString pdfPath = writePdfWithKeyword(
        m_scratch.filePath(QStringLiteral("uat_hn_011.pdf")),
        QStringLiteral("phoenix"));

    auto* app = qobject_cast<Application*>(qApp);
    QVERIFY(app);
    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow* mw = currentMainWindow();
    QVERIFY(mw);
    QAction* hnAction = findHighlightsNotesAction(mw);
    QVERIFY(hnAction);
    QVERIFY(!hnAction->isEnabled());

    auto* doc = mw->findChild<DocumentView*>()->currentDocument();
    QVERIFY(doc);
    auto* store = doc->annotations();
    QVERIFY(store);

    Annotation note;
    note.type = AnnotationType::Note;
    note.page = 0;
    note.bounds = QRectF(100, 100, 24, 24);
    note.text = QStringLiteral("Re-read this part.");
    store->add(note);
    QApplication::processEvents();
    QTRY_VERIFY_WITH_TIMEOUT(hnAction->isEnabled(), 2000);

    // Removing it flips back to disabled.
    const auto rows = store->annotations();
    QCOMPARE(rows.size(), size_t(1));
    store->remove(rows[0].id);
    QApplication::processEvents();
    QTRY_VERIFY_WITH_TIMEOUT(!hnAction->isEnabled(), 2000);
}

// UAT-HN-012 — the H&N list is filtered to text-content annotation
// types. A document with a Rectangle (pure shape) plus a Highlight
// (text-content) shows only the Highlight in the H&N list.
void TestUatSearchAndMarkup::uat_hn_012_listFiltersToTextContentTypes() {
    QVERIFY(m_scratch.isValid());
    const QString pdfPath = writePdfWithKeyword(
        m_scratch.filePath(QStringLiteral("uat_hn_012.pdf")),
        QStringLiteral("dragon"));

    auto* app = qobject_cast<Application*>(qApp);
    QVERIFY(app);
    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow* mw = currentMainWindow();
    QVERIFY(mw);
    auto* sidebar = mw->findChild<Sidebar*>();
    QVERIFY(sidebar);
    auto* doc = mw->findChild<DocumentView*>()->currentDocument();
    QVERIFY(doc);
    auto* store = doc->annotations();
    QVERIFY(store);

    Annotation rect;
    rect.type = AnnotationType::Rectangle;
    rect.page = 0;
    rect.bounds = QRectF(50, 50, 200, 100);
    store->add(rect);
    Annotation hl;
    hl.type = AnnotationType::Highlight;
    hl.page = 0;
    hl.bounds = QRectF(60, 200, 180, 30);
    hl.text = QStringLiteral("Important paragraph.");
    store->add(hl);
    QApplication::processEvents();

    // Only the Highlight counts toward Highlights & Notes; the
    // Rectangle is a pure-shape annotation.
    QCOMPARE(sidebar->highlightsAndNotesCount(), 1);

    sidebar->setMode(Sidebar::Mode::HighlightsAndNotes);
    QApplication::processEvents();
    auto* list = sidebar->findChild<QListWidget*>();
    QVERIFY(list);
    QCOMPARE(list->count(), 1);
    QVERIFY(list->item(0)->text().contains(QStringLiteral("Highlight")));
}

// UAT-ANN-125 — Edit > Select All (Cmd/Ctrl+A) while annotations
// exist selects all of them. selectedAnnotationIds() reports all ids;
// the primary selectedAnnotationId() is set to one of them.
void TestUatSearchAndMarkup::uat_ann_125_selectAllSelectsEveryAnnotation() {
    AnnEditingFixture f = buildAnnEditingFixture(m_scratch,
                                                  QStringLiteral("125"));
    QVERIFY(f.overlay);
    QVERIFY(f.drawnId != 0);

    // Add a second annotation so selectAll has more than one to select.
    auto* markup = f.mw->findChild<MarkupToolbar*>();
    QVERIFY(markup);
    QAction* rectAction = findToolAction(markup, QStringLiteral("Rectangle"));
    QVERIFY(rectAction);
    rectAction->setChecked(true);
    QApplication::processEvents();
    dragOnOverlay(f.overlay, QPoint(400, 100), QPoint(500, 180));
    QApplication::processEvents();
    QCOMPARE(f.store->count(), 2);

    // Switch back to Select tool.
    QAction* selectAction = findToolAction(markup, QStringLiteral("Select"));
    if (selectAction) { selectAction->setChecked(true); QApplication::processEvents(); }

    // Trigger Select All via the menu action.
    QAction* selectAllAction = findMenuAction(
        f.mw->menuBar(), QStringLiteral("&Edit"),
        QStringLiteral("Select &All"));
    QVERIFY2(selectAllAction, "Edit > Select All action must exist");
    QVERIFY2(selectAllAction->isEnabled(),
             "Select All must be enabled for a document with annotations");
    selectAllAction->trigger();
    QApplication::processEvents();

    // All annotation ids should be reported as selected.
    const std::vector<int> selected = f.overlay->selectedAnnotationIds();
    QCOMPARE(static_cast<int>(selected.size()), 2);
    QVERIFY2(f.overlay->selectedAnnotationId() != 0,
             "Primary selection must be set after selectAll()");
}

// UAT-ANN-126 — Select All followed by Delete removes every annotation
// in a single undo step (one Undo restores all of them).
void TestUatSearchAndMarkup::uat_ann_126_selectAllThenDeleteRemovesAllInOneUndo() {
    AnnEditingFixture f = buildAnnEditingFixture(m_scratch,
                                                  QStringLiteral("126"));
    QVERIFY(f.overlay);
    QVERIFY(f.drawnId != 0);

    // Add a second annotation.
    auto* markup = f.mw->findChild<MarkupToolbar*>();
    QVERIFY(markup);
    QAction* rectAction = findToolAction(markup, QStringLiteral("Rectangle"));
    QVERIFY(rectAction);
    rectAction->setChecked(true);
    QApplication::processEvents();
    dragOnOverlay(f.overlay, QPoint(400, 100), QPoint(500, 180));
    QApplication::processEvents();
    QCOMPARE(f.store->count(), 2);

    // Switch back to Select tool.
    QAction* selectAction = findToolAction(markup, QStringLiteral("Select"));
    if (selectAction) { selectAction->setChecked(true); QApplication::processEvents(); }

    // Select All then Delete.
    QAction* selectAllAction = findMenuAction(
        f.mw->menuBar(), QStringLiteral("&Edit"),
        QStringLiteral("Select &All"));
    QVERIFY(selectAllAction);
    selectAllAction->trigger();
    QApplication::processEvents();

    sendKey(f.overlay, Qt::Key_Delete);
    QApplication::processEvents();

    QCOMPARE(f.store->count(), 0);
    QCOMPARE(f.overlay->selectedAnnotationId(), 0);

    // Undo should restore all annotations in one step.
    QVERIFY(f.store->canUndo());
    f.store->undo();
    QApplication::processEvents();
    QCOMPARE(f.store->count(), 2);
}

// UAT-ANN-127 — Drag-to-move an annotation produces exactly one undo
// step. Workstream D3 wraps the per-frame update() calls during a
// drag inside beginCompound/endCompound; without it a 60-frame drag
// pushed 60 history frames and Ctrl+Z unwound the drag in micro-
// steps, hanging the UI on Sidebar rebuilds.
void TestUatSearchAndMarkup::uat_ann_127_dragGeneratesOneUndoStep() {
    AnnEditingFixture f = buildAnnEditingFixture(m_scratch, QStringLiteral("127"));
    QVERIFY(f.overlay);
    QVERIFY(f.drawnId != 0);
    QCOMPARE(f.store->count(), 1);

    // Select the rectangle first (Select tool is sticky: a single
    // click selects, a second click + drag begins the move).
    sendMouse(f.overlay, QEvent::MouseButtonPress, QPoint(260, 295), Qt::LeftButton);
    sendMouse(f.overlay, QEvent::MouseButtonRelease, QPoint(260, 295), Qt::LeftButton);
    QApplication::processEvents();
    QCOMPARE(f.overlay->selectedAnnotationId(), f.drawnId);

    const QRectF before = f.store->find(f.drawnId)->bounds;

    // Drag from inside the annotation to a new position. Multiple
    // mouse-move events fire per drag — each would push its own undo
    // frame without compound mode.
    sendMouse(f.overlay, QEvent::MouseButtonPress, QPoint(260, 295), Qt::LeftButton);
    for (int i = 1; i <= 20; ++i) {
        sendMouse(f.overlay, QEvent::MouseMove, QPoint(260 + i * 2, 295 + i * 2),
                  Qt::LeftButton);
    }
    sendMouse(f.overlay, QEvent::MouseButtonRelease, QPoint(300, 335), Qt::LeftButton);
    QApplication::processEvents();

    const QRectF after = f.store->find(f.drawnId)->bounds;
    QVERIFY2(after.topLeft() != before.topLeft(),
             "Drag should have shifted the annotation's top-left in doc space");

    // Count how many undos it takes to revert the drag back to the
    // pre-drag bounds. With compound mode, this must be exactly one.
    int steps = 0;
    while (f.store->canUndo() && f.store->find(f.drawnId)
           && f.store->find(f.drawnId)->bounds != before) {
        f.store->undo();
        ++steps;
        if (steps > 5) break; // belt-and-braces, never expected to hit
    }
    QCOMPARE(steps, 1);
    QVERIFY(f.store->find(f.drawnId) != nullptr);
    QCOMPARE(f.store->find(f.drawnId)->bounds, before);
}

// UAT-ANN-128 — Clicking on an existing annotation while a drawing
// tool (e.g. Arrow) is active selects the annotation rather than
// creating a new overlapping one. Workstream D1: the press handler
// hit-tests existing annotations BEFORE the drawing-tool path.
void TestUatSearchAndMarkup::uat_ann_128_clickOnAnnotationWithDrawingToolSelects() {
    AnnEditingFixture f = buildAnnEditingFixture(m_scratch, QStringLiteral("128"));
    QVERIFY(f.overlay);
    QVERIFY(f.drawnId != 0);
    QCOMPARE(f.store->count(), 1);

    // Switch to the Rectangle drawing tool (a "draws on drag" tool;
    // Arrow / Ellipse would behave the same — Rectangle is the easy
    // one to drive through the markup-toolbar action).
    auto *markup = f.mw->findChild<MarkupToolbar *>();
    QVERIFY(markup);
    QAction *rectAction = findToolAction(markup, QStringLiteral("Rectangle"));
    QVERIFY(rectAction);
    rectAction->setChecked(true);
    QApplication::processEvents();
    QVERIFY(f.overlay->activeTool() == AnnotationTool::Rectangle);

    // Click on the existing rectangle (no drag — press + release at
    // the same point).
    sendMouse(f.overlay, QEvent::MouseButtonPress, QPoint(260, 295), Qt::LeftButton);
    sendMouse(f.overlay, QEvent::MouseButtonRelease, QPoint(260, 295), Qt::LeftButton);
    QApplication::processEvents();

    QCOMPARE(f.store->count(), 1); // no new annotation created
    QCOMPARE(f.overlay->selectedAnnotationId(), f.drawnId);
}

// UAT-ANN-130 — Opening the Inspector's Stroke colour picker on a
// selected annotation must NOT cause the annotation to vanish, even
// when the AnnotationStore mutates while the modal is open.
//
// Background: AnnotationStore stores annotations in a std::vector
// and AnnotationStore::find(id) returns a raw pointer into that
// vector. The Inspector's stroke / fill button handlers used to:
//
//   const Annotation *a = m_store->find(m_id);  // pointer into vector
//   const QColor c = QColorDialog::getColor(a->style.stroke, ...);
//                                          // ^ modal — spins event loop
//   Annotation updated = *a;                    // dereferences a stale ptr
//   m_store->update(updated);                   // writes garbage back
//
// Any store mutation that fires during the modal (auto-save, queued
// changed-slot, undo coalescing, redo from a parallel dialog, etc.)
// can reallocate the vector. After the modal returns, `a` points at
// freed memory. The dereference reads garbage geometry/style, and
// update(garbage) corrupts the entry with the original id — the
// rectangle appears to vanish (off-page / zero-size bounds) and the
// colour change is "lost" (overwritten by the garbage style).
//
// 2026-05-20 HITL pass surfaced the bug live. Fix: snapshot the
// pre-dialog colour, then re-fetch by id AFTER the modal. This UAT
// drives the Stroke handler with a synthetic mid-modal store
// mutation (simulating any of the racing paths above) and asserts
// the original rectangle survives with the new colour applied.
void TestUatSearchAndMarkup::uat_ann_130_strokeDialogSurvivesStoreMutation() {
    AnnEditingFixture f = buildAnnEditingFixture(m_scratch, QStringLiteral("130"));
    QVERIFY(f.overlay);
    QVERIFY(f.store);
    QVERIFY(f.drawnId != 0);
    QCOMPARE(f.store->count(), 1);

    // Select the rectangle so the Inspector binds to it.
    sendMouse(f.overlay, QEvent::MouseButtonPress, QPoint(260, 295), Qt::LeftButton);
    sendMouse(f.overlay, QEvent::MouseButtonRelease, QPoint(260, 295), Qt::LeftButton);
    QApplication::processEvents();
    QCOMPARE(f.overlay->selectedAnnotationId(), f.drawnId);

    // Locate the Inspector's Stroke button by objectName. We don't
    // need the Inspector dock to be visible — the button click slot
    // runs regardless of dock visibility (the connect is in the
    // ctor).
    auto *strokeBtn = f.mw->findChild<QToolButton *>(
        QStringLiteral("trailer.inspector.strokeButton"));
    QVERIFY2(strokeBtn, "Inspector Stroke button not found by objectName");

    // Snapshot the original bounds — the bug had the rectangle's
    // bounds going to zero/garbage after the modal closed. Guard the
    // find()-deref so a fixture regression yields a readable QVERIFY
    // failure rather than a segfault.
    const Annotation *original = f.store->find(f.drawnId);
    QVERIFY2(original != nullptr, "Fixture rectangle missing from store before colour pick");
    const QRectF originalBounds = original->bounds;
    QVERIFY(!originalBounds.isEmpty());

    // Hook into the QColorDialog the moment it shows. The headless
    // (offscreen) Qt plugin always uses Qt's own widget dialog (not
    // a native colour panel), so we can findChild it from
    // top-level widgets. While the dialog is up we (a) mutate the
    // store enough to force a std::vector reallocation, then (b)
    // pick a colour and accept. The pre-fix code would write
    // garbage back through a dangling pointer; the post-fix code
    // re-fetches by id and stays consistent.
    AnnotationStore *store = f.store;
    const int targetId = f.drawnId;
    bool dialogHandled = false;
    // Cap polling attempts so a regression that stops the dialog
    // from appearing fails loudly instead of hanging the suite.
    // 100 ticks * 20 ms = 2 s budget for the modal to surface.
    int attempts = 0;
    constexpr int kMaxAttempts = 100;
    auto poller = std::make_unique<QTimer>();
    QObject::connect(poller.get(), &QTimer::timeout, store,
                     [store, &dialogHandled, &attempts, p = poller.get()]() {
                         ++attempts;
                         QColorDialog *dlg = nullptr;
                         for (auto *w : QApplication::topLevelWidgets()) {
                             if (auto *d = qobject_cast<QColorDialog *>(w)) {
                                 dlg = d;
                                 break;
                             }
                         }
                         if (!dlg) {
                             if (attempts >= kMaxAttempts) {
                                 p->stop();
                             }
                             return;
                         }
                         // Mutate the store while the modal is open.
                         // Adding many annotations forces the vector
                         // to reallocate at least once at typical
                         // libstdc++ growth factors; the original
                         // find(targetId) pointer would be invalid.
                         for (int i = 0; i < 64; ++i) {
                             Annotation throwaway;
                             throwaway.type = AnnotationType::Rectangle;
                             throwaway.page = 0;
                             throwaway.bounds = QRectF(0, 0, 1, 1);
                             store->add(std::move(throwaway));
                         }
                         dlg->setCurrentColor(QColor(255, 0, 0));
                         dlg->accept();
                         dialogHandled = true;
                         p->stop();
                     });
    poller->start(20);

    // Trigger the stroke-colour handler. This will invoke
    // QColorDialog::getColor under the hood, which calls
    // QDialog::exec → spins the event loop. The poller above runs
    // INSIDE that spun loop and supplies the colour + mutates the
    // store before exec returns.
    strokeBtn->click();
    QApplication::processEvents();

    QVERIFY2(dialogHandled, "QColorDialog never reached the polling "
                            "intercept — modal handling may have changed "
                            "(or the dialog uses a non-widget native panel)");

    // Post-conditions: the original rectangle still exists with its
    // original bounds (no garbage geometry leaked in), AND its
    // stroke colour is now red (the writeback worked through a
    // freshly re-fetched pointer).
    const Annotation *survivor = store->find(targetId);
    QVERIFY2(survivor != nullptr, "Original rectangle was wiped from the "
                                  "store — find() pointer was held across the modal");
    QCOMPARE(survivor->bounds, originalBounds);
    QCOMPARE(survivor->style.stroke, QColor(255, 0, 0));
}

// UAT-ANN-131 — After committing a freshly-drawn shape, the markup
// toolbar auto-switches back to the Select tool so the user can grab
// the just-drawn shape to move / resize / restyle without manually
// flipping the toolbar back. 2026-05-20 HITL pass.
void TestUatSearchAndMarkup::uat_ann_131_toolSwitchesToSelectAfterShapeCommit() {
    QVERIFY(m_scratch.isValid());
    const QString pdfPath = writePdfWithKeyword(
        m_scratch.filePath(QStringLiteral("uat_ann_131.pdf")), QStringLiteral("fixture"));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    mw->resize(1100, 750);
    QApplication::processEvents();

    auto *markup = mw->findChild<MarkupToolbar *>();
    QVERIFY(markup);
    QAction *rectAction = findToolAction(markup, QStringLiteral("Rectangle"));
    QVERIFY(rectAction);
    rectAction->setChecked(true);
    QApplication::processEvents();
    QCOMPARE(markup->activeTool(), AnnotationTool::Rectangle);

    auto *overlay = mw->findChild<AnnotationOverlay *>();
    QVERIFY(overlay);
    QCOMPARE(overlay->activeTool(), AnnotationTool::Rectangle);

    dragOnOverlay(overlay, QPoint(200, 250), QPoint(320, 340));
    QApplication::processEvents();

    // Post-drag: the rectangle was committed AND the toolbar / overlay
    // both flipped back to Select. Without the auto-switch the user
    // would have to click Select manually before they could grab the
    // shape they just drew.
    QCOMPARE(markup->activeTool(), AnnotationTool::Select);
    QCOMPARE(overlay->activeTool(), AnnotationTool::Select);
}

// Custom main mirrors test_uat_foundations.cpp: sandbox HOME / XDG
// so Settings and RecentFiles don't touch the user's real config.
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
    TestUatSearchAndMarkup tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_uat_search_and_markup.moc"
