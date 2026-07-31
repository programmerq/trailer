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
#include "ui/FormToolbar.h"
#include "ui/MainWindow.h"
#include "ui/MarkupToolbar.h"
#include "ui/SearchBar.h"
#include "ui/Sidebar.h"

#include <QAction>
#include <QColorDialog>
#include <QDir>
#include <QDockWidget>
#include <QFont>
#include <QHash>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPageSize>
#include <QPainter>
#include <QPdfSearchModel>
#include <QPdfView>
#include <QPdfWriter>
#include <QPlainTextEdit>
#include <QPointingDevice>
#include <QTabletEvent>
#include <QSettings>
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

// Writes a multi-page A4 PDF that stamps `keyword` as real (selectable,
// searchable) text on exactly the 0-based page indices in `pages`, one
// match per listed page. Every other page gets keyword-free filler text
// so the page exists but yields no search hit. This is the fixture the
// position-aware seed threshold (ADR 0006) needs: matches on distinct,
// known pages so the seed can be asserted against currentPage().
// Deterministic: page order is fixed and QPdfSearchModel streams
// rowsInserted in page order.
QString writePdfWithKeywordOnPages(const QString &path, const QString &keyword,
                                   const QList<int> &pages, int totalPages) {
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    QPainter p(&writer);
    QFont font(QStringLiteral("Helvetica"));
    font.setPointSize(24);
    p.setFont(font);
    for (int i = 0; i < totalPages; ++i) {
        if (pages.contains(i)) {
            p.drawText(300, 400,
                       QStringLiteral("Match on page %1: %2").arg(i).arg(keyword));
        } else {
            p.drawText(300, 400,
                       QStringLiteral("Filler content on page %1 with no hit").arg(i));
        }
        if (i < totalPages - 1)
            writer.newPage();
    }
    p.end();
    return path;
}

// Like writePdfWithKeywordOnPages, but one designated `seedPage` carries
// `seedPageCount` (>=2) occurrences of `keyword` stamped at known,
// ASCENDING y positions — so reading order (top-to-bottom) is
// deterministic and the tie-break rule (ADR 0006 item 3 / R4 — the
// earliest-indexed match on the at/after-page seed wins) can be asserted.
// Every other page in `pages` still gets exactly one match; `seedPage`
// gets `seedPageCount` instead of one. QPdfSearchModel streams matches in
// reading order within a page, so the topmost (smallest y) occurrence is
// the earliest model index on that page.
QString writePdfWithMultiMatchSeedPage(const QString &path, const QString &keyword,
                                       const QList<int> &pages, int totalPages, int seedPage,
                                       int seedPageCount) {
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    QPainter p(&writer);
    QFont font(QStringLiteral("Helvetica"));
    font.setPointSize(24);
    p.setFont(font);
    for (int i = 0; i < totalPages; ++i) {
        if (i == seedPage) {
            // Ascending y => the j==0 stamp is topmost => the earliest
            // reading-order match => the smallest model index on this page.
            for (int j = 0; j < seedPageCount; ++j) {
                p.drawText(300, 400 + j * 800,
                           QStringLiteral("Seed-page hit #%1: %2").arg(j).arg(keyword));
            }
        } else if (pages.contains(i)) {
            p.drawText(300, 400, QStringLiteral("Match on page %1: %2").arg(i).arg(keyword));
        } else {
            p.drawText(300, 400,
                       QStringLiteral("Filler content on page %1 with no hit").arg(i));
        }
        if (i < totalPages - 1)
            writer.newPage();
    }
    p.end();
    return path;
}

// Persistent screenshot dir (survives past the run, unlike
// QTemporaryDir) for G2 evidence PNGs. Mirrors the helper in
// test_uat_foundations.cpp; file-local so each translation unit keeps
// its own copy.
QString screenshotDir() {
    QDir dir(QDir::current());
    dir.mkpath(QStringLiteral("uat-screenshots"));
    return dir.absoluteFilePath(QStringLiteral("uat-screenshots"));
}

void grabTo(QWidget *w, const QString &name) {
    const QString dir = screenshotDir();
    QVERIFY2(QDir().mkpath(dir) || QDir(dir).exists(),
             qPrintable(QStringLiteral("G2 screenshot dir unavailable: %1").arg(dir)));
    const QString path = QDir(dir).absoluteFilePath(name);
    const bool saved = w->grab().save(path, "PNG");
    QVERIFY2(saved, qPrintable(QStringLiteral("G2 screenshot save failed: %1").arg(path)));
    qInfo().noquote() << "G2-SCREENSHOT" << path;
}

// Reads the 0-based page of the currently-seeded search match straight
// from the view's search model (the same Page role pagesWithSearchMatches
// walks). Returns -1 if there's no current match.
int currentSeedPage(QPdfView *view) {
    if (!view || !view->searchModel())
        return -1;
    const int idx = view->currentSearchResultIndex();
    if (idx < 0 || idx >= view->searchModel()->rowCount(QModelIndex()))
        return -1;
    return view->searchModel()
        ->index(idx, 0)
        .data(static_cast<int>(QPdfSearchModel::Role::Page))
        .toInt();
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
    void uat_vwr_068_searchSeedsFirstMatchAtOrAfterCurrentPage();
    void uat_vwr_069_searchSeedPreservesWholeDocumentCoverage();
    void uat_vwr_070_searchSeedTieBreakEarliestOnPage();
    void uat_vwr_083_magnifierEscapeDeactivates();
    void uat_ann_010_rectangleToolCreatesAnnotation();
    void uat_ann_012_lineToolCreatesAnnotation();
    void uat_ann_017_selectToolDragDoesNotCreateAnnotation();
    void uat_ann_018_inkStrokeCapturesVaryingPressure();
    void uat_ann_036_highlightStoresQuadPerTextRun();
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
    void uat_ann_128_drawingToolPressStartsNewShape();
    void uat_ann_129_freehandPressOverInkStartsNewStroke();
    void uat_ann_130_strokeDialogSurvivesStoreMutation();
    void uat_ann_131_boundedShapeStaysStickyAfterCommit();
    void uat_ann_132_freehandStaysStickyAfterStroke();
    void uat_ann_133_boundedToolsDrawFirstOverExisting();
    void uat_ann_134_boundedToolsAreStickyViaToolbar();
    void uat_ann_140_interleavedUndoIsChronological();
    void uat_toc_010_outlineDisabledOnPlainPdf();
    void uat_toc_011_outlineExposedForPdfWithBookmarks();
    void uat_toc_012_clickingOutlineEntryNavigatesToPage();
    void uat_hn_010_highlightsModeDisabledForEmptyDoc();
    void uat_hn_011_highlightsModeEnabledAfterAddingNote();
    void uat_hn_012_listFiltersToTextContentTypes();
    void uat_xct_070_toolbarAnchoringAndOverflow();
    void uat_xct_074_formActivationWhileMarkupVisibleKeepsMainAnchored();
    void uat_xct_075_staleWindowStateBlobDoesNotResurrectOldToolbarOrder();
    void uat_xct_076_toggleAnyToolbarNeverMovesAnotherToolbarsActions();
    void uat_xct_077_staleWindowStateBlobViaPerTypeDefaultAlsoReasserted();

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

// UAT-VWR-068 — Search seeds the first match at or after the current
// page (ADR 0006, accepted — Option B). Opening Find while reading a
// middle page must select the first match whose page is >= the current
// page, wrapping to index 0 if the viewport is past the last match —
// not the always-index-0 seed that shipped before. Coverage stays
// whole-document (asserted in UAT-VWR-069); only the seed index moves.
//
// The seed is read ONLY after QTRY_VERIFY confirms the model is fully
// populated. QPdfSearchModel streams rowsInserted in page order, so an
// earlier-page match is inserted before the current-page match; the
// full-population wait is the decisive async-populate guard (ADR 0006
// R1/R2): if the seed froze on a provisional earlier-page match pushed
// mid-stream, the at/after assertion below would fail.
void TestUatSearchAndMarkup::uat_vwr_068_searchSeedsFirstMatchAtOrAfterCurrentPage() {
    QVERIFY(m_scratch.isValid());
    const QString keyword = QStringLiteral("zephyrquux");
    // Matches on distinct, known 0-based pages; 13-page document.
    const QList<int> matchPages{2, 5, 8, 11};
    const int totalPages = 13;
    const QString pdfPath = writePdfWithKeywordOnPages(
        m_scratch.filePath(QStringLiteral("uat_vwr_068.pdf")), keyword, matchPages, totalPages);

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
    QVERIFY(findAction);
    findAction->trigger();
    QApplication::processEvents();

    auto *searchBar = mw->findChild<SearchBar *>();
    QVERIFY(searchBar);
    auto *lineEdit = searchBar->findChild<QLineEdit *>();
    QVERIFY(lineEdit);

    auto *view = mw->findChild<QPdfView *>();
    QVERIFY(view);

    const int knownTotal = static_cast<int>(matchPages.size());

    // Re-seed helper: clear any prior query, navigate to `page`, type the
    // keyword, and wait for the model to FULLY populate (rowCount ==
    // knownTotal) before reading the seed. Reading only after full
    // population is the async guard — see the slot comment.
    // Writes the resulting 0-based seed page into `seedPage`. Kept void
    // (not int-returning) because QtTest's QTRY_* macros expand to a
    // `return;` on failure, which is ill-formed in a value-returning
    // lambda.
    int seedPage = -1;
    auto seedFromPage = [&](int page) {
        lineEdit->clear();
        QApplication::processEvents();
        doc->goToPage(page);
        QApplication::processEvents();
        QTRY_COMPARE_WITH_TIMEOUT(doc->currentPage(), page, 2000);
        lineEdit->setText(keyword);
        QApplication::processEvents();
        QTRY_VERIFY_WITH_TIMEOUT(view->searchModel() != nullptr &&
                                     view->searchModel()->rowCount(QModelIndex()) == knownTotal,
                                 5000);
        QTRY_VERIFY_WITH_TIMEOUT(view->currentSearchResultIndex() >= 0, 5000);
        seedPage = currentSeedPage(view);
    };

    // (a) Seed at/after current page, first such. Reading page 9 on a doc
    //     whose matches sit on pages {2,5,8,11}: the seed is page 11's
    //     match (first page >= 9), NOT page 2's.
    seedFromPage(9);
    QVERIFY2(seedPage >= 9, "Seed page must be at or after the current page");
    QCOMPARE(seedPage, 11);
    // The 1-based counter maps to that same match (4 of 4 here).
    QCOMPARE(doc->currentSearchMatchIndex(), static_cast<int>(matchPages.indexOf(11)) + 1);

    // G2 evidence of the seeded-search state.
    grabTo(mw, QStringLiteral("vwr068_seed_at_or_after_page.png"));

    // (b) On-current-page equality (>=, not >). A match sitting ON the
    //     current page must be the seed.
    seedFromPage(8);
    QCOMPARE(seedPage, 8);

    // (c) Wrap past the last match. Viewport past page 11 (the last
    //     match): the seed wraps to index 0 (page 2's match).
    seedFromPage(12);
    QCOMPARE(seedPage, 2);
    QCOMPARE(view->currentSearchResultIndex(), 0);
}

// UAT-VWR-069 — The position-aware seed preserves whole-document
// coverage (ADR 0006 guardian invariant). Only the initial seed index
// moves; the populated result set still spans the whole document and
// Find Previous from the seed reaches the earlier-page matches. Guards
// against a naive "seed at current page" that would filter the results
// to pages >= current and hide earlier hits.
void TestUatSearchAndMarkup::uat_vwr_069_searchSeedPreservesWholeDocumentCoverage() {
    QVERIFY(m_scratch.isValid());
    const QString keyword = QStringLiteral("zephyrquux");
    const QList<int> matchPages{2, 5, 8, 11};
    const int totalPages = 13;
    const QString pdfPath = writePdfWithKeywordOnPages(
        m_scratch.filePath(QStringLiteral("uat_vwr_069.pdf")), keyword, matchPages, totalPages);

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

    QAction *findAction =
        findMenuAction(mw->menuBar(), QStringLiteral("&Edit"), QStringLiteral("&Find…"));
    QVERIFY(findAction);
    findAction->trigger();
    QApplication::processEvents();

    auto *searchBar = mw->findChild<SearchBar *>();
    QVERIFY(searchBar);
    auto *lineEdit = searchBar->findChild<QLineEdit *>();
    QVERIFY(lineEdit);

    auto *view = mw->findChild<QPdfView *>();
    QVERIFY(view);

    const int knownTotal = static_cast<int>(matchPages.size());

    doc->goToPage(9);
    QApplication::processEvents();
    QTRY_COMPARE_WITH_TIMEOUT(doc->currentPage(), 9, 2000);

    lineEdit->setText(keyword);
    QApplication::processEvents();
    QTRY_VERIFY_WITH_TIMEOUT(view->searchModel() != nullptr &&
                                 view->searchModel()->rowCount(QModelIndex()) == knownTotal,
                             5000);
    QTRY_VERIFY_WITH_TIMEOUT(view->currentSearchResultIndex() >= 0, 5000);

    // (d) Coverage unchanged: the denominator spans the whole document —
    //     matches before page 9 are still present, not filtered out.
    QCOMPARE(view->searchModel()->rowCount(QModelIndex()), knownTotal);
    QCOMPARE(doc->searchMatchCount(), knownTotal);

    // Seed landed on page 11 (the at/after-page match).
    QCOMPARE(currentSeedPage(view), 11);

    // Find Previous from the seed walks back through the earlier-page
    // matches — down to page 2, which is before the current page 9 — so
    // the earlier hits are reachable and coverage is whole-document.
    emit searchBar->findPreviousRequested();
    QApplication::processEvents();
    QCOMPARE(currentSeedPage(view), 8);

    emit searchBar->findPreviousRequested();
    QApplication::processEvents();
    QCOMPARE(currentSeedPage(view), 5);

    emit searchBar->findPreviousRequested();
    QApplication::processEvents();
    QCOMPARE(currentSeedPage(view), 2);
}

// UAT-VWR-070 — Tie-break (ADR 0006 item 3 / R4): when the at/after-page
// seed page carries several matches, the seed lands on the EARLIEST model
// index on that page — the first match in reading order — not a later
// occurrence. The fixture stamps TWO occurrences on the seed page at
// known, ascending y positions, so a regression returning the LAST match
// on the seed page lands on the higher index and fails this test.
void TestUatSearchAndMarkup::uat_vwr_070_searchSeedTieBreakEarliestOnPage() {
    QVERIFY(m_scratch.isValid());
    const QString keyword = QStringLiteral("zephyrquux");
    // One match each on pages 2 and 8; the SEED page (5) carries TWO.
    const QList<int> matchPages{2, 5, 8};
    const int totalPages = 13;
    const int seedPage = 5;
    const int seedPageCount = 2;
    const QString pdfPath = writePdfWithMultiMatchSeedPage(
        m_scratch.filePath(QStringLiteral("uat_vwr_070.pdf")), keyword, matchPages, totalPages,
        seedPage, seedPageCount);

    // Global match layout in page/reading order:
    //   idx 0 → page 2 (single)
    //   idx 1 → page 5, TOP occurrence  (the correct tie-break seed)
    //   idx 2 → page 5, lower occurrence (a "last match" regression lands here)
    //   idx 3 → page 8 (single)
    const int knownTotal = 4;
    const int expectedSeedIndex = 1; // earliest model index on the seed page

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
    QVERIFY(findAction);
    findAction->trigger();
    QApplication::processEvents();

    auto *searchBar = mw->findChild<SearchBar *>();
    QVERIFY(searchBar);
    auto *lineEdit = searchBar->findChild<QLineEdit *>();
    QVERIFY(lineEdit);
    auto *view = mw->findChild<QPdfView *>();
    QVERIFY(view);

    doc->goToPage(seedPage);
    QApplication::processEvents();
    QTRY_COMPARE_WITH_TIMEOUT(doc->currentPage(), seedPage, 2000);

    lineEdit->setText(keyword);
    QApplication::processEvents();
    // Read the seed only after FULL population (async guard) — same wait
    // as the other position-aware seed tests.
    QTRY_VERIFY_WITH_TIMEOUT(view->searchModel() != nullptr &&
                                 view->searchModel()->rowCount(QModelIndex()) == knownTotal,
                             5000);
    QTRY_VERIFY_WITH_TIMEOUT(view->currentSearchResultIndex() >= 0, 5000);

    // The seed sits on the seed page…
    QCOMPARE(currentSeedPage(view), seedPage);
    // …and specifically on the EARLIEST model index on that page (the
    // first reading-order occurrence). A regression returning the last
    // match on the seed page would land on index 2 here and fail.
    QCOMPARE(view->currentSearchResultIndex(), expectedSeedIndex);
    QCOMPARE(doc->currentSearchMatchIndex(), expectedSeedIndex + 1); // 1-based counter

    grabTo(mw, QStringLiteral("vwr070_tiebreak_earliest_on_page.png"));
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

// UAT-ANN-018 — Ink strokes capture per-sample pen pressure.
//
// Pressure-aware freehand: stylus input drives AnnotationOverlay::
// tabletEvent, which records a pressure per point. The committed Ink
// annotation must carry a `pressures` vector parallel to its points,
// reflecting the varying pressure so the renderer can taper the
// stroke. Driven with synthetic QTabletEvents (a plain mouse can't
// carry varying pressure).
void TestUatSearchAndMarkup::uat_ann_018_inkStrokeCapturesVaryingPressure() {
    QVERIFY(m_scratch.isValid());
    const QString pdfPath = writePdfWithKeyword(
        m_scratch.filePath(QStringLiteral("uat_ann_018.pdf")), QStringLiteral("fixture"));

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

    auto *overlay = mw->findChild<AnnotationOverlay *>();
    QVERIFY(overlay);
    overlay->setActiveTool(AnnotationTool::Ink);

    const QPointingDevice *dev = QPointingDevice::primaryPointingDevice();
    QVERIFY(dev);
    auto sendTablet = [&](QEvent::Type type, QPoint pos, qreal pressure, Qt::MouseButton button,
                          Qt::MouseButtons buttons) {
        QTabletEvent ev(type, dev, QPointF(pos), QPointF(overlay->mapToGlobal(pos)), pressure, 0.0f,
                        0.0f, 0.0f, 0.0, 0.0f, Qt::NoModifier, button, buttons);
        QApplication::sendEvent(overlay, &ev);
        QApplication::processEvents();
    };

    const int before = store->count();
    sendTablet(QEvent::TabletPress, QPoint(180, 240), 0.2, Qt::LeftButton, Qt::LeftButton);
    sendTablet(QEvent::TabletMove, QPoint(230, 270), 0.6, Qt::NoButton, Qt::LeftButton);
    sendTablet(QEvent::TabletMove, QPoint(290, 320), 0.95, Qt::NoButton, Qt::LeftButton);
    sendTablet(QEvent::TabletRelease, QPoint(290, 320), 0.0, Qt::LeftButton, Qt::NoButton);

    QCOMPARE(store->count(), before + 1);
    const Annotation &a = store->annotations().back();
    QCOMPARE(a.type, AnnotationType::Ink);
    QVERIFY2(!a.pressures.empty(), "Ink from a pressure device must carry a pressures vector");
    QCOMPARE(a.pressures.size(), a.points.size());
    QVERIFY2(a.pressures.front() != a.pressures.back(),
             "Captured pressures must vary across the stroke, not be a constant");
}

// UAT-ANN-036 — A Highlight over multi-run text stores one quad per run.
//
// Text-aware markup: when the text-selection provider resolves a drag
// to several runs (a selection that wraps across lines), the Highlight
// annotation must keep each run as its own quad rather than collapsing
// to a single bounding box. We inject a deterministic two-run provider
// so the assertion doesn't depend on real glyph geometry.
void TestUatSearchAndMarkup::uat_ann_036_highlightStoresQuadPerTextRun() {
    QVERIFY(m_scratch.isValid());
    const QString pdfPath = writePdfWithKeyword(
        m_scratch.filePath(QStringLiteral("uat_ann_036.pdf")), QStringLiteral("fixture"));

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

    auto *overlay = mw->findChild<AnnotationOverlay *>();
    QVERIFY(overlay);
    overlay->setActiveTool(AnnotationTool::Highlight);

    // Two runs on two lines — what a wrapped selection produces.
    overlay->setTextSelectionProvider([](QPointF, QPointF, int) {
        return std::vector<QRectF>{QRectF(40, 60, 160, 14), QRectF(40, 80, 110, 14)};
    });

    const int before = store->count();
    dragOnOverlay(overlay, QPoint(200, 250), QPoint(360, 320));

    QCOMPARE(store->count(), before + 1);
    const Annotation &a = store->annotations().back();
    QCOMPARE(a.type, AnnotationType::Highlight);
    QVERIFY2(a.quads.size() >= 2,
             "A wrapped highlight must keep one quad per text run, not collapse to a bbox");
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

    // The annotation sweep now runs on a background worker, so the restored
    // annotations land asynchronously — pump the event loop until the load
    // commits rather than reading the store synchronously.
    QTRY_VERIFY2(store2->count() >= 1, "Saved-and-reopened PDF must restore the rectangle "
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

// UAT-ANN-128 — Pressing over an existing annotation while a bounded
// drawing tool (e.g. Rectangle) is active starts a NEW shape (draw-
// first-on-press), rather than selecting the annotation underneath.
//
// This is the DRAWING-TOOL PARITY inversion of the pre-parity
// UAT-ANN-128 (owner ruling "parity", 2026-07-20; ADR
// docs/decision-records/2026-07-20-drawing-tool-parity.md). Before
// parity a bounded-tool press selected the existing shape; now
// selection is Select-tool-only (Preview-style) and the press draws a
// new overlapping shape. The press handler runs its select/move
// hit-test ONLY for the Select tool.
void TestUatSearchAndMarkup::uat_ann_128_drawingToolPressStartsNewShape() {
    AnnEditingFixture f = buildAnnEditingFixture(m_scratch, QStringLiteral("128"));
    QVERIFY(f.overlay);
    QVERIFY(f.drawnId != 0);
    QCOMPARE(f.store->count(), 1);

    // The fixture drew one rectangle spanning view (200,250)-(320,340)
    // and left the Select tool armed. Re-arm the Rectangle drawing tool
    // so the next press exercises the draw-first path.
    auto *markup = f.mw->findChild<MarkupToolbar *>();
    QVERIFY(markup);
    QAction *rectAction = findToolAction(markup, QStringLiteral("Rectangle"));
    QVERIFY(rectAction);
    rectAction->setChecked(true);
    QApplication::processEvents();
    QVERIFY(f.overlay->activeTool() == AnnotationTool::Rectangle);

    // Press-drag STARTING on top of the existing rectangle and dragging
    // off to the side.
    dragOnOverlay(f.overlay, QPoint(260, 295), QPoint(420, 400));
    QApplication::processEvents();

    // A NEW rectangle was drawn; the existing one is neither selected
    // nor removed.
    QCOMPARE(f.store->count(), 2);
    QCOMPARE(f.store->annotations().back().type, AnnotationType::Rectangle);
    QVERIFY2(f.store->annotations().back().id != f.drawnId,
             "draw-first press must create a distinct new rectangle");
    QCOMPARE(f.overlay->selectedAnnotationId(), 0);
}

// UAT-ANN-129 — With the free-form Ink tool active, a press-drag that
// starts on top of an existing Ink stroke begins a NEW stroke (like
// Preview), rather than selecting/moving the one underneath. Ink is the
// exception to UAT-ANN-128 (bounded tools select on click); otherwise
// the user could never draw over their own ink.
void TestUatSearchAndMarkup::uat_ann_129_freehandPressOverInkStartsNewStroke() {
    QVERIFY(m_scratch.isValid());
    const QString pdfPath = writePdfWithKeyword(
        m_scratch.filePath(QStringLiteral("uat_ann_129.pdf")), QStringLiteral("fixture"));

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

    auto *overlay = mw->findChild<AnnotationOverlay *>();
    QVERIFY(overlay);
    overlay->setActiveTool(AnnotationTool::Ink);
    QCOMPARE(overlay->activeTool(), AnnotationTool::Ink);

    // First freehand stroke (dragOnOverlay sends press + 2 moves +
    // release, enough for an Ink commit).
    const int before = store->count();
    dragOnOverlay(overlay, QPoint(200, 250), QPoint(320, 340));
    QCOMPARE(store->count(), before + 1);
    const Annotation firstInk = store->annotations().back();
    QCOMPARE(firstInk.type, AnnotationType::Ink);
    const int firstId = firstInk.id;
    const std::vector<QPointF> firstPoints = firstInk.points;
    // Committing an Ink stroke does not select it.
    QCOMPARE(overlay->selectedAnnotationId(), 0);

    // Second freehand stroke — STARTS INSIDE the first stroke's drawn
    // view region (the first drag ran (200,250)->(320,340), so (250,290)
    // is on it) and drags off to the side.
    dragOnOverlay(overlay, QPoint(250, 290), QPoint(430, 300));

    // A NEW, distinct Ink annotation is created; the original is neither
    // selected nor moved.
    QCOMPARE(store->count(), before + 2);
    const Annotation secondInk = store->annotations().back();
    QCOMPARE(secondInk.type, AnnotationType::Ink);
    QVERIFY2(secondInk.id != firstId, "second stroke must be a distinct annotation");
    QCOMPARE(overlay->selectedAnnotationId(), 0);

    const Annotation *orig = store->find(firstId);
    QVERIFY2(orig != nullptr, "original Ink annotation must still exist");
    QCOMPARE(orig->points.size(), firstPoints.size());
    for (size_t i = 0; i < firstPoints.size(); ++i) {
        QVERIFY2(qFuzzyCompare(orig->points[i].x() + 1.0, firstPoints[i].x() + 1.0) &&
                     qFuzzyCompare(orig->points[i].y() + 1.0, firstPoints[i].y() + 1.0),
                 "original stroke must be unchanged (not moved)");
    }
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

// UAT-ANN-131 — After committing a freshly-drawn bounded shape, the
// markup toolbar STAYS on that tool (sticky-draw), so the user can draw
// shape after shape without re-arming the tool between each.
//
// This is the DRAWING-TOOL PARITY inversion of the pre-parity
// UAT-ANN-131 (owner ruling "parity", 2026-07-20; ADR
// docs/decision-records/2026-07-20-drawing-tool-parity.md). Before
// parity the bounded shapes auto-reverted to Select on commit; now they
// match Ink and stay armed. Must drive through the MARKUP TOOLBAR: the
// revert/sticky decision lives in MainWindow::onAnnotationCommitted,
// which reads the toolbar's tool.
void TestUatSearchAndMarkup::uat_ann_131_boundedShapeStaysStickyAfterCommit() {
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

    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    AnnotationStore *store = dv->currentDocument()->annotations();
    QVERIFY(store);
    const int before = store->count();

    dragOnOverlay(overlay, QPoint(200, 250), QPoint(320, 340));
    QApplication::processEvents();

    // Post-drag: the rectangle was committed AND the toolbar / overlay
    // both STAY on Rectangle (sticky). Pre-parity they flipped to Select.
    QCOMPARE(store->count(), before + 1);
    QCOMPARE(markup->activeTool(), AnnotationTool::Rectangle);
    QCOMPARE(overlay->activeTool(), AnnotationTool::Rectangle);

    // A second drag therefore draws a SECOND rectangle rather than
    // becoming a rubber-band selection.
    dragOnOverlay(overlay, QPoint(360, 250), QPoint(460, 340));
    QApplication::processEvents();
    QCOMPARE(store->count(), before + 2);
    QCOMPARE(store->annotations().back().type, AnnotationType::Rectangle);
    QCOMPARE(overlay->selectedAnnotationId(), 0);
}

// UAT-ANN-132 — The free-form Freehand (Ink) tool is STICKY: after a
// stroke commits it stays active (Preview-style), so consecutive
// strokes all draw. This is the complement of UAT-ANN-131 (bounded
// shapes flip back to Select on commit) and the regression guard for
// CF-3 (backlog 2026-07-20-freehand-auto-revert-drawover-noop): the
// auto-revert made the user's second draw-over drag silently become a
// rubber-band selection with no feedback.
//
// Must drive through the MARKUP TOOLBAR (not overlay->setActiveTool):
// the auto-revert lives in MainWindow::onAnnotationCommitted, which
// only fires the flip-back when the toolbar's tool is a non-sticky
// tool. UAT-ANN-129 sets Ink on the overlay directly and so bypasses
// this path.
void TestUatSearchAndMarkup::uat_ann_132_freehandStaysStickyAfterStroke() {
    QVERIFY(m_scratch.isValid());
    const QString pdfPath = writePdfWithKeyword(
        m_scratch.filePath(QStringLiteral("uat_ann_132.pdf")), QStringLiteral("fixture"));

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

    // Arm the Freehand tool via the toolbar, exactly as a user click
    // would — this is the realistic path that exercises the revert.
    auto *markup = mw->findChild<MarkupToolbar *>();
    QVERIFY(markup);
    QAction *inkAction = findToolAction(markup, QStringLiteral("Freehand"));
    QVERIFY(inkAction);
    inkAction->setChecked(true);
    QApplication::processEvents();
    QCOMPARE(markup->activeTool(), AnnotationTool::Ink);

    auto *overlay = mw->findChild<AnnotationOverlay *>();
    QVERIFY(overlay);
    QCOMPARE(overlay->activeTool(), AnnotationTool::Ink);

    // First stroke.
    const int before = store->count();
    dragOnOverlay(overlay, QPoint(200, 250), QPoint(320, 340));
    QApplication::processEvents();
    QCOMPARE(store->count(), before + 1);
    QCOMPARE(store->annotations().back().type, AnnotationType::Ink);

    // STICKY: the toolbar AND overlay both remain on Ink — no revert to
    // Select. (Pre-fix: onAnnotationCommitted flipped both to Select.)
    QCOMPARE(markup->activeTool(), AnnotationTool::Ink);
    QCOMPARE(overlay->activeTool(), AnnotationTool::Ink);

    // A second press-drag therefore draws a SECOND stroke rather than
    // rubber-band-selecting — the exact silent no-op CF-3 describes.
    dragOnOverlay(overlay, QPoint(250, 290), QPoint(430, 300));
    QApplication::processEvents();
    QCOMPARE(store->count(), before + 2);
    QCOMPARE(store->annotations().back().type, AnnotationType::Ink);
    // The second drag drew; it did not select anything.
    QCOMPARE(overlay->selectedAnnotationId(), 0);
}

// UAT-ANN-133 — DRAWING-TOOL PARITY, draw-first-on-press for every
// bounded shape tool. For each of Rectangle / Ellipse / Line / Arrow:
// arm the tool, draw one shape, then press-drag STARTING on top of that
// shape — a NEW shape of the same type is created and the original is
// neither selected nor moved. Selection is Select-tool-only (Preview).
// Owner ruling "parity", 2026-07-20; ADR
// docs/decision-records/2026-07-20-drawing-tool-parity.md.
void TestUatSearchAndMarkup::uat_ann_133_boundedToolsDrawFirstOverExisting() {
    struct Case {
        const char *label;
        AnnotationTool tool;
        AnnotationType type;
    };
    const Case cases[] = {
        {"Rectangle", AnnotationTool::Rectangle, AnnotationType::Rectangle},
        {"Ellipse", AnnotationTool::Ellipse, AnnotationType::Ellipse},
        {"Line", AnnotationTool::Line, AnnotationType::Line},
        {"Arrow", AnnotationTool::Arrow, AnnotationType::Arrow},
    };

    for (const Case &c : cases) {
        QVERIFY(m_scratch.isValid());
        const QString pdfPath = writePdfWithKeyword(
            m_scratch.filePath(QStringLiteral("uat_ann_133_%1.pdf").arg(c.label)),
            QStringLiteral("fixture"));

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

        auto *markup = mw->findChild<MarkupToolbar *>();
        QVERIFY(markup);
        QAction *toolAction = findToolAction(markup, QString::fromLatin1(c.label));
        QVERIFY2(toolAction, c.label);
        toolAction->setChecked(true);
        QApplication::processEvents();
        QCOMPARE(markup->activeTool(), c.tool);

        auto *overlay = mw->findChild<AnnotationOverlay *>();
        QVERIFY(overlay);
        QCOMPARE(overlay->activeTool(), c.tool);

        // Draw the first shape spanning view (200,250)-(320,340).
        const int before = store->count();
        dragOnOverlay(overlay, QPoint(200, 250), QPoint(320, 340));
        QApplication::processEvents();
        QCOMPARE(store->count(), before + 1);
        const Annotation firstShape = store->annotations().back();
        QCOMPARE(firstShape.type, c.type);
        const int firstId = firstShape.id;
        const QRectF firstBounds = firstShape.bounds;

        // Press-drag STARTING inside the first shape's view bounds and
        // dragging off to the side. (260,295) is inside (200,250)-(320,340).
        dragOnOverlay(overlay, QPoint(260, 295), QPoint(430, 420));
        QApplication::processEvents();

        // A NEW shape of the same type is created; the original is
        // untouched and unselected.
        QCOMPARE(store->count(), before + 2);
        QCOMPARE(store->annotations().back().type, c.type);
        QVERIFY2(store->annotations().back().id != firstId,
                 "draw-first press must create a distinct new shape, not select/move the first");
        QCOMPARE(overlay->selectedAnnotationId(), 0);
        const Annotation *orig = store->find(firstId);
        QVERIFY2(orig != nullptr, "original shape must still exist");
        QCOMPARE(orig->bounds, firstBounds);
    }
}

// UAT-ANN-134 — DRAWING-TOOL PARITY, sticky-draw for every bounded
// shape tool. For each of Rectangle / Ellipse / Line / Arrow: arm the
// tool VIA THE TOOLBAR, commit one shape, and confirm the tool STAYS
// armed (toolbar AND overlay), so a second drag draws a second shape.
// Driving through the toolbar is essential — the sticky/revert decision
// lives in MainWindow::onAnnotationCommitted, which reads the toolbar's
// tool (arming on the overlay bypasses it; see UAT-ANN-132). Owner
// ruling "parity", 2026-07-20; ADR
// docs/decision-records/2026-07-20-drawing-tool-parity.md.
void TestUatSearchAndMarkup::uat_ann_134_boundedToolsAreStickyViaToolbar() {
    struct Case {
        const char *label;
        AnnotationTool tool;
        AnnotationType type;
    };
    const Case cases[] = {
        {"Rectangle", AnnotationTool::Rectangle, AnnotationType::Rectangle},
        {"Ellipse", AnnotationTool::Ellipse, AnnotationType::Ellipse},
        {"Line", AnnotationTool::Line, AnnotationType::Line},
        {"Arrow", AnnotationTool::Arrow, AnnotationType::Arrow},
    };

    for (const Case &c : cases) {
        QVERIFY(m_scratch.isValid());
        const QString pdfPath = writePdfWithKeyword(
            m_scratch.filePath(QStringLiteral("uat_ann_134_%1.pdf").arg(c.label)),
            QStringLiteral("fixture"));

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

        auto *markup = mw->findChild<MarkupToolbar *>();
        QVERIFY(markup);
        QAction *toolAction = findToolAction(markup, QString::fromLatin1(c.label));
        QVERIFY2(toolAction, c.label);
        toolAction->setChecked(true);
        QApplication::processEvents();
        QCOMPARE(markup->activeTool(), c.tool);

        auto *overlay = mw->findChild<AnnotationOverlay *>();
        QVERIFY(overlay);
        QCOMPARE(overlay->activeTool(), c.tool);

        // First shape.
        const int before = store->count();
        dragOnOverlay(overlay, QPoint(200, 250), QPoint(320, 340));
        QApplication::processEvents();
        QCOMPARE(store->count(), before + 1);
        QCOMPARE(store->annotations().back().type, c.type);

        // STICKY: toolbar AND overlay both stay on the tool — no revert.
        QCOMPARE(markup->activeTool(), c.tool);
        QCOMPARE(overlay->activeTool(), c.tool);

        // A second drag draws a SECOND shape rather than rubber-band-
        // selecting.
        dragOnOverlay(overlay, QPoint(360, 250), QPoint(460, 340));
        QApplication::processEvents();
        QCOMPARE(store->count(), before + 2);
        QCOMPARE(store->annotations().back().type, c.type);
        QCOMPARE(overlay->selectedAnnotationId(), 0);
    }
}

// UAT-ANN-140 — Interleaved page-op + annotation undo is chronological.
//
// Regression guard for the unified undo log (roadmap Now #4): a qpdf
// page delete, then an annotation, then another page delete must undo
// in strict reverse order (delete, annotation, delete) — the old
// most-recently-touched-stack heuristic undid both deletes first, so
// after two undos the document sat in a state the user never passed
// through. Driven in the real app harness (document has a live view
// attached) via the same IDocument::undo / redo the Edit > Undo / Redo
// menu actions invoke.
void TestUatSearchAndMarkup::uat_ann_140_interleavedUndoIsChronological() {
    QVERIFY(m_scratch.isValid());
    const QString pdfPath = m_scratch.filePath(QStringLiteral("uat_ann_140.pdf"));
    {
        QPdfWriter writer(pdfPath);
        writer.setPageSize(QPageSize(QPageSize::A4));
        QPainter p(&writer);
        p.drawText(100, 100, QStringLiteral("Page 1"));
        writer.newPage();
        p.drawText(100, 100, QStringLiteral("Page 2"));
        writer.newPage();
        p.drawText(100, 100, QStringLiteral("Page 3"));
        p.end();
    }

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
    QCOMPARE(doc->pageCount(), 3);
    AnnotationStore *store = doc->annotations();
    QVERIFY(store);
    QCOMPARE(store->count(), 0);

    // The user-facing undo path exists.
    QVERIFY2(findMenuAction(mw->menuBar(), QStringLiteral("&Edit"), QStringLiteral("&Undo")),
             "Edit > Undo action not found");
    QVERIFY2(findMenuAction(mw->menuBar(), QStringLiteral("&Edit"), QStringLiteral("&Redo")),
             "Edit > Redo action not found");

    // #1 delete (qpdf) -> #2 annotation -> #3 delete (qpdf)
    doc->deletePages({2});
    QCOMPARE(doc->pageCount(), 2);
    Annotation note;
    note.page = 0;
    note.type = AnnotationType::Rectangle;
    note.bounds = QRectF(10, 10, 40, 30);
    store->add(note);
    QCOMPARE(store->count(), 1);
    doc->deletePages({1});
    QCOMPARE(doc->pageCount(), 1);

    QVERIFY(doc->canUndo());
    QVERIFY(!doc->canRedo());

    doc->undo(); // reverse #3 (page delete)
    QApplication::processEvents();
    QCOMPARE(doc->pageCount(), 2);
    QCOMPARE(store->count(), 1);

    doc->undo(); // reverse #2 (annotation) — NOT another page op
    QApplication::processEvents();
    QCOMPARE(doc->pageCount(), 2);
    QCOMPARE(store->count(), 0);

    doc->undo(); // reverse #1 (page delete)
    QApplication::processEvents();
    QCOMPARE(doc->pageCount(), 3);
    QCOMPARE(store->count(), 0);

    QVERIFY(!doc->canUndo());
    QVERIFY(doc->canRedo());

    doc->redo(); // #1
    QApplication::processEvents();
    QCOMPARE(doc->pageCount(), 2);
    QCOMPARE(store->count(), 0);
    doc->redo(); // #2
    QApplication::processEvents();
    QCOMPARE(doc->pageCount(), 2);
    QCOMPARE(store->count(), 1);
    doc->redo(); // #3
    QApplication::processEvents();
    QCOMPARE(doc->pageCount(), 1);
    QCOMPARE(store->count(), 1);
}

// UAT-XCT-070 — Toolbar anchoring & overflow (ADR 0007, Option A).
//
// Geometry-provable (AGENTS.md G2) encoding of the four invariants the
// accepted decision record establishes, driven under
// QT_QPA_PLATFORM=offscreen:
//
//   #1 The main toolbar's top-left origin is stable when the form (or
//      markup) contextual bar is toggled, with the window size held
//      constant, and the main toolbar sits on the top row.
//   #2 The form toolbar's buttons are right-aligned near the search
//      field (a leading expanding spacer pushes them to the trailing
//      edge).
//   #3 At the window minimum width the widest contextual bar (markup)
//      overflows into its extension chevron while the primary row's
//      trailing search stays fully visible.
//   #4 The overflow chevron's width is pinned to a fixed constant and
//      the leading markup neighbour + primary search do not move across
//      the overflow appear/disappear transition.
//
// Grab evidence (G2 screenshots) is captured for the form-hidden,
// form-shown, and narrow-overflow states.
void TestUatSearchAndMarkup::uat_xct_070_toolbarAnchoringAndOverflow() {
    QVERIFY(m_scratch.isValid());
    const QString pdfPath = writePdfWithKeyword(
        m_scratch.filePath(QStringLiteral("uat_xct_070.pdf")), QStringLiteral("fixture"));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);

    auto *mainTb = mw->findChild<QToolBar *>(QStringLiteral("MainToolbar"));
    auto *markupTb = mw->findChild<MarkupToolbar *>();
    auto *formTb = mw->findChild<FormToolbar *>();
    QVERIFY2(mainTb, "MainToolbar not found");
    QVERIFY2(markupTb, "MarkupToolbar not found");
    QVERIFY2(formTb, "FormToolbar not found");

    // Hold the window size constant for the invariant-#1 toggle.
    mw->resize(1100, 750);
    QApplication::processEvents();

    // ---- Invariant #1: main origin stable + on the top row ----
    markupTb->hide();
    formTb->hide();
    QApplication::processEvents();

    const QPoint mainOriginFormHidden = mainTb->mapTo(mw, QPoint(0, 0));
    grabTo(mw, QStringLiteral("xct070_form_hidden.png"));

    formTb->show();
    QApplication::processEvents();
    const QPoint mainOriginFormShown = mainTb->mapTo(mw, QPoint(0, 0));
    grabTo(mw, QStringLiteral("xct070_form_shown.png"));

    QVERIFY2(mainOriginFormShown == mainOriginFormHidden,
             qPrintable(QStringLiteral("main toolbar origin moved when the form bar "
                                       "was shown: hidden=(%1,%2) shown=(%3,%4)")
                            .arg(mainOriginFormHidden.x())
                            .arg(mainOriginFormHidden.y())
                            .arg(mainOriginFormShown.x())
                            .arg(mainOriginFormShown.y())));

    // main sits on the top row: its y is <= the contextual bar's y.
    const int mainY = mainTb->mapTo(mw, QPoint(0, 0)).y();
    const int formY = formTb->mapTo(mw, QPoint(0, 0)).y();
    QVERIFY2(mainY <= formY, "main toolbar must sit on the top row (minimal y)");

    // ---- Invariant #2: form buttons right-aligned near search ----
    // The form bar is shown from #1. Its first real action is "Select";
    // the leading spacer widget is skipped by matching on the text.
    QAction *selectAction = nullptr;
    for (QAction *a : formTb->actions()) {
        if (a->text() == QStringLiteral("Select")) {
            selectAction = a;
            break;
        }
    }
    QVERIFY2(selectAction, "form toolbar Select action not found");
    QWidget *selectWidget = formTb->widgetForAction(selectAction);
    QVERIFY2(selectWidget, "form toolbar Select widget not realised");
    QVERIFY2(selectWidget->geometry().x() > formTb->width() / 2,
             qPrintable(QStringLiteral("form buttons must be right-aligned near the "
                                       "search field: first-button x=%1, half-width=%2")
                            .arg(selectWidget->geometry().x())
                            .arg(formTb->width() / 2)));

    // ---- Invariant #3: widest contextual bar overflows at min width ----
    formTb->hide();
    QApplication::processEvents();

    const int minW = mw->minimumWidth();
    QVERIFY2(minW > 0, "MainWindow must carry a minimum width (R3) so the primary "
                       "row never overflows search into its own chevron");
    mw->resize(minW, 750);
    QApplication::processEvents();
    markupTb->show();
    QApplication::processEvents();
    markupTb->layout()->activate();
    QApplication::processEvents();

    QToolButton *markupExt =
        markupTb->findChild<QToolButton *>(QStringLiteral("qt_toolbar_ext_button"));
    QVERIFY2(markupExt, "markup toolbar extension chevron object not found");
    QTRY_VERIFY2(markupExt->isVisible(),
                 "the widest contextual bar (markup) must overflow into its extension "
                 "chevron at the window minimum width");

    QToolButton *searchBtn = nullptr;
    for (QToolButton *b : mainTb->findChildren<QToolButton *>()) {
        if (b->accessibleName() == QStringLiteral("Search")) {
            searchBtn = b;
            break;
        }
    }
    QVERIFY2(searchBtn, "primary search button not found on the main toolbar");
    QVERIFY2(searchBtn->isVisible(),
             "primary search must remain visible at the window minimum width — never "
             "collapsed into the main toolbar's own chevron");
    const QRect searchInMain(searchBtn->mapTo(mainTb, QPoint(0, 0)), searchBtn->size());
    QVERIFY2(mainTb->rect().contains(searchInMain.center()),
             "primary search must sit inside the main toolbar, not the overflow menu");
    grabTo(mw, QStringLiteral("xct070_narrow_overflow.png"));

    // ---- Invariant #4: chevron width pinned + neighbours stable ----
    // The R2 stylesheet (kToolbarExtensionPinStyle) pins the chevron's
    // *content* width to 20px. The realised widget width is style-
    // dependent: Fusion frames it with ~1px on each side (→ 22px), other
    // styles add a different frame — so a hardcoded "== 22" only passes
    // under Fusion/headless-Linux and is a portability trap. Assert a
    // style-relative window instead: >= 20 (the stylesheet min-width
    // floor) and <= 24 (a generous frame allowance). This still
    // discriminates against the R2 stylesheet being removed — the
    // unpinned default extent is ~12px, which is < 20 and fails the floor.
    constexpr int kChevronMinPin = 20; // stylesheet min-width floor
    constexpr int kChevronMaxPin = 24; // + frame allowance
    const int pinnedW = markupExt->width();
    QVERIFY2(pinnedW >= kChevronMinPin && pinnedW <= kChevronMaxPin,
             qPrintable(QStringLiteral("pinned chevron width %1 must be in the "
                                       "style-relative range [%2,%3] (removing the R2 "
                                       "stylesheet drops it to the ~12px default)")
                            .arg(pinnedW)
                            .arg(kChevronMinPin)
                            .arg(kChevronMaxPin)));

    // Pin invariance: the SAME class-targeted stylesheet is set on all
    // three toolbars, so their extension chevrons must share one width.
    // The chevrons are created eagerly in Qt6 (findChild-reachable even
    // while hidden); sizeHint() reflects the QSS min/max-width clamp
    // regardless of overflow state, so it is the style-relative measure
    // to compare across bars.
    QToolButton *mainExtBtn =
        mainTb->findChild<QToolButton *>(QStringLiteral("qt_toolbar_ext_button"));
    QToolButton *formExtBtn =
        formTb->findChild<QToolButton *>(QStringLiteral("qt_toolbar_ext_button"));
    QVERIFY2(mainExtBtn && formExtBtn, "main/form extension chevrons not found");
    const int markupHint = markupExt->sizeHint().width();
    QVERIFY2(mainExtBtn->sizeHint().width() == markupHint &&
                 formExtBtn->sizeHint().width() == markupHint,
             qPrintable(QStringLiteral("all three toolbars' pinned chevrons must share "
                                       "one width (pin invariance): markup=%1 main=%2 form=%3")
                            .arg(markupHint)
                            .arg(mainExtBtn->sizeHint().width())
                            .arg(formExtBtn->sizeHint().width())));

    // The chevron must not overlap the last visible markup button.
    QAction *markupSelect = findToolAction(markupTb, QStringLiteral("Select"));
    QVERIFY(markupSelect);
    QWidget *markupSelectW = markupTb->widgetForAction(markupSelect);
    QVERIFY(markupSelectW);
    const QRect leadNarrow = markupSelectW->geometry();
    const bool searchVisibleNarrow = searchBtn->isVisible();

    // Widen so the whole markup bar fits: the chevron disappears.
    mw->resize(1500, 750);
    QApplication::processEvents();
    markupTb->layout()->activate();
    QApplication::processEvents();
    QTRY_VERIFY2(!markupExt->isVisible(),
                 "markup extension chevron must disappear once the bar fits");
    const QRect leadWide = markupSelectW->geometry();

    // The leading neighbour does not move across the overflow transition,
    // and the primary search stays visible throughout.
    QCOMPARE(leadWide, leadNarrow);
    QVERIFY2(searchVisibleNarrow && searchBtn->isVisible(),
             "primary search stays visible across the overflow appear/disappear "
             "transition");

    // Narrow again: the chevron reappears at its pinned width (its size
    // does not depend on the overflow state, so toggling it cannot reflow
    // its neighbours).
    mw->resize(minW, 750);
    QApplication::processEvents();
    markupTb->layout()->activate();
    QApplication::processEvents();
    QTRY_VERIFY(markupExt->isVisible());
    const int pinnedW2 = markupExt->width();
    QVERIFY2(pinnedW2 >= kChevronMinPin && pinnedW2 <= kChevronMaxPin,
             "re-narrowed chevron must reappear at its pinned (style-relative) width");
    // Pin invariance across the overflow appear/disappear toggle: the
    // width does not depend on the overflow state.
    QCOMPARE(pinnedW2, pinnedW);

    // ---- R3 open-search overflow guard (ADR 0007, Option A, R3) ----
    // The window minimum-width floor must fold in the OPENED search bar's
    // footprint, not merely the collapsed search icon it measured at build
    // time. Otherwise, at the minimum width, a user who OPENS Find gets the
    // SearchBar (maxWidth 360, non-trivial minimumSizeHint) shoved into the
    // main toolbar's OWN extension chevron — the exact HIG "trailing items
    // stay visible at all sizes" violation R3 exists to prevent. Reproduce
    // it: at the window minimum width, open the search bar and assert it
    // sits fully inside the primary row and the main toolbar's own chevron
    // never appears.
    markupTb->hide();
    formTb->hide();
    mw->resize(mw->minimumWidth(), 750);
    QApplication::processEvents();

    QAction *findForOpen =
        findMenuAction(mw->menuBar(), QStringLiteral("&Edit"), QStringLiteral("&Find…"));
    QVERIFY2(findForOpen, "Edit → Find… action not found");
    findForOpen->trigger(); // showSearchBar(): hides the icon, expands the bar
    QApplication::processEvents();
    mainTb->layout()->activate();
    QApplication::processEvents();

    auto *searchBarWidget = mw->findChild<SearchBar *>();
    QVERIFY2(searchBarWidget, "SearchBar widget not found");
    QTRY_VERIFY2(searchBarWidget->isVisible(),
                 "opening Find must expand the SearchBar at the window minimum width, "
                 "not collapse it into the main toolbar's overflow chevron");
    const QRect barInMain(searchBarWidget->mapTo(mainTb, QPoint(0, 0)), searchBarWidget->size());
    QVERIFY2(mainTb->rect().contains(barInMain.center()),
             qPrintable(QStringLiteral("opened SearchBar center (%1,%2) must sit inside the "
                                       "main toolbar rect %3x%4 — not overflow into its own "
                                       "chevron — at the window minimum width")
                            .arg(barInMain.center().x())
                            .arg(barInMain.center().y())
                            .arg(mainTb->rect().width())
                            .arg(mainTb->rect().height())));

    QToolButton *mainOwnExt =
        mainTb->findChild<QToolButton *>(QStringLiteral("qt_toolbar_ext_button"));
    QVERIFY2(!mainOwnExt || !mainOwnExt->isVisible(),
             "the main toolbar's OWN overflow chevron must NOT appear when Find is opened "
             "at the window minimum width — the opened search must fit the primary row");
    grabTo(mw, QStringLiteral("xct070_open_search_min_width.png"));
}

// UAT-XCT-074 — Reserved toolbar positions survive the reported live
// transition: the markup toolbar is visible, then the user manually
// activates the form toolbar (View → Show Form Filling Toolbar, or its
// toolbar button). Two things the owner's dogfood report named as one
// bug are really two: (a) markup auto-hiding is a DELIBERATE mutual-
// exclusion (different workflows; see the comment at the connect() call
// in MainWindow's ctor) and (b) that hide must never smuggle a position
// change for the main toolbar along with it. This case pins (b): main's
// on-screen origin must be bit-identical before and after the
// transition, and markup/form visibility must reflect the deliberate
// exclusion.
void TestUatSearchAndMarkup::uat_xct_074_formActivationWhileMarkupVisibleKeepsMainAnchored() {
    QVERIFY(m_scratch.isValid());
    const QString pdfPath = writePdfWithKeyword(
        m_scratch.filePath(QStringLiteral("uat_xct_074.pdf")), QStringLiteral("fixture"));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    mw->resize(1100, 750);
    QApplication::processEvents();

    auto *mainTb = mw->findChild<QToolBar *>(QStringLiteral("MainToolbar"));
    auto *markupTb = mw->findChild<MarkupToolbar *>();
    auto *formTb = mw->findChild<FormToolbar *>();
    QVERIFY(mainTb && markupTb && formTb);

    formTb->hide();
    markupTb->show();
    QApplication::processEvents();
    const QPoint originMarkupVisible = mainTb->mapTo(mw, QPoint(0, 0));
    grabTo(mw, QStringLiteral("xct074_markup_visible.png"));

    QAction *formToggle = nullptr;
    for (QAction *a : mw->findChildren<QAction *>()) {
        if (a->objectName() == QStringLiteral("action.view.formToolbar")) {
            formToggle = a;
            break;
        }
    }
    QVERIFY2(formToggle, "action.view.formToolbar not found");
    formToggle->trigger(); // simulates the owner's manual "activate form toolbar"
    QApplication::processEvents();
    grabTo(mw, QStringLiteral("xct074_form_activated_markup_hidden.png"));

    // (a) The mutual exclusion is deliberate product behaviour, not an
    // accident — confirm it still fires: activating form hides markup.
    QVERIFY2(!markupTb->isVisible(),
             "activating the form toolbar must hide markup (deliberate mutual exclusion)");
    QVERIFY2(formTb->isVisible(), "the form toolbar must be visible after activation");

    // (b) The position invariant the owner actually reported broken:
    // the main toolbar's origin must not move by even one pixel across
    // that transition.
    const QPoint originFormActive = mainTb->mapTo(mw, QPoint(0, 0));
    QVERIFY2(originFormActive == originMarkupVisible,
             qPrintable(QStringLiteral("main toolbar moved when form was activated while markup "
                                       "was visible: markup-visible=(%1,%2) form-active=(%3,%4)")
                            .arg(originMarkupVisible.x())
                            .arg(originMarkupVisible.y())
                            .arg(originFormActive.x())
                            .arg(originFormActive.y())));
}

// UAT-XCT-075 — A persisted per-file windowState blob captured under an
// older toolbar arrangement must never resurrect that arrangement.
// QMainWindow::restoreState() restores the top toolbar area's order and
// row-break placement from the blob (matched by object name), which is
// a *different* channel than the explicit markupToolbarVisible bool
// RecentEntry also carries — restoreState() runs first and can silently
// overwrite the construction-time order ADR 0007 established, even
// though none of the three toolbars are user-movable and there is
// therefore never a legitimate reason for the blob to carry a different
// order. This reproduces exactly that: a blob saved under the PRE-ADR-
// 0007 order (markup, form+break, main with NO break — main a tenant on
// form's row) is planted as this document's persisted view-state, then
// the document is opened for real. Before the fix
// (MainWindow::reassertToolbarLayout()), opening this document left
// main looking fine until the form toolbar was shown — at which point
// main jumped ~184px right, exactly the ADR 0007 "~183px" bug,
// resurrected from disk on a build that already has the construction-
// time fix.
void TestUatSearchAndMarkup::uat_xct_075_staleWindowStateBlobDoesNotResurrectOldToolbarOrder() {
    QVERIFY(m_scratch.isValid());
    const QString pdfPath = writePdfWithKeyword(
        m_scratch.filePath(QStringLiteral("uat_xct_075.pdf")), QStringLiteral("fixture"));

    // Build a throwaway QMainWindow that mimics the PRE-fix construction
    // order exactly (see ADR 0007 "what ships today"): markup added,
    // form added with a break before it, main added LAST with NO break
    // before it (so main tenants on form's row).
    QMainWindow legacy;
    auto *legacyMain = new QToolBar(QStringLiteral("Main"), &legacy);
    legacyMain->setObjectName(QStringLiteral("MainToolbar"));
    auto *legacyMarkup = new QToolBar(QStringLiteral("Markup"), &legacy);
    legacyMarkup->setObjectName(QStringLiteral("MarkupToolbar"));
    auto *legacyForm = new QToolBar(QStringLiteral("Form"), &legacy);
    legacyForm->setObjectName(QStringLiteral("FormToolbar"));

    legacy.addToolBar(Qt::TopToolBarArea, legacyMarkup);
    legacy.addToolBar(Qt::TopToolBarArea, legacyForm);
    legacy.insertToolBarBreak(legacyForm);
    legacy.addToolBar(Qt::TopToolBarArea, legacyMain); // appended LAST, no break: tenant
    legacyMarkup->hide();
    legacyForm->hide();
    legacy.resize(1100, 750);

    const QByteArray staleBlob = legacy.saveState();
    QVERIFY(!staleBlob.isEmpty());

    RecentEntry state;
    state.currentPage = 0;
    state.zoomMode = ZoomMode::Custom;
    state.zoomFactor = 1.0;
    state.sidebarMode = SidebarMode::Hidden;
    state.markupToolbarVisible = false;
    state.windowState = staleBlob;

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->recentFiles().add(pdfPath);
    app->recentFiles().updateViewState(pdfPath, state);

    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    // Hold the window at a fixed, generous size (mirrors UAT-XCT-070/074)
    // so the comparison isolates toolbar order, not incidental overflow
    // from a narrow default window.
    mw->resize(1100, 750);
    QApplication::processEvents();

    auto *mainTb = mw->findChild<QToolBar *>(QStringLiteral("MainToolbar"));
    auto *formTb = mw->findChild<FormToolbar *>();
    QVERIFY(mainTb && formTb);

    const QPoint mainOriginAfterOpen = mainTb->mapTo(mw, QPoint(0, 0));
    QCOMPARE(mainOriginAfterOpen.x(), 0);
    grabTo(mw, QStringLiteral("xct075_stale_blob_after_open.png"));

    formTb->show();
    QApplication::processEvents();
    const QPoint mainOriginFormShown = mainTb->mapTo(mw, QPoint(0, 0));
    grabTo(mw, QStringLiteral("xct075_stale_blob_form_shown.png"));
    QVERIFY2(mainOriginFormShown == mainOriginAfterOpen,
             qPrintable(QStringLiteral("a stale windowState blob resurrected the pre-ADR-0007 "
                                       "toolbar order: main moved from (%1,%2) to (%3,%4) when "
                                       "the form toolbar was shown")
                            .arg(mainOriginAfterOpen.x())
                            .arg(mainOriginAfterOpen.y())
                            .arg(mainOriginFormShown.x())
                            .arg(mainOriginFormShown.y())));
}

// UAT-XCT-076 — General reserved-position sweep: toggling ANY one
// toolbar's visibility never moves another toolbar's actions. Where
// UAT-XCT-070/074/075 each pin one specific transition, this case is
// the general invariant: it records the on-screen geometry of a set of
// individual MAIN-toolbar action widgets (not just the toolbar's own
// origin — the actions inside it) at a hidden-contextual-bars baseline,
// then sweeps every show/hide combination of markup and form and
// re-asserts those exact widget geometries after each step. A toolbar
// that only *looks* anchored because its own top-left corner didn't
// move, while an individual action inside it silently reflowed (e.g.
// icon-only overflow state churn), would still fail this case.
void TestUatSearchAndMarkup::uat_xct_076_toggleAnyToolbarNeverMovesAnotherToolbarsActions() {
    QVERIFY(m_scratch.isValid());
    const QString pdfPath = writePdfWithKeyword(
        m_scratch.filePath(QStringLiteral("uat_xct_076.pdf")), QStringLiteral("fixture"));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    mw->resize(1100, 750);
    QApplication::processEvents();

    auto *mainTb = mw->findChild<QToolBar *>(QStringLiteral("MainToolbar"));
    auto *markupTb = mw->findChild<MarkupToolbar *>();
    auto *formTb = mw->findChild<FormToolbar *>();
    QVERIFY(mainTb && markupTb && formTb);

    markupTb->hide();
    formTb->hide();
    QApplication::processEvents();
    mainTb->layout()->activate();
    QApplication::processEvents();

    // Every realised main-toolbar action's widget — spanning leading
    // (zoom/rotate), middle (markup/form toggles), and trailing (near
    // search) — must keep its exact geometry across every toggle below.
    QList<QAction *> mainActions;
    for (QAction *a : mainTb->actions()) {
        if (mainTb->widgetForAction(a))
            mainActions.append(a);
    }
    QVERIFY2(mainActions.size() >= 3, "main toolbar must expose at least 3 realised actions");

    QHash<QAction *, QRect> baseline;
    for (QAction *a : mainActions) {
        QWidget *w = mainTb->widgetForAction(a);
        baseline.insert(a, QRect(w->mapTo(mw, QPoint(0, 0)), w->size()));
    }

    auto assertUnchanged = [&](const QString &stepLabel) {
        for (QAction *a : mainActions) {
            QWidget *w = mainTb->widgetForAction(a);
            QVERIFY2(w, qPrintable(QStringLiteral("action widget vanished after: %1").arg(stepLabel)));
            const QRect now(w->mapTo(mw, QPoint(0, 0)), w->size());
            const QRect expected = baseline.value(a);
            QVERIFY2(now == expected,
                     qPrintable(QStringLiteral("main-toolbar action moved after '%1': "
                                               "expected %2,%3 %4x%5, got %6,%7 %8x%9")
                                    .arg(stepLabel)
                                    .arg(expected.x())
                                    .arg(expected.y())
                                    .arg(expected.width())
                                    .arg(expected.height())
                                    .arg(now.x())
                                    .arg(now.y())
                                    .arg(now.width())
                                    .arg(now.height())));
        }
    };

    // Sweep: show markup alone; hide it; show form alone; hide it; show
    // markup then switch to form (mutual exclusion); back to baseline.
    markupTb->show();
    QApplication::processEvents();
    assertUnchanged(QStringLiteral("markup shown"));

    markupTb->hide();
    QApplication::processEvents();
    assertUnchanged(QStringLiteral("markup hidden again"));

    formTb->show();
    QApplication::processEvents();
    assertUnchanged(QStringLiteral("form shown"));

    formTb->hide();
    QApplication::processEvents();
    assertUnchanged(QStringLiteral("form hidden again"));

    markupTb->show();
    QApplication::processEvents();
    assertUnchanged(QStringLiteral("markup shown (2nd time)"));

    formTb->show(); // triggers the mutual-exclusion hide of markup
    QApplication::processEvents();
    QVERIFY2(!markupTb->isVisible(), "form activation must still hide markup (deliberate)");
    assertUnchanged(QStringLiteral("form activated while markup was visible"));

    markupTb->show(); // triggers the mutual-exclusion hide of form
    QApplication::processEvents();
    QVERIFY2(!formTb->isVisible(), "markup activation must still hide form (deliberate)");
    assertUnchanged(QStringLiteral("markup activated while form was visible"));
}

// UAT-XCT-077 — The PER-TYPE fallback restore path (a brand-new document
// of a type Trailer has seen before, with no per-file RecentEntry of its
// own) calls the same restoreState()/reassertToolbarLayout() pair as the
// per-file path in UAT-XCT-075, through a structurally parallel but
// distinct branch (DocumentTypeDefaults rather than RecentFiles). This
// is very likely the MORE common real-world trigger — it fires on every
// "first time opening this particular PDF" as long as the user has
// closed some other PDF before — so it gets its own direct test rather
// than relying on code-reading to infer the per-file case covers it.
void TestUatSearchAndMarkup::uat_xct_077_staleWindowStateBlobViaPerTypeDefaultAlsoReasserted() {
    QVERIFY(m_scratch.isValid());
    const QString pdfPath = writePdfWithKeyword(
        m_scratch.filePath(QStringLiteral("uat_xct_077.pdf")), QStringLiteral("fixture"));

    // Same throwaway pre-ADR-0007-order QMainWindow as UAT-XCT-075.
    QMainWindow legacy;
    auto *legacyMain = new QToolBar(QStringLiteral("Main"), &legacy);
    legacyMain->setObjectName(QStringLiteral("MainToolbar"));
    auto *legacyMarkup = new QToolBar(QStringLiteral("Markup"), &legacy);
    legacyMarkup->setObjectName(QStringLiteral("MarkupToolbar"));
    auto *legacyForm = new QToolBar(QStringLiteral("Form"), &legacy);
    legacyForm->setObjectName(QStringLiteral("FormToolbar"));

    legacy.addToolBar(Qt::TopToolBarArea, legacyMarkup);
    legacy.addToolBar(Qt::TopToolBarArea, legacyForm);
    legacy.insertToolBarBreak(legacyForm);
    legacy.addToolBar(Qt::TopToolBarArea, legacyMain);
    legacyMarkup->hide();
    legacyForm->hide();
    legacy.resize(1100, 750);

    const QByteArray staleBlob = legacy.saveState();
    QVERIFY(!staleBlob.isEmpty());

    DocumentTypeDefault def;
    def.zoomMode = ZoomMode::Custom;
    def.zoomFactor = 1.0;
    def.sidebarMode = SidebarMode::Hidden;
    def.markupToolbarVisible = false;
    def.windowState = staleBlob;

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    // No RecentEntry for this path at all — forces the per-type fallback
    // branch (entry.hasViewState() false) rather than the per-file one.
    app->documentTypeDefaults().setForType(DocumentType::Pdf, def);

    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    mw->resize(1100, 750);
    QApplication::processEvents();

    auto *mainTb = mw->findChild<QToolBar *>(QStringLiteral("MainToolbar"));
    auto *formTb = mw->findChild<FormToolbar *>();
    QVERIFY(mainTb && formTb);

    const QPoint mainOriginAfterOpen = mainTb->mapTo(mw, QPoint(0, 0));
    formTb->show();
    QApplication::processEvents();
    const QPoint mainOriginFormShown = mainTb->mapTo(mw, QPoint(0, 0));
    QVERIFY2(mainOriginFormShown == mainOriginAfterOpen,
             qPrintable(QStringLiteral("per-type restore path: a stale windowState blob "
                                       "resurrected the pre-ADR-0007 toolbar order: main moved "
                                       "from (%1,%2) to (%3,%4) when the form toolbar was shown")
                            .arg(mainOriginAfterOpen.x())
                            .arg(mainOriginAfterOpen.y())
                            .arg(mainOriginFormShown.x())
                            .arg(mainOriginFormShown.y())));
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

    // See tests/test_image_scale.cpp's main() for why this is needed on
    // macOS: QSettings(org, app) defaults to NativeFormat there, which
    // ignores the HOME sandboxing above.
    QSettings::setDefaultFormat(QSettings::IniFormat);
    Application app(argc, argv);
    TestUatSearchAndMarkup tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_uat_search_and_markup.moc"
