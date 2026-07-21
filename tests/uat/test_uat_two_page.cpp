// UAT harness — Two-page (facing) layout, PR1.
//
// Drives Application + MainWindow in-process under QT_QPA_PLATFORM=offscreen.
// Each slot maps to a case in docs/uat/02-viewer.md (UAT-VWR-070..073) and
// pins one clause of the accepted decision record
// docs/decision-records/2026-07-21-two-page-layout.md:
//
//   UAT-VWR-070  toggle enablement (G3): enabled only for multi-page PDFs,
//                disabled-with-tooltip for images and single-page PDFs.
//   UAT-VWR-071  cover-alone pairing + side-by-side geometry (clauses 1,2).
//   UAT-VWR-072  per-page Actual Size + truthful zoom readout (clause 3),
//                validated at the dpr matrix {1, 1.5, 2} (clause 4).
//   UAT-VWR-073  honest degradation (D2-A, G3): markup / search disabled-
//                with-tooltip in Two-Pages mode, re-enabled on leaving it.
//   UAT-VWR-074  curated G2 evidence: Single/Two-Pages before/after pair on the
//                same doc + the disabled-toggle+tooltip reconstruction.
//
// Lookups go through stable objectNames on MainWindow's actions and on the
// TwoPageView widget, so the harness survives label / IA renames. The binary
// is labelled `uat` in CTest and registered across the dpr matrix.

#include "app/Application.h"
#include "document/IDocument.h"
#include "document/SpreadLayout.h"
#include "ui/DocumentView.h"
#include "ui/MainWindow.h"
#include "ui/TwoPageView.h"

#include <QAbstractScrollArea>
#include <QAction>
#include <QDir>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QPageSize>
#include <QPainter>
#include <QPdfView>
#include <QPdfWriter>
#include <QRect>
#include <QScreen>
#include <QScrollBar>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QVector>
#include <QVBoxLayout>
#include <QWidget>
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

// Multi-page A4 PDF with a page-numbered line per page, so each page has real
// content and a real point size for the spread canvas to lay out.
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

QString writeSampleImage(const QString &path) {
    QImage img(200, 300, QImage::Format_ARGB32);
    img.fill(Qt::white);
    img.save(path, "PNG");
    return path;
}

// A BOOK-like multi-page A4 PDF for the G2 evidence shots: page 1 is a distinct
// cover (title, no number), and every later page carries one large, centred page
// number. Rendered big enough to survive the window-grab downscale, so a
// reviewer can literally read the cover-alone-then-facing rhythm off the
// after-shot: page 1 sits alone, then 2·3, then 4·5 side by side.
QString writeBookPdf(const QString &path, int pages) {
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setResolution(150); // predictable device-pixel dimensions
    QPainter p(&writer);
    const QRect page(0, 0, writer.width(), writer.height());
    for (int i = 0; i < pages; ++i) {
        QFont f = p.font();
        f.setBold(true);
        if (i == 0) {
            // Cover: a title block, visually unmistakable versus a numbered page.
            f.setPointSize(56);
            p.setFont(f);
            p.drawText(page, Qt::AlignCenter | Qt::TextWordWrap,
                       QStringLiteral("THE\nTRAILER\nBOOK\n\n· cover ·"));
        } else {
            // A single huge page number fills the page.
            f.setPointSize(320);
            p.setFont(f);
            p.drawText(page, Qt::AlignCenter, QString::number(i + 1));
        }
        if (i < pages - 1)
            writer.newPage();
    }
    p.end();
    return path;
}

QAction *findAction(MainWindow *mw, const QString &objectName) {
    return mw ? mw->findChild<QAction *>(objectName) : nullptr;
}

// The devicePixelRatio this run was launched under. The CMake dpr matrix
// (tests/uat/CMakeLists.txt, trailer_register_uat_dpr_matrix) sets
// QT_SCALE_FACTOR per process; Qt reads it before QGuiApplication and stamps
// it on every QScreen. Defaults to 1.0 when unset (a plain `ctest` run). The
// offscreen platform reports dpr = 1 by default, so a value > 1 here is the
// proof the injection actually took — mirrors requestedDpr() in
// test_uat_thumbnail_sidebar.cpp.
double requestedDpr() {
    bool ok = false;
    const double v = qEnvironmentVariable("QT_SCALE_FACTOR").toDouble(&ok);
    return (ok && v > 0.0) ? v : 1.0;
}

// The dpr Qt actually realized on the primary screen — what the widget tree
// and the TwoPageView render path see.
double screenDpr() {
    auto *s = QGuiApplication::primaryScreen();
    return s ? s->devicePixelRatio() : 1.0;
}

// Parse the integer percent shown in the status-bar zoom indicator
// (MainWindow's `zoomIndicator` QLabel, text like "150%"). Returns -1 if the
// label is missing / not visible / unparseable so the caller can fail loudly.
int displayedZoomPercent(MainWindow *mw) {
    auto *label = mw ? mw->findChild<QLabel *>(QStringLiteral("zoomIndicator")) : nullptr;
    if (!label || !label->isVisible())
        return -1;
    QString t = label->text().trimmed();
    t.remove(QLatin1Char('%'));
    bool ok = false;
    const int pct = t.toInt(&ok);
    return ok ? pct : -1;
}

// Measure the ACTUAL painted width, in LOGICAL pixels, of the lone cover page
// (page 1, top-of-canvas at scroll 0) in a TwoPageView, by grabbing the
// viewport and scanning for the white page rectangle on a near-top row. This
// reads what is really drawn on screen — independent of any stored zoom factor
// — so dividing it by the native point width yields the TRUE render scale the
// user is looking at. Returns < 0 if the page could not be measured (not found,
// or clipped at a viewport edge, which would make the width untrustworthy).
double measuredCoverWidthLogical(TwoPageView *view) {
    if (!view)
        return -1.0;
    view->verticalScrollBar()->setValue(0);
    QApplication::processEvents();
    const QImage img = view->viewport()->grab().toImage();
    if (img.isNull() || img.width() < 8 || img.height() < 8)
        return -1.0;
    const double dpr = img.devicePixelRatio() > 0.0 ? img.devicePixelRatio() : 1.0;
    // The cover's top edge sits at the outer canvas margin (~20 logical px);
    // scan a row a few logical px below it, well inside the top white margin and
    // far above the second spread (which is a whole page height lower). Even if
    // this row crossed the centred page label, the page's left/right edges are
    // white margins, so the first/last near-white pixel still marks the page.
    const int scanY = static_cast<int>(std::lround(28.0 * dpr));
    if (scanY < 0 || scanY >= img.height())
        return -1.0;
    auto nearWhite = [](QRgb c) {
        return qRed(c) > 220 && qGreen(c) > 220 && qBlue(c) > 220;
    };
    int left = -1, right = -1;
    for (int x = 0; x < img.width(); ++x) {
        if (nearWhite(img.pixel(x, scanY))) {
            if (left < 0)
                left = x;
            right = x;
        }
    }
    if (left < 0)
        return -1.0; // no page found on this row
    // Reject a clipped measurement: if the white run touches either viewport
    // edge the page overflows and its true width is unknowable here.
    if (left == 0 || right == img.width() - 1)
        return -1.0;
    return double(right - left + 1) / dpr;
}

// Open `path` in a FRESH window: close any existing MainWindows first (the
// default is window-per-file, so leaving earlier windows open makes
// currentMainWindow() ambiguous), then open and return the sole window.
MainWindow *openFreshWindow(Application *app, const QString &path) {
    for (auto *w : QApplication::topLevelWidgets()) {
        if (qobject_cast<MainWindow *>(w))
            w->close();
    }
    QApplication::processEvents();
    app->openFiles({path});
    QApplication::processEvents();
    MainWindow *mw = currentMainWindow();
    if (mw) {
        mw->show();
        QApplication::processEvents();
    }
    return mw;
}

} // namespace

class TestUatTwoPage : public QObject {
    Q_OBJECT
  private slots:
    void init();

    void uat_vwr_070_toggleEnablementByDocType();
    void uat_vwr_071_coverAlonePairingGeometry();
    void uat_vwr_072_actualSizeTruthfulZoom();
    void uat_vwr_073_honestDegradationTooltips();
    void uat_vwr_074_g2Evidence();
    void uat_vwr_075_navigationScrollsSpread();
    void uat_vwr_076_relayoutOnPageDelete();
    void uat_vwr_077_livePageTrackingOnFreeScroll();
    void uat_vwr_078_spreadAwareFitNoOverflow();
    void uat_vwr_079_zoomReadoutMatchesRenderScale();
    void uat_vwr_080_dprBackingResolution();
    void uat_vwr_081_nextPageWalksAllSpreads();
    void uat_vwr_082_oversizedSpreadTracking();

  private:
    QTemporaryDir m_scratch;
};

void TestUatTwoPage::init() {
    for (auto *w : QApplication::topLevelWidgets()) {
        if (qobject_cast<MainWindow *>(w))
            w->close();
    }
    QApplication::processEvents();
}

// UAT-VWR-070 — the "Two Pages" toggle is enabled only for multi-page PDFs;
// disabled-with-tooltip for images and single-page PDFs (G3, no lying controls).
void TestUatTwoPage::uat_vwr_070_toggleEnablementByDocType() {
    QVERIFY(m_scratch.isValid());
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);

    // Multi-page PDF → enabled.
    const QString multi =
        writeSamplePdf(m_scratch.filePath(QStringLiteral("uat_vwr_070_multi.pdf")), 4);
    MainWindow *mw = openFreshWindow(app, multi);
    QVERIFY(mw);
    QAction *twoPages = findAction(mw, QStringLiteral("action.view.twoPages"));
    QVERIFY2(twoPages, "Two Pages action must exist with objectName action.view.twoPages");
    QVERIFY2(twoPages->isEnabled(), "Two Pages must be enabled for a multi-page PDF");

    // Single-page PDF → disabled + explanatory tooltip.
    const QString single =
        writeSamplePdf(m_scratch.filePath(QStringLiteral("uat_vwr_070_single.pdf")), 1);
    mw = openFreshWindow(app, single);
    QVERIFY(mw);
    twoPages = findAction(mw, QStringLiteral("action.view.twoPages"));
    QVERIFY(twoPages);
    QVERIFY2(!twoPages->isEnabled(), "Two Pages must be disabled for a single-page PDF");
    QVERIFY2(twoPages->toolTip().contains(QStringLiteral("one page"), Qt::CaseInsensitive),
             qPrintable(QStringLiteral("single-page tooltip should explain why; got: '%1'")
                            .arg(twoPages->toolTip())));

    // Image → disabled + explanatory tooltip.
    const QString image = writeSampleImage(m_scratch.filePath(QStringLiteral("uat_vwr_070.png")));
    mw = openFreshWindow(app, image);
    QVERIFY(mw);
    twoPages = findAction(mw, QStringLiteral("action.view.twoPages"));
    QVERIFY(twoPages);
    QVERIFY2(!twoPages->isEnabled(), "Two Pages must be disabled for an image");
    QVERIFY2(twoPages->toolTip().contains(QStringLiteral("images"), Qt::CaseInsensitive),
             qPrintable(QStringLiteral("image tooltip should explain why; got: '%1'")
                            .arg(twoPages->toolTip())));
}

// UAT-VWR-071 — switching to Two Pages shows the custom TwoPageView, which
// lays pages out side by side using the cover-alone pairing (page 1 alone,
// then (2,3),(4,5)…). The canvas is wider than one page (a spread) and tall
// enough to stack multiple spreads (continuous scroll).
void TestUatTwoPage::uat_vwr_071_coverAlonePairingGeometry() {
    QVERIFY(m_scratch.isValid());
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    const QString pdf =
        writeSamplePdf(m_scratch.filePath(QStringLiteral("uat_vwr_071.pdf")), 5);
    app->openFiles({pdf});
    QApplication::processEvents();
    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    mw->resize(1200, 800);
    mw->show();
    QApplication::processEvents();

    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    IDocument *doc = dv->currentDocument();
    QVERIFY(doc);

    doc->setViewMode(ViewMode::TwoPages);
    QApplication::processEvents();
    QCOMPARE(doc->viewMode(), ViewMode::TwoPages);

    auto *twoPage = mw->findChild<TwoPageView *>(QStringLiteral("view.twoPage"));
    QVERIFY2(twoPage, "TwoPageView (objectName view.twoPage) must exist in Two-Pages mode");
    QVERIFY2(twoPage->isVisible(), "TwoPageView must be the visible surface in Two-Pages mode");

    // The view must actually have built the cover-alone pairing from the shared
    // helper: a 5-page book → spreads [1],[2,3],[4,5] (page 1 alone, then facing
    // pairs). Assert the live layout equals spreadsFor(5, coverAlone=true) — the
    // integration check that the view consumes the pairing correctly (the pure
    // pairing itself is unit-tested in tests/test_spread_layout.cpp).
    const std::vector<Spread> expected = spreadsFor(5, /*coverAlone=*/true);
    QCOMPARE(int(twoPage->spreads().size()), int(expected.size()));
    for (size_t i = 0; i < expected.size(); ++i) {
        QCOMPARE(twoPage->spreads()[i].left, expected[i].left);
        QCOMPARE(twoPage->spreads()[i].right, expected[i].right);
    }
    // 3 stacked spreads at fit zoom exceed the viewport, so there is a real
    // vertical scroll range to move through (continuous spread scroll).
    QVERIFY2(twoPage->verticalScrollBar()->maximum() > 0,
             "TwoPageView must expose a non-empty vertical scroll range for 3 stacked spreads");
}

// UAT-VWR-072 — "Actual Size" renders each page at 1 PDF point -> 1 logical
// pixel (not spread-fits-window), and the zoom-% readout reports the true
// per-page zoom, identical in meaning to Single/Continuous. Runs across the
// dpr matrix so the pts x zoom x dpr device-pixel path is exercised at HiDPI.
void TestUatTwoPage::uat_vwr_072_actualSizeTruthfulZoom() {
    QVERIFY(m_scratch.isValid());
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    const QString pdf =
        writeSamplePdf(m_scratch.filePath(QStringLiteral("uat_vwr_072.pdf")), 4);
    app->openFiles({pdf});
    QApplication::processEvents();
    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    mw->resize(1200, 800);
    mw->show();
    QApplication::processEvents();

    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    IDocument *doc = dv->currentDocument();
    QVERIFY(doc);

    doc->setViewMode(ViewMode::TwoPages);
    QApplication::processEvents();
    doc->zoomActual();
    QApplication::processEvents();

    // Actual Size == per-page zoom factor 1.0, and the same 100% means the
    // same physical page size in every mode.
    QVERIFY2(qFuzzyCompare(doc->zoomFactor(), 1.0),
             qPrintable(QStringLiteral("Actual Size must be per-page zoom 1.0; got %1")
                            .arg(doc->zoomFactor())));

    // The custom surface must actually render at that same factor — the zoom is
    // SHARED with QPdfView so the readout is truthful, not a separate scale. A
    // desync between doc->zoomFactor() (read from QPdfView) and the TwoPageView's
    // render zoom would silently make the readout lie; assert they agree.
    auto *twoPage = mw->findChild<TwoPageView *>(QStringLiteral("view.twoPage"));
    QVERIFY(twoPage);
    QVERIFY2(qFuzzyCompare(twoPage->zoomFactor(), doc->zoomFactor()),
             qPrintable(QStringLiteral("TwoPageView render zoom %1 must match the readout %2")
                            .arg(twoPage->zoomFactor())
                            .arg(doc->zoomFactor())));
}

// UAT-VWR-073 — honest degradation (D2-A, G3): in Two-Pages mode markup and
// search are disabled-with-tooltip that says where to go, and both re-enable
// when the user returns to Continuous.
void TestUatTwoPage::uat_vwr_073_honestDegradationTooltips() {
    QVERIFY(m_scratch.isValid());
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    const QString pdf =
        writeSamplePdf(m_scratch.filePath(QStringLiteral("uat_vwr_073.pdf")), 4);
    app->openFiles({pdf});
    QApplication::processEvents();
    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    mw->show();
    QApplication::processEvents();

    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    IDocument *doc = dv->currentDocument();
    QVERIFY(doc);

    QAction *markup = findAction(mw, QStringLiteral("action.view.markupToolbar"));
    QAction *find = findAction(mw, QStringLiteral("action.edit.find"));
    QAction *twoPages = findAction(mw, QStringLiteral("action.view.twoPages"));
    QAction *continuous = findAction(mw, QStringLiteral("action.view.continuous"));
    QAction *formToolbar = findAction(mw, QStringLiteral("action.view.formToolbar"));
    QVERIFY2(markup, "markup toolbar toggle must exist with objectName action.view.markupToolbar");
    QVERIFY2(find, "Find action must exist with objectName action.edit.find");
    QVERIFY2(twoPages && continuous, "view-mode actions must exist");

    // The read-only badge is the PRIMARY degradation signal — a compact
    // in-context pill in the status bar (minimal-UI guideline, #116), not a
    // full-width banner. Look it up by stable objectName; it is hidden outside
    // Two-Pages mode, and the full sentence lives in its tooltip.
    auto *badge = mw->findChild<QLabel *>(QStringLiteral("twoPageReadOnlyBadge"));
    QVERIFY2(badge, "Two-Pages read-only badge (objectName twoPageReadOnlyBadge) must exist");
    QVERIFY2(badge->toolTip().contains(
                 QStringLiteral("Two Pages is a read-only view — switch to Single or "
                                "Continuous to edit")),
             qPrintable(QStringLiteral("badge tooltip must carry the full read-only "
                                       "sentence; got: '%1'")
                            .arg(badge->toolTip())));

    // Switch modes exactly as the user does — via the View-menu actions — so
    // the degradation runs through the real command path, not a back-door
    // setViewMode call.
    continuous->trigger();
    QApplication::processEvents();
    QVERIFY2(find->isEnabled(), "Find is available in Continuous mode");
    QVERIFY2(!badge->isVisible(), "read-only badge must be hidden outside Two-Pages mode");

    // Two-Pages: markup + search disabled-with-tooltip pointing back to the
    // working modes.
    QVERIFY2(twoPages->isEnabled(), "Two Pages must be enabled for this multi-page PDF");
    twoPages->trigger();
    QApplication::processEvents();
    QCOMPARE(doc->viewMode(), ViewMode::TwoPages);

    // Primary signal: the compact read-only badge is now visible; its tooltip
    // (asserted above) carries the full "switch to edit" sentence.
    QVERIFY2(badge->isVisible(), "read-only badge must be visible in Two-Pages mode");

    QVERIFY2(!markup->isEnabled(), "markup must be disabled in Two-Pages mode");
    QVERIFY2(markup->toolTip().contains(QStringLiteral("Switch to"), Qt::CaseInsensitive),
             qPrintable(QStringLiteral("markup degrade tooltip should point back; got: '%1'")
                            .arg(markup->toolTip())));
    QVERIFY2(!find->isEnabled(), "search must be disabled in Two-Pages mode");
    QVERIFY2(find->toolTip().contains(QStringLiteral("Switch to"), Qt::CaseInsensitive),
             qPrintable(QStringLiteral("search degrade tooltip should point back; got: '%1'")
                            .arg(find->toolTip())));

    // G3 floor: the form-toolbar degrade tooltip must name FILLING FORMS — not
    // borrow the "mark up or search" copy (the fixed bug). It stays disabled.
    if (formToolbar) {
        QVERIFY2(!formToolbar->isEnabled(), "form toolbar must be disabled in Two-Pages mode");
        QVERIFY2(formToolbar->toolTip().contains(QStringLiteral("form"), Qt::CaseInsensitive),
                 qPrintable(QStringLiteral("form degrade tooltip must reference forms; got: '%1'")
                                .arg(formToolbar->toolTip())));
        QVERIFY2(!formToolbar->toolTip().contains(QStringLiteral("mark up or search")),
                 qPrintable(QStringLiteral("form tooltip must not say 'mark up or search'; got: '%1'")
                                .arg(formToolbar->toolTip())));
    }

    // Leaving Two-Pages restores both and hides the badge.
    continuous->trigger();
    QApplication::processEvents();
    QVERIFY2(find->isEnabled(), "Find re-enables when leaving Two-Pages mode");
    QVERIFY2(markup->isEnabled(), "markup re-enables when leaving Two-Pages mode");
    QVERIFY2(!badge->isVisible(), "read-only badge must hide again on leaving Two-Pages mode");
}

// UAT-VWR-074 — curated G2 evidence, HONESTLY re-shot. Grabs the SAME book-like
// document in Single mode (before) and Two-Pages mode (after) — the required
// before/after pair — plus the read-only disabled-toggle panel and a zoom shot
// whose visible readout matches its true render scale. The after-shot shows the
// compact read-only badge (the minimal-UI signal per #116, not a full-width
// banner) and the cover-alone-then-facing rhythm (page 1 alone, then 2·3, 4·5).
// Every zoom grab is taken AFTER the readout has refreshed via the
// real zoom action, so the shot can never re-introduce the stale-readout bug
// (a "100%" caption over a page painted at ~50%) that this re-shoot exists to
// correct — uat_vwr_079 guards the invariant independently. Writes PNGs only
// when TRAILER_TWO_PAGE_EVIDENCE_DIR is set; otherwise it still exercises the
// same render path as a plain assertion so the slot is a real test in CI.
void TestUatTwoPage::uat_vwr_074_g2Evidence() {
    QVERIFY(m_scratch.isValid());
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);

    // A real 6-page book: cover [1], then facing spreads [2,3],[4,5], then [6].
    const QString pdf =
        writeBookPdf(m_scratch.filePath(QStringLiteral("uat_vwr_074_book.pdf")), 6);
    MainWindow *mw = openFreshWindow(app, pdf);
    QVERIFY(mw);
    mw->resize(1100, 900);
    QApplication::processEvents();

    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    IDocument *doc = dv->currentDocument();
    QVERIFY(doc);

    auto *single = findAction(mw, QStringLiteral("action.view.singlePage"));
    auto *twoPages = findAction(mw, QStringLiteral("action.view.twoPages"));
    auto *actual = findAction(mw, QStringLiteral("action.view.actualSize"));
    auto *zoomOut = findAction(mw, QStringLiteral("action.view.zoomOut"));
    QVERIFY(single && twoPages && actual && zoomOut);

    const QByteArray dir = qgetenv("TRAILER_TWO_PAGE_EVIDENCE_DIR");
    auto save = [&](const QImage &img, const QString &name) {
        if (dir.isEmpty())
            return;
        QDir().mkpath(QString::fromLocal8Bit(dir));
        const QString path = QString::fromLocal8Bit(dir) + QDir::separator() + name;
        QVERIFY2(!img.isNull() && img.save(path, "PNG"),
                 qPrintable(QStringLiteral("failed to save %1").arg(path)));
    };

    // BEFORE: Single mode on the book — the cover (page 1) fills the surface.
    single->trigger();
    QApplication::processEvents();
    const QPixmap before = mw->grab();
    QVERIFY(!before.isNull());
    save(before.toImage(), QStringLiteral("tp-before.png"));

    // AFTER: Two-Pages mode on the SAME book, TOP-ANCHORED. Zoom out until at
    // least the lone cover (page 1) and the first facing pair (2·3) — ideally
    // 4·5 too — are all visible at scroll 0, so the cover-alone-then-facing
    // rhythm is unmistakable, and the read-only banner is captured in-frame.
    QVERIFY(twoPages->isEnabled());
    twoPages->trigger();
    QApplication::processEvents();
    auto *twoPageView = mw->findChild<TwoPageView *>(QStringLiteral("view.twoPage"));
    QVERIFY(twoPageView);
    // Step out via the REAL zoom-out action (not doc->zoomOut(), which would
    // leave the status-bar readout stale — the very bug this re-shoot corrects)
    // until the lone cover AND the first facing pair both fit at scroll 0, so the
    // zoom badge in-frame stays truthful about the scale the spread is painted at.
    actual->trigger();
    QApplication::processEvents();
    for (int i = 0; i < 5; ++i) {
        zoomOut->trigger();
        QApplication::processEvents();
    }
    twoPageView->verticalScrollBar()->setValue(0);
    QApplication::processEvents();
    auto *badge = mw->findChild<QLabel *>(QStringLiteral("twoPageReadOnlyBadge"));
    QVERIFY2(badge && badge->isVisible(),
             "read-only badge must be visible in the after-shot");
    const QPixmap after = mw->grab();
    QVERIFY(!after.isNull());
    save(after.toImage(), QStringLiteral("tp-after.png"));

    // ZOOM shot: Actual Size (100%). Trigger the REAL action, let the readout
    // refresh, THEN grab — so the caption ("100%") and the painted render scale
    // agree. This is the honest counterpart to the withdrawn stale "100%" shot.
    actual->trigger();
    QApplication::processEvents();
    twoPageView->verticalScrollBar()->setValue(0);
    QApplication::processEvents();
    const QPixmap zoom100 = mw->grab();
    QVERIFY(!zoom100.isNull());
    save(zoom100.toImage(), QStringLiteral("tp-zoom100.png"));

    // DISABLED TOGGLE + tooltip (G3). Read the tooltip text LIVE from the real
    // greyed m_twoPagesAction (opening a single-page PDF, then an image) rather
    // than retyping the strings, so the evidence cannot drift from the code. The
    // panel is a reconstruction because a hover-tooltip cannot be captured in an
    // offscreen static grab — the accepted fallback per the G2 capture-method
    // ruling — but every string in it is the actual QAction::toolTip().
    const QString singlePdf =
        writeSamplePdf(m_scratch.filePath(QStringLiteral("uat_vwr_074_single.pdf")), 1);
    MainWindow *sw = openFreshWindow(app, singlePdf);
    QVERIFY(sw);
    QAction *swToggle = findAction(sw, QStringLiteral("action.view.twoPages"));
    QVERIFY(swToggle && !swToggle->isEnabled());
    const QString singleTip = swToggle->toolTip();

    const QString imgPath =
        writeSampleImage(m_scratch.filePath(QStringLiteral("uat_vwr_074.png")));
    MainWindow *iw = openFreshWindow(app, imgPath);
    QVERIFY(iw);
    QAction *iwToggle = findAction(iw, QStringLiteral("action.view.twoPages"));
    QVERIFY(iwToggle && !iwToggle->isEnabled());
    const QString imageTip = iwToggle->toolTip();

    QWidget panel;
    panel.setObjectName(QStringLiteral("evidence.toggleDisabled"));
    panel.setStyleSheet(QStringLiteral("background:#f4f4f4;"));
    auto *lay = new QVBoxLayout(&panel);
    // The read-only signal is now a COMPACT status-bar badge (a lock pill), not a
    // full-width banner (minimal-UI guideline, #116). Show the pill left-aligned
    // at its natural width with the full sentence as its tooltip caption below,
    // mirroring the real m_readOnlyBadge style.
    auto *badgeRow = new QWidget(&panel);
    auto *badgeLay = new QHBoxLayout(badgeRow);
    badgeLay->setContentsMargins(0, 0, 0, 0);
    auto *badgeLabel = new QLabel(QStringLiteral("\U0001F512 Read-only"), badgeRow);
    badgeLabel->setStyleSheet(QStringLiteral(
        "background:#fff4d6; border:1px solid #e6c86a; border-radius:4px; "
        "color:#5a4a12; font-size:13px; padding:1px 6px;"));
    badgeLay->addWidget(badgeLabel);
    badgeLay->addStretch(1);
    lay->addWidget(badgeRow);
    auto *badgeTip = new QLabel(
        QStringLiteral("Badge tooltip: “Two Pages is a read-only view — switch to "
                       "Single or Continuous to edit”"),
        &panel);
    badgeTip->setWordWrap(true);
    badgeTip->setStyleSheet(QStringLiteral("color:#555; font-size:12px; padding:2px 2px 6px;"));
    lay->addWidget(badgeTip);
    auto *row = new QLabel(QStringLiteral("View ▸ Two Pages   (disabled)"), &panel);
    row->setEnabled(false); // greyed, as the disabled menu entry renders
    row->setStyleSheet(QStringLiteral("font-size:15px; padding:6px 10px;"));
    lay->addWidget(row);
    auto *tipSingle =
        new QLabel(QStringLiteral("Tooltip (single-page PDF): “%1”").arg(singleTip), &panel);
    auto *tipImage =
        new QLabel(QStringLiteral("Tooltip (image): “%1”").arg(imageTip), &panel);
    for (QLabel *t : {tipSingle, tipImage}) {
        t->setFrameShape(QFrame::StyledPanel);
        t->setStyleSheet(QStringLiteral("color:#333; background:#fffbe6; padding:6px 10px;"));
        lay->addWidget(t);
    }
    panel.resize(560, 200);
    panel.show();
    QApplication::processEvents();
    const QPixmap disabled = panel.grab();
    QVERIFY(!disabled.isNull());
    save(disabled.toImage(), QStringLiteral("tp-disabled.png"));
}

// UAT-VWR-075 — navigation stays live in Two-Pages mode. Previous/Next Page (and
// thumbnail-click) route through IDocument::goToPage, which in two-up mode must
// scroll the TwoPageView to the target page's spread — otherwise it would move
// only the hidden QPdfView and read as an inert control (G3). Regression guard
// for the correctness-review finding.
void TestUatTwoPage::uat_vwr_075_navigationScrollsSpread() {
    QVERIFY(m_scratch.isValid());
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    const QString pdf =
        writeSamplePdf(m_scratch.filePath(QStringLiteral("uat_vwr_075.pdf")), 6);
    MainWindow *mw = openFreshWindow(app, pdf);
    QVERIFY(mw);
    mw->resize(1000, 720);
    QApplication::processEvents();

    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    IDocument *doc = dv->currentDocument();
    QVERIFY(doc);
    doc->setViewMode(ViewMode::TwoPages);
    QApplication::processEvents();

    auto *twoPage = mw->findChild<TwoPageView *>(QStringLiteral("view.twoPage"));
    QVERIFY(twoPage);
    QScrollBar *vbar = twoPage->verticalScrollBar();
    QVERIFY2(vbar->maximum() > 0, "need a real scroll range to observe navigation");
    vbar->setValue(0);
    QApplication::processEvents();
    QCOMPARE(vbar->value(), 0);

    // Jump to the last page: the visible spread canvas must scroll down.
    doc->goToPage(doc->pageCount() - 1);
    QApplication::processEvents();
    QVERIFY2(vbar->value() > 0,
             "Navigating to a later page must scroll the TwoPageView spread canvas");
}

// UAT-VWR-076 — the spread layout follows document mutations. Deleting a page in
// Two-Pages mode must re-lay-out the spreads (fewer pages → fewer spreads),
// rather than keep drawing the pre-edit layout. Regression guard for the
// stale-spreads correctness-review finding.
void TestUatTwoPage::uat_vwr_076_relayoutOnPageDelete() {
    QVERIFY(m_scratch.isValid());
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    const QString pdf =
        writeSamplePdf(m_scratch.filePath(QStringLiteral("uat_vwr_076.pdf")), 6);
    MainWindow *mw = openFreshWindow(app, pdf);
    QVERIFY(mw);
    mw->resize(1000, 720);
    QApplication::processEvents();

    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    IDocument *doc = dv->currentDocument();
    QVERIFY(doc);
    doc->setViewMode(ViewMode::TwoPages);
    QApplication::processEvents();

    auto *twoPage = mw->findChild<TwoPageView *>(QStringLiteral("view.twoPage"));
    QVERIFY(twoPage);
    // 6 pages, cover-alone: [1],[2,3],[4,5],[6] → 4 spreads.
    QCOMPARE(int(twoPage->spreads().size()), 4);

    // Delete two pages → 4 pages: [1],[2,3],[4] → 3 spreads. The view must
    // reflect the new page graph without a manual resize/zoom nudge.
    doc->deletePages({4, 5});
    QApplication::processEvents();
    QCOMPARE(doc->pageCount(), 4);
    QCOMPARE(int(twoPage->spreads().size()), 3);
}

// UAT-VWR-077 — the current-page indicator tracks free-scroll in Two-Pages mode
// instead of freezing on the first spread. Free-scrolling the TwoPageView must
// advance doc->currentPage() (which the sidebar highlight polls) and emit
// TwoPageView::currentPageChanged. Regression guard for the Preview-parity miss
// where the indicator froze because the hidden QPdfView navigator can't observe
// the custom surface's scrolling.
void TestUatTwoPage::uat_vwr_077_livePageTrackingOnFreeScroll() {
    QVERIFY(m_scratch.isValid());
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    const QString pdf =
        writeSamplePdf(m_scratch.filePath(QStringLiteral("uat_vwr_077.pdf")), 8);
    MainWindow *mw = openFreshWindow(app, pdf);
    QVERIFY(mw);
    mw->resize(1000, 720);
    QApplication::processEvents();

    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    IDocument *doc = dv->currentDocument();
    QVERIFY(doc);
    doc->setViewMode(ViewMode::TwoPages);
    QApplication::processEvents();

    auto *twoPage = mw->findChild<TwoPageView *>(QStringLiteral("view.twoPage"));
    QVERIFY(twoPage);
    QScrollBar *vbar = twoPage->verticalScrollBar();
    QVERIFY2(vbar->maximum() > 0, "need a real scroll range to observe live tracking");

    // At the top, the current page is the cover (page 0).
    vbar->setValue(0);
    QApplication::processEvents();
    QCOMPARE(doc->currentPage(), 0);

    // The view must emit currentPageChanged as we free-scroll — and the tracked
    // current page must advance past the first spread, not stay frozen at 0.
    QSignalSpy spy(twoPage, &TwoPageView::currentPageChanged);
    vbar->setValue(vbar->maximum());
    QApplication::processEvents();

    QVERIFY2(spy.count() > 0,
             "free-scrolling must emit TwoPageView::currentPageChanged");
    QVERIFY2(doc->currentPage() > 0,
             qPrintable(QStringLiteral("current page must advance on free-scroll, not freeze at "
                                       "the first spread; got %1")
                            .arg(doc->currentPage())));
}

// UAT-VWR-078 — Fit-Width / Fit-Page are spread-aware in Two-Pages mode: a
// facing spread (page1 + gutter + page2) fits the viewport WIDTH without
// horizontal overflow. QPdfView's per-page fit would leave a whole page hanging
// off the edge, so the fit must route through the custom surface. Regression
// guard for the overflowing-spread fit finding.
void TestUatTwoPage::uat_vwr_078_spreadAwareFitNoOverflow() {
    QVERIFY(m_scratch.isValid());
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    const QString pdf =
        writeSamplePdf(m_scratch.filePath(QStringLiteral("uat_vwr_078.pdf")), 6);
    MainWindow *mw = openFreshWindow(app, pdf);
    QVERIFY(mw);
    mw->resize(1000, 720);
    QApplication::processEvents();

    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    IDocument *doc = dv->currentDocument();
    QVERIFY(doc);
    doc->setViewMode(ViewMode::TwoPages);
    QApplication::processEvents();

    auto *twoPage = mw->findChild<TwoPageView *>(QStringLiteral("view.twoPage"));
    QVERIFY(twoPage);
    QScrollBar *hbar = twoPage->horizontalScrollBar();

    // Precondition: zoom in until a spread overflows the viewport horizontally,
    // so there is a real overflow for the fit to remove.
    for (int i = 0; i < 12 && hbar->maximum() == 0; ++i) {
        doc->zoomIn();
        QApplication::processEvents();
    }
    QVERIFY2(hbar->maximum() > 0,
             "precondition: an oversized spread must overflow horizontally");

    // Spread-aware Fit-Width must remove the horizontal overflow (a full spread
    // fits the viewport width; <= 1px slack tolerates canvas-size ceil rounding).
    doc->zoomFitWidth();
    QApplication::processEvents();
    QVERIFY2(hbar->maximum() <= 1,
             qPrintable(QStringLiteral("Fit-Width must fit a full spread without horizontal "
                                       "overflow; hbar max = %1")
                            .arg(hbar->maximum())));

    // Fit-Page likewise fits the spread horizontally (and tightens for height).
    for (int i = 0; i < 12 && hbar->maximum() == 0; ++i) {
        doc->zoomIn();
        QApplication::processEvents();
    }
    QVERIFY2(hbar->maximum() > 0, "re-establish horizontal overflow before Fit-Page");
    doc->zoomFitPage();
    QApplication::processEvents();
    QVERIFY2(hbar->maximum() <= 1,
             qPrintable(QStringLiteral("Fit-Page must fit a full spread without horizontal "
                                       "overflow; hbar max = %1")
                            .arg(hbar->maximum())));
}

// UAT-VWR-079 — the status-bar zoom readout tells the TRUTH about the scale the
// content is actually drawn at in Two-Pages mode. uat_vwr_072 proves the stored
// factors agree (TwoPageView::zoomFactor() == doc->zoomFactor()); this proves
// the NUMBER THE USER SEES ("150%") matches the PAINTED page — the gap that let
// a "100%" evidence shot actually be captured at ~50% because the readout had
// gone stale relative to the true render scale. Measured by grabbing the painted
// cover page and comparing its logical width / native point width against the
// parsed `zoomIndicator` percent, at three distinct zoom levels so a stuck /
// stale readout (fixed number, or number desynced from the render) is caught.
// Runs across the dpr matrix; the measurement divides out dpr so the assertion
// is dpr-invariant.
void TestUatTwoPage::uat_vwr_079_zoomReadoutMatchesRenderScale() {
    QVERIFY(m_scratch.isValid());
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    const QString pdf =
        writeSamplePdf(m_scratch.filePath(QStringLiteral("uat_vwr_079.pdf")), 5);
    MainWindow *mw = openFreshWindow(app, pdf);
    QVERIFY(mw);
    // Wide window so the lone A4 cover fits the viewport WIDTH at every tested
    // zoom (<=125%), keeping the width scan clear of the viewport edges.
    mw->resize(1600, 1000);
    QApplication::processEvents();

    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    IDocument *doc = dv->currentDocument();
    QVERIFY(doc);

    // Enter Two-Pages through the real View-menu action (the user's path).
    QAction *twoPages = findAction(mw, QStringLiteral("action.view.twoPages"));
    QVERIFY(twoPages && twoPages->isEnabled());
    twoPages->trigger();
    QApplication::processEvents();
    QCOMPARE(doc->viewMode(), ViewMode::TwoPages);

    auto *twoPage = mw->findChild<TwoPageView *>(QStringLiteral("view.twoPage"));
    QVERIFY(twoPage);

    // Native point width of the cover, read WITHOUT rendering (same value the
    // paint path scales by pts x zoom), so the true-scale computation is not
    // circular with the render zoom.
    const double nativeW = doc->pageSizeHint(0).width();
    QVERIFY2(nativeW > 0.0, "cover page must report a positive native point width");

    // Drive zoom through the REAL zoom actions so the readout refreshes exactly
    // as it does for the user (MainWindow updates the indicator from the action
    // handlers, not from doc->zoom*()). Reading the label after each keeps a
    // stale-label regression observable rather than masked.
    QAction *actual = findAction(mw, QStringLiteral("action.view.actualSize"));
    QAction *zoomIn = findAction(mw, QStringLiteral("action.view.zoomIn"));
    QAction *zoomOut = findAction(mw, QStringLiteral("action.view.zoomOut"));
    QVERIFY(actual && zoomIn && zoomOut);

    auto checkAtCurrentZoom = [&](const char *ctx) {
        QApplication::processEvents();
        const int shownPct = displayedZoomPercent(mw);
        QVERIFY2(shownPct > 0,
                 qPrintable(QStringLiteral("[%1] zoom indicator must show a "
                                           "positive percent")
                                .arg(QLatin1String(ctx))));
        const double logicalW = measuredCoverWidthLogical(twoPage);
        QVERIFY2(logicalW > 0.0,
                 qPrintable(QStringLiteral("[%1] cover page must be measurable "
                                           "on screen (not clipped) — widen the "
                                           "window if this fails")
                                .arg(QLatin1String(ctx))));
        const int truePct = int(std::lround(logicalW / nativeW * 100.0));
        qInfo().noquote() << "ZOOM-READOUT" << ctx << "shown%" << shownPct
                          << "painted%" << truePct << "logicalW" << logicalW
                          << "nativeW" << nativeW << "dpr" << screenDpr();
        // The readout and the true painted scale must agree within 2 points
        // (sub-pixel page-edge rounding). A stale readout (e.g. "100%" while the
        // page is painted at ~50%) fails this by ~50 points.
        QVERIFY2(std::abs(truePct - shownPct) <= 2,
                 qPrintable(QStringLiteral("[%1] zoom readout %2%% disagrees with "
                                           "the painted render scale %3%% — the "
                                           "readout is lying about the zoom")
                                .arg(QLatin1String(ctx))
                                .arg(shownPct)
                                .arg(truePct)));
    };

    // Level 1: Actual Size — readout must read 100% AND the page must actually
    // be painted at 1 pt -> 1 logical px.
    actual->trigger();
    QApplication::processEvents();
    QCOMPARE(displayedZoomPercent(mw), 100);
    checkAtCurrentZoom("actual-100");

    // Level 2: zoomed out (distinct, well below 100%).
    actual->trigger();
    QApplication::processEvents();
    zoomOut->trigger();
    zoomOut->trigger();
    checkAtCurrentZoom("zoomed-out");

    // Level 3: zoomed in one step (distinct, above 100%, still fits the width).
    actual->trigger();
    QApplication::processEvents();
    zoomIn->trigger();
    checkAtCurrentZoom("zoomed-in");
}

// UAT-VWR-080 — HiDPI backing-resolution oracle. Before this slot the dpr
// matrix {1, 1.5, 2} ran the whole two-page suite three times but asserted
// nothing dpr-SPECIFIC, so a Retina-blur regression (each page rendered at 1x
// logical resolution then upscaled by dpr) would pass silently: the logical
// geometry is identical, only the pixels are mushy. This slot reads the ACTIVE
// dpr for the current matrix variant and asserts (a) the injection took, (b) the
// view's backing store is dpr-aware, and (c) the render TARGET — the per-page
// raster the paint path actually produces — has true pixel resolution
// pts x zoom x dpr, i.e. it scales with dpr instead of staying at the logical
// size. (c) is the one that fails under the 1x-then-upscale regression.
void TestUatTwoPage::uat_vwr_080_dprBackingResolution() {
    QVERIFY(m_scratch.isValid());
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);

    // Prove the dpr injection took: the CMake matrix runs this binary under
    // QT_SCALE_FACTOR in {1, 1.5, 2}; a silent regression to dpr = 1 would make
    // the {1.5, 2} runs vacuous. Read the requested value and assert the primary
    // screen realized it, so every assertion below is a real HiDPI check.
    const double wantDpr = requestedDpr();
    const double gotDpr = screenDpr();
    qInfo().noquote() << "DPR requested" << wantDpr << "primaryScreen" << gotDpr;
    QVERIFY2(std::abs(gotDpr - wantDpr) < 0.01,
             qPrintable(QStringLiteral("dpr injection did not take: QT_SCALE_FACTOR "
                                       "requested %1 but the screen reports %2")
                            .arg(wantDpr)
                            .arg(gotDpr)));

    const QString pdf =
        writeSamplePdf(m_scratch.filePath(QStringLiteral("uat_vwr_080.pdf")), 5);
    MainWindow *mw = openFreshWindow(app, pdf);
    QVERIFY(mw);
    mw->resize(1200, 800);
    QApplication::processEvents();

    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    IDocument *doc = dv->currentDocument();
    QVERIFY(doc);

    doc->setViewMode(ViewMode::TwoPages);
    QApplication::processEvents();
    auto *twoPage = mw->findChild<TwoPageView *>(QStringLiteral("view.twoPage"));
    QVERIFY(twoPage);

    // Actual Size — a clean lw = native point width for the resolution math.
    QAction *actual = findAction(mw, QStringLiteral("action.view.actualSize"));
    QVERIFY(actual);
    actual->trigger();
    QApplication::processEvents();

    // The realized dpr the widget tree paints at (matches the primary screen
    // under the offscreen plugin's fixed-at-startup dpr).
    const double dpr = twoPage->viewport()->devicePixelRatioF();
    QVERIFY2(std::abs(dpr - wantDpr) < 0.01, "widget dpr must match the injected dpr");

    // (b) Backing store is dpr-aware: a viewport grab carries the dpr and its
    // PHYSICAL pixel dimensions are dpr x the logical size. This proves the
    // paint surface itself is HiDPI, not a 1x buffer.
    const QSize vpLogical = twoPage->viewport()->size();
    const QImage vpGrab = twoPage->viewport()->grab().toImage();
    QVERIFY(!vpGrab.isNull());
    qInfo().noquote() << "VIEWPORT logical" << vpLogical << "grab-physical"
                      << vpGrab.size() << "grab-dpr" << vpGrab.devicePixelRatio();
    QVERIFY2(std::abs(vpGrab.devicePixelRatio() - dpr) < 0.01,
             "viewport grab must carry the device-pixel ratio");
    QVERIFY2(std::abs(vpGrab.width() - std::lround(vpLogical.width() * dpr)) <= 1 &&
                 std::abs(vpGrab.height() - std::lround(vpLogical.height() * dpr)) <= 1,
             qPrintable(QStringLiteral("viewport backing store must be dpr x the "
                                       "logical size: logical %1x%2 * dpr %3 vs "
                                       "physical %4x%5")
                            .arg(vpLogical.width())
                            .arg(vpLogical.height())
                            .arg(dpr)
                            .arg(vpGrab.width())
                            .arg(vpGrab.height())));

    // (c) THE anti-blur oracle. renderPageImage(0) is the exact per-page raster
    // paintEvent() draws (shared code). Its device-pixel size must be
    // pts x zoom x dpr — the render target's TRUE resolution scaling with dpr.
    // Expected width is computed independently from the native point size
    // (pageSizeHint, no render) x the zoom x the live dpr, so it is not circular
    // with the view's own render.
    const double nativeW = doc->pageSizeHint(0).width();
    const double nativeH = doc->pageSizeHint(0).height();
    const double zoom = doc->zoomFactor();
    QVERIFY(nativeW > 0.0 && nativeH > 0.0);
    const QImage pageImg = twoPage->renderPageImage(0);
    QVERIFY2(!pageImg.isNull(), "renderPageImage(0) must produce the page raster");

    const int expDevW = int(std::ceil(nativeW * zoom * dpr));
    const int expDevH = int(std::ceil(nativeH * zoom * dpr));
    const int logicalRasterW = int(std::ceil(nativeW * zoom)); // the 1x resolution
    qInfo().noquote() << "PAGE-RASTER device" << pageImg.size() << "raster-dpr"
                      << pageImg.devicePixelRatio() << "expDev" << expDevW << "x"
                      << expDevH << "logical1x-width" << logicalRasterW << "dpr"
                      << dpr;

    QVERIFY2(std::abs(pageImg.devicePixelRatio() - dpr) < 0.01,
             "page raster must be stamped with the device-pixel ratio");
    // Device resolution scales with dpr (== the logical raster x dpr). A
    // 1x-then-upscale regression would leave renderPageImage at ~logicalRasterW
    // and fail this by a factor of dpr.
    QVERIFY2(std::abs(pageImg.width() - expDevW) <= 1 &&
                 std::abs(pageImg.height() - expDevH) <= 1,
             qPrintable(QStringLiteral("page render target must be dpr x the logical "
                                       "raster: expected %1x%2 device px (native "
                                       "%3x%4 * zoom %5 * dpr %6), got %7x%8")
                            .arg(expDevW)
                            .arg(expDevH)
                            .arg(nativeW)
                            .arg(nativeH)
                            .arg(zoom)
                            .arg(dpr)
                            .arg(pageImg.width())
                            .arg(pageImg.height())));
    // And the logical size the raster occupies is dpr-invariant (pts x zoom):
    // physical width / dpr rounds back to the 1x logical raster width.
    QVERIFY2(std::abs(int(std::lround(pageImg.width() / dpr)) - logicalRasterW) <= 1,
             "page raster must occupy pts x zoom LOGICAL pixels regardless of dpr");
    // Explicit anti-upscale guard at HiDPI: the device raster is strictly wider
    // than the 1x logical raster, so content is genuinely rendered at higher
    // resolution rather than upscaled. (At dpr = 1 there is nothing to upscale.)
    if (dpr > 1.01) {
        QVERIFY2(pageImg.width() > logicalRasterW + 1,
                 qPrintable(QStringLiteral("at dpr %1 the page raster (%2 px) must "
                                           "exceed the 1x logical width (%3 px) — "
                                           "otherwise the page is rendered at 1x and "
                                           "upscaled (Retina blur)")
                                .arg(dpr)
                                .arg(pageImg.width())
                                .arg(logicalRasterW)));
    }
}

// UAT-VWR-081 — Next/Previous Page walk EVERY spread in Two-Pages mode, in
// order, to the last spread (not stuck). This is the reviewer's empirical
// repro: currentPage() returns the leading page of the top-visible spread, but
// Next Page was wired as goToPage(currentPage()+1) — from the left page of a
// spread, +1 is that spread's RIGHT page, which scrollToPage maps back to the
// SAME spread, so navigation stuck at the second spread and could never reach
// the later ones. Drives the REAL View-menu Next/Previous Page actions (the
// user's path, and the exact code that regressed) rather than calling
// goToPage() directly, so it guards the wiring, not just the helper. Fails
// against the old currentPage()±1 wiring, which produces 1,1,1,… here.
void TestUatTwoPage::uat_vwr_081_nextPageWalksAllSpreads() {
    QVERIFY(m_scratch.isValid());
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    // 8-page cover-alone book → spreads [1],[2,3],[4,5],[6,7],[8]; 0-based
    // leading page indices, in order: 0, 1, 3, 5, 7.
    const QString pdf =
        writeSamplePdf(m_scratch.filePath(QStringLiteral("uat_vwr_081.pdf")), 8);
    MainWindow *mw = openFreshWindow(app, pdf);
    QVERIFY(mw);
    // A short viewport at Actual Size keeps every spread (each a full A4 page
    // tall) larger than the viewport, so scrollToPage can top-align EACH spread
    // — including the last — instead of clamping it against the canvas bottom.
    // That makes currentPage() (derived from the top-visible spread) reach the
    // final spread's leading page deterministically.
    mw->resize(1000, 640);
    QApplication::processEvents();

    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    IDocument *doc = dv->currentDocument();
    QVERIFY(doc);
    doc->setViewMode(ViewMode::TwoPages);
    QApplication::processEvents();
    doc->zoomActual();
    QApplication::processEvents();

    auto *twoPage = mw->findChild<TwoPageView *>(QStringLiteral("view.twoPage"));
    QVERIFY(twoPage);
    QVERIFY2(twoPage->verticalScrollBar()->maximum() > 0,
             "need a real scroll range so each spread can top-align");

    // Cover-alone pairing sanity: 8 pages → 5 spreads.
    QCOMPARE(int(twoPage->spreads().size()), 5);

    QAction *nextPage = findAction(mw, QStringLiteral("action.view.nextPage"));
    QAction *prevPage = findAction(mw, QStringLiteral("action.view.previousPage"));
    QVERIFY2(nextPage && prevPage, "Next/Previous Page actions must exist");

    // Start at the cover.
    doc->goToPage(0);
    QApplication::processEvents();
    QCOMPARE(doc->currentPage(), 0);

    // Pressing Next Page four times must visit spread leading pages 1,3,5,7 in
    // order — every spread, to the last. The old currentPage()+1 wiring yields
    // 1,1,1,1 here (stuck on the second spread), so this sequence is the guard.
    const QVector<int> forwardExpected{1, 3, 5, 7};
    QVector<int> forwardGot;
    for (int i = 0; i < forwardExpected.size(); ++i) {
        nextPage->trigger();
        QApplication::processEvents();
        forwardGot.push_back(doc->currentPage());
    }
    QVERIFY2(forwardGot == forwardExpected,
             qPrintable(QStringLiteral("Next Page must walk every spread to the last "
                                       "(expected 1,3,5,7); got %1 — navigation is stuck")
                            .arg([&] {
                                QStringList s;
                                for (int v : forwardGot)
                                    s << QString::number(v);
                                return s.join(QLatin1Char(','));
                            }())));

    // Next Page at the last spread clamps (stays on the final leading page).
    nextPage->trigger();
    QApplication::processEvents();
    QCOMPARE(doc->currentPage(), 7);

    // Previous Page walks back symmetrically: 5,3,1,0.
    const QVector<int> backExpected{5, 3, 1, 0};
    QVector<int> backGot;
    for (int i = 0; i < backExpected.size(); ++i) {
        prevPage->trigger();
        QApplication::processEvents();
        backGot.push_back(doc->currentPage());
    }
    QVERIFY2(backGot == backExpected,
             qPrintable(QStringLiteral("Previous Page must walk back through every spread "
                                       "(expected 5,3,1,0); got %1")
                            .arg([&] {
                                QStringList s;
                                for (int v : backGot)
                                    s << QString::number(v);
                                return s.join(QLatin1Char(','));
                            }())));

    // Previous Page at the cover clamps (stays on page 0).
    prevPage->trigger();
    QApplication::processEvents();
    QCOMPARE(doc->currentPage(), 0);
}

// UAT-VWR-082 — an oversized spread (taller than 2x the viewport) is NOT
// abandoned at its midpoint in Two-Pages mode. When the user scrolls into such a
// spread's bottom half — the spread still fills the viewport, the NEXT spread's
// top has not yet crossed the viewport top — the current-page indicator must
// still report the CURRENT spread's leading page, not the next. The prior
// midpoint rule flipped to the next spread the instant the scroll passed the
// spread's centre, so currentPage() led by one and Next Page could skip a whole
// spread. Guards the top-crossing rule in TwoPageView::topVisibleLeadingPage().
// FAILS against the old midpoint implementation (it reports page 1 at the tested
// offset instead of 0); passes with the top-crossing rule.
void TestUatTwoPage::uat_vwr_082_oversizedSpreadTracking() {
    QVERIFY(m_scratch.isValid());
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    // 6-page cover-alone book → spreads [1],[2,3],[4,5],[6]; 0-based leading
    // pages 0,1,3,5. We exercise the FIRST spread (the lone cover, page 0).
    const QString pdf =
        writeSamplePdf(m_scratch.filePath(QStringLiteral("uat_vwr_082.pdf")), 6);
    MainWindow *mw = openFreshWindow(app, pdf);
    QVERIFY(mw);
    mw->resize(900, 700);
    QApplication::processEvents();

    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    IDocument *doc = dv->currentDocument();
    QVERIFY(doc);
    doc->setViewMode(ViewMode::TwoPages);
    QApplication::processEvents();

    auto *twoPage = mw->findChild<TwoPageView *>(QStringLiteral("view.twoPage"));
    QVERIFY(twoPage);
    QScrollBar *vbar = twoPage->verticalScrollBar();

    // Reproduce "Actual Size / manual zoom in a small window": make the cover
    // spread MORE THAN 2x taller than the viewport. Drive the view's zoom
    // directly — its scroll-tracking logic is what this slot exercises, and
    // uat_vwr_072/079 already prove that zoom is shared with the truthful readout
    // — computing the factor from the LIVE viewport height so the >2x relation
    // holds regardless of the offscreen chrome height.
    const int vpH = twoPage->viewport()->height();
    QVERIFY2(vpH > 0, "viewport must have a real height");
    const double nativeH = doc->pageSizeHint(0).height();
    QVERIFY2(nativeH > 0.0, "cover must report a positive native height");
    const double zoom = (2.5 * vpH) / nativeH; // spread height ≈ 2.5x the viewport
    twoPage->setZoomFactor(zoom);
    QApplication::processEvents();

    const double spreadH = nativeH * zoom;
    QVERIFY2(spreadH > 2.0 * vpH,
             "precondition: the cover spread must be taller than 2x the viewport");
    QVERIFY2(vbar->maximum() > 0, "need a real scroll range to move through the spread");

    // Top-aligning the cover reports the cover (page 0) — the top-align
    // correctness the top-crossing rule must preserve.
    vbar->setValue(0);
    QApplication::processEvents();
    QCOMPARE(doc->currentPage(), 0);

    // Find spread 1's ([2,3]) top-align offset: the FIRST scroll offset at which
    // advancing to spread 1 is correct (its top reaches the viewport top).
    // scrollToPage sets vbar to absoluteTop(spread1) - kOuterMargin, i.e. exactly
    // this switch point. currentPage() legitimately becomes 1 here.
    twoPage->scrollToPage(1); // spread [2,3], leading page index 1
    QApplication::processEvents();
    const int spread1TopAlign = vbar->value();
    QVERIFY2(spread1TopAlign > 0, "spread 1 must have a real top-align offset");
    QCOMPARE(doc->currentPage(), 1);

    // Scroll into the cover's BOTTOM HALF but short of spread 1's top-align: the
    // cover still fills the viewport, spread 1's top has NOT crossed the viewport
    // top. 0.75x the spread height is safely past the cover's midpoint yet well
    // below spread1TopAlign (~spreadH + gap up), so it is inside the exact band
    // where the old midpoint rule mis-reports the next spread.
    const int bottomHalf = static_cast<int>(spreadH * 0.75);
    QVERIFY2(bottomHalf < spread1TopAlign,
             qPrintable(QStringLiteral("test scroll point %1 must be BELOW spread 1's "
                                       "top-align %2 (next spread not yet in view)")
                            .arg(bottomHalf)
                            .arg(spread1TopAlign)));

    QSignalSpy spy(twoPage, &TwoPageView::currentPageChanged);
    vbar->setValue(bottomHalf);
    QApplication::processEvents();

    // The cover is still the current spread. Reporting page 1 here (the old
    // midpoint rule) would lead the indicator by one and let Next Page skip the
    // cover→2·3 step. currentPage() — and the last currentPageChanged value —
    // must still be the cover's leading page, 0.
    QVERIFY2(doc->currentPage() == 0,
             qPrintable(QStringLiteral("oversized cover spread must stay current in its "
                                       "bottom half (expected leading page 0); got %1")
                            .arg(doc->currentPage())));
    if (spy.count() > 0)
        QCOMPARE(spy.last().at(0).toInt(), 0);
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
    TestUatTwoPage tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_uat_two_page.moc"
