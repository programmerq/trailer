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
#include <QTemporaryDir>
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

    // The read-only mode banner is the PRIMARY degradation signal. Look it up by
    // stable objectName; it is hidden outside Two-Pages mode.
    auto *banner = mw->findChild<QLabel *>(QStringLiteral("twoPageModeBanner"));
    QVERIFY2(banner, "Two-Pages read-only banner (objectName twoPageModeBanner) must exist");

    // Switch modes exactly as the user does — via the View-menu actions — so
    // the degradation runs through the real command path, not a back-door
    // setViewMode call.
    continuous->trigger();
    QApplication::processEvents();
    QVERIFY2(find->isEnabled(), "Find is available in Continuous mode");
    QVERIFY2(!banner->isVisible(), "read-only banner must be hidden outside Two-Pages mode");

    // Two-Pages: markup + search disabled-with-tooltip pointing back to the
    // working modes.
    QVERIFY2(twoPages->isEnabled(), "Two Pages must be enabled for this multi-page PDF");
    twoPages->trigger();
    QApplication::processEvents();
    QCOMPARE(doc->viewMode(), ViewMode::TwoPages);

    // Primary signal: the read-only banner is now visible with the exact copy.
    QVERIFY2(banner->isVisible(), "read-only banner must be visible in Two-Pages mode");
    QCOMPARE(banner->text(),
             QStringLiteral("Two Pages is a read-only view — switch to Single or "
                            "Continuous to edit"));

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

    // Leaving Two-Pages restores both and hides the banner.
    continuous->trigger();
    QApplication::processEvents();
    QVERIFY2(find->isEnabled(), "Find re-enables when leaving Two-Pages mode");
    QVERIFY2(markup->isEnabled(), "markup re-enables when leaving Two-Pages mode");
    QVERIFY2(!banner->isVisible(), "read-only banner must hide again on leaving Two-Pages mode");
}

// UAT-VWR-074 — curated G2 evidence. Grabs the SAME multi-page document in
// Single mode (before) and Two-Pages mode (after) — the required before/after
// pair — plus a reconstruction of the disabled "Two Pages" toggle with its
// tooltip (a tooltip cannot be reached in an offscreen static grab(), so the
// reconstruction is the accepted fallback, noted in the PR). Writes PNGs only
// when TRAILER_TWO_PAGE_EVIDENCE_DIR is set; otherwise it still exercises the
// same render path as a plain assertion so the slot is a real test in CI.
void TestUatTwoPage::uat_vwr_074_g2Evidence() {
    QVERIFY(m_scratch.isValid());
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);

    const QString pdf =
        writeSamplePdf(m_scratch.filePath(QStringLiteral("uat_vwr_074.pdf")), 6);
    MainWindow *mw = openFreshWindow(app, pdf);
    QVERIFY(mw);
    mw->resize(1000, 760);
    QApplication::processEvents();

    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    IDocument *doc = dv->currentDocument();
    QVERIFY(doc);

    auto *continuous = findAction(mw, QStringLiteral("action.view.continuous"));
    auto *single = findAction(mw, QStringLiteral("action.view.singlePage"));
    auto *twoPages = findAction(mw, QStringLiteral("action.view.twoPages"));
    QVERIFY(single && twoPages && continuous);

    const QByteArray dir = qgetenv("TRAILER_TWO_PAGE_EVIDENCE_DIR");
    auto save = [&](const QImage &img, const QString &name) {
        if (dir.isEmpty())
            return;
        QDir().mkpath(QString::fromLocal8Bit(dir));
        const QString path = QString::fromLocal8Bit(dir) + QDir::separator() + name;
        QVERIFY2(!img.isNull() && img.save(path, "PNG"),
                 qPrintable(QStringLiteral("failed to save %1").arg(path)));
    };

    // BEFORE: Single mode on the 6-page document.
    single->trigger();
    QApplication::processEvents();
    const QPixmap before = mw->grab();
    QVERIFY(!before.isNull());
    save(before.toImage(), QStringLiteral("2026-07-21-two-page-single-before.png"));

    // AFTER: Two-Pages mode on the SAME document, TOP-ANCHORED. Zoom out enough
    // that the lone cover (page 1) AND the first facing pair (2,3) are both
    // visible at scroll 0, so the shot makes the cover-alone pairing (page 1
    // alone, then 2 & 3 side by side) unmistakable — and page 1 appears in both
    // the before and after shots.
    QVERIFY(twoPages->isEnabled());
    twoPages->trigger();
    QApplication::processEvents();
    for (int i = 0; i < 4; ++i)
        doc->zoomOut();
    QApplication::processEvents();
    auto *twoPageView = mw->findChild<TwoPageView *>(QStringLiteral("view.twoPage"));
    QVERIFY(twoPageView);
    twoPageView->verticalScrollBar()->setValue(0);
    QApplication::processEvents();
    const QPixmap after = mw->grab();
    QVERIFY(!after.isNull());
    save(after.toImage(), QStringLiteral("2026-07-21-two-page-facing-after.png"));

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
    panel.resize(560, 160);
    panel.show();
    QApplication::processEvents();
    const QPixmap disabled = panel.grab();
    QVERIFY(!disabled.isNull());
    save(disabled.toImage(), QStringLiteral("2026-07-21-two-page-toggle-disabled.png"));
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
