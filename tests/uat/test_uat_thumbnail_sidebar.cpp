// UAT harness — sidebar thumbnail sizing (scale-to-width + aspect-fit rows).
//
// Pins the P1 fix tracked in
// docs/backlog/2026-07-13-thumbnail-sidebar-sizing.md and decided in
// docs/decision-records/0006-thumbnail-scale-to-width.md.
//
// The bug: sidebar PDF thumbnails rendered inside a fixed 80x100 logical
// box, centred in the full-width column (~1/8 of the sidebar width) with a
// fixed 108 px row height that left vertical slack under landscape pages.
//
// Target behaviour, proven here for a MIXED-orientation deck at two sidebar
// widths:
//   1. Painted thumbnail fills the column width (scale-to-width, left-aligned).
//   2. Each row's visualRect height tracks the page aspect
//      (round(availW / aspect) + 2*kThumbVerticalPadding), so portrait rows
//      are tall and landscape rows short — no fixed 108 px gap.
//
// This test is written test-FIRST: on the unmodified Sidebar it FAILS (every
// row is 108 px regardless of width/aspect) and captures BEFORE screenshots.

#include "app/Application.h"
#include "document/IDocument.h"
#include "ui/DocumentView.h"
#include "ui/MainWindow.h"
#include "ui/Sidebar.h"
#include "ui/ThumbnailModel.h"

#include <QAbstractItemModel>
#include <QColor>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QListView>
#include <QScreen>
#include <QPageSize>
#include <QPainter>
#include <QPdfDocument>
#include <QPdfView>
#include <QPdfWriter>
#include <QPixmap>
#include <QSignalSpy>
#include <QSizeF>
#include <QString>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <cmath>

using namespace trailer;

namespace {

// These constants mirror the file-local ones in Sidebar.cpp's anonymous
// namespace (kThumbVerticalPadding, kThumbHorizontalMargin). They are not
// exported, so the test hardcodes the same values; if the Sidebar changes
// them, update here too. They are load-bearing for the height oracle below.
constexpr int kThumbVerticalPadding = 4;
constexpr int kThumbHorizontalMargin = 6;

MainWindow *currentMainWindow() {
    for (auto *w : QApplication::topLevelWidgets()) {
        if (auto *mw = qobject_cast<MainWindow *>(w))
            return mw;
    }
    return nullptr;
}

// The devicePixelRatio this run was launched under. The CMake dpr matrix
// (tests/uat/CMakeLists.txt, trailer_register_uat_dpr_matrix) sets
// QT_SCALE_FACTOR per process; Qt reads it before QGuiApplication and
// stamps it on every QScreen. Defaults to 1.0 when unset (a plain `ctest`
// run). The offscreen platform reports dpr = 1 by default, so a value > 1
// here is the proof the injection actually took — per the backlog
// threshold, "a passing run at the ambient dpr is not evidence."
double requestedDpr() {
    bool ok = false;
    const double v = qEnvironmentVariable("QT_SCALE_FACTOR").toDouble(&ok);
    return (ok && v > 0.0) ? v : 1.0;
}

// The dpr Qt actually realized on the primary screen (what the widget tree
// and the thumbnail render path see).
double screenDpr() {
    auto *s = QGuiApplication::primaryScreen();
    return s ? s->devicePixelRatio() : 1.0;
}

// Directory that persists past the test run (mirrors the helper in
// test_uat_foundations.cpp) so the G2 evidence PNGs can be collected from
// the build tree after the run.
QString screenshotDir() {
    QDir dir(QDir::current());
    dir.mkpath(QStringLiteral("uat-screenshots"));
    return dir.absoluteFilePath(QStringLiteral("uat-screenshots"));
}

void grabTo(QWidget *w, const QString &name) {
    const QString path = QDir(screenshotDir()).absoluteFilePath(name);
    w->grab().save(path, "PNG");
    qInfo().noquote() << "G2-SCREENSHOT" << path;
}

// Build a mixed-orientation PDF: alternating PORTRAIT and LANDSCAPE pages.
// Even indices (0,2,4) are A4 portrait (aspect ~0.71). Odd indices (1,3,5)
// are an EXTREME ~2.5:1 panoramic landscape (800x320 pt), NOT A4-landscape's
// ~1.4:1. The extreme aspect is deliberate: under scale-to-width the landscape
// row height is round(availW / ~2.5) + padding, which lands far from the
// legacy fixed 108 px row at BOTH tested widths, so a fixed-108 regression
// cannot hide inside the height oracle's ±3 tolerance (see the explicit
// not-108 guard in checkAtWidth). QPdfWriter honours a per-page size via
// setPageSize() before newPage(). Each page carries a page-numbered line so
// the thumbnails are visually distinct.
QString writeMixedPdf(const QString &path, int pages) {
    QPdfWriter writer(path);
    const QPageSize portraitSize(QPageSize::A4);
    const QPageSize landscapeSize(QSizeF(800, 320), QPageSize::Point,
                                  QStringLiteral("wide"), QPageSize::ExactMatch);
    writer.setPageSize(portraitSize); // page 0 is portrait
    QPainter p(&writer);
    for (int i = 0; i < pages; ++i) {
        const bool portrait = (i % 2 == 0);
        if (i > 0) {
            writer.setPageSize(portrait ? portraitSize : landscapeSize);
            writer.newPage();
        }
        p.drawText(QRect(50, 50, 700, 200), Qt::AlignCenter,
                   QStringLiteral("Page %1 %2")
                       .arg(i + 1)
                       .arg(portrait ? QStringLiteral("PORTRAIT")
                                     : QStringLiteral("LANDSCAPE")));
    }
    p.end();
    return path;
}

} // namespace

class TestUatThumbnailSidebar : public QObject {
    Q_OBJECT
  private slots:
    void init();
    void uat_thumb_010_scaleToWidthAndAspectRows();
    void uat_thumb_020_imageDocAspectRow();

  private:
    // Drive one sidebar width: resize the dock, settle the debounce, capture
    // evidence, and assert the per-row height oracle + a paper-width scan.
    void checkAtWidth(MainWindow *mw, Sidebar *sidebar, QListView *view,
                      QPdfDocument *qpdf, int dockWidth, int portraitIndex,
                      int landscapeIndex);

    // Device-pixel width of the portrait row's cached DecorationRole pixmap
    // from the PREVIOUS (narrower) checkAtWidth call. Lets the next (wider)
    // call assert the debounced re-render actually produced a crisper pixmap.
    // 0 before the first call. Assumes checkAtWidth is invoked narrow→wide.
    int m_prevPortraitPixmapW = 0;

    QTemporaryDir m_scratch;
};

void TestUatThumbnailSidebar::init() {
    for (auto *w : QApplication::topLevelWidgets()) {
        if (qobject_cast<MainWindow *>(w))
            w->close();
    }
    QApplication::processEvents();
}

void TestUatThumbnailSidebar::checkAtWidth(MainWindow *mw, Sidebar *sidebar,
                                           QListView *view, QPdfDocument *qpdf,
                                           int dockWidth, int portraitIndex,
                                           int landscapeIndex) {
    mw->resizeDocks({sidebar}, {dockWidth}, Qt::Horizontal);
    QApplication::processEvents();
    // Allow the resize debounce (~120 ms) to fire so the re-render at the
    // new column width lands; layout itself is immediate via
    // scheduleDelayedItemsLayout().
    QTest::qWait(250);
    QApplication::processEvents();

    const int vpW = view->viewport()->width();
    const int availW = vpW - 2 * kThumbHorizontalMargin;
    qInfo().noquote() << "WIDTH-CASE dock" << dockWidth << "viewport" << vpW
                      << "availW" << availW;

    // Evidence FIRST so a failing run still emits before-images.
    grabTo(sidebar, QStringLiteral("thumbnail-sidebar-%1.png").arg(vpW));

    const QModelIndex pIdx = view->model()->index(portraitIndex, 0);
    const QModelIndex lIdx = view->model()->index(landscapeIndex, 0);
    QVERIFY(pIdx.isValid());
    QVERIFY(lIdx.isValid());

    // Ground-truth aspect from the underlying QPdfDocument (independent of
    // the model's AspectRole, so the height oracle is not circular).
    const QSizeF pPts = qpdf->pagePointSize(portraitIndex);
    const QSizeF lPts = qpdf->pagePointSize(landscapeIndex);
    QVERIFY(!pPts.isEmpty());
    QVERIFY(!lPts.isEmpty());
    const double pAspect = pPts.width() / pPts.height();
    const double lAspect = lPts.width() / lPts.height();
    QVERIFY2(pAspect < 1.0, "portrait page must have aspect < 1");
    QVERIFY2(lAspect > 1.0, "landscape page must have aspect > 1");

    const int expPortraitH =
        int(std::lround(availW / pAspect)) + 2 * kThumbVerticalPadding;
    const int expLandscapeH =
        int(std::lround(availW / lAspect)) + 2 * kThumbVerticalPadding;

    const int gotPortraitH = view->visualRect(pIdx).height();
    const int gotLandscapeH = view->visualRect(lIdx).height();

    qInfo().noquote() << "HEIGHT portrait expected" << expPortraitH << "got"
                      << gotPortraitH << "| landscape expected" << expLandscapeH
                      << "got" << gotLandscapeH;

    // Per-row height oracle: proves BOTH axes. Height is coupled to width
    // through aspect under scale-to-width, so a matching height confirms the
    // pixmap filled availW. Old code returned a fixed 108 for every row.
    QVERIFY2(std::abs(gotPortraitH - expPortraitH) <= 3,
             qPrintable(QStringLiteral("portrait row height %1, expected ~%2 "
                                       "(availW=%3, aspect=%4)")
                            .arg(gotPortraitH)
                            .arg(expPortraitH)
                            .arg(availW)
                            .arg(pAspect)));
    QVERIFY2(std::abs(gotLandscapeH - expLandscapeH) <= 3,
             qPrintable(QStringLiteral("landscape row height %1, expected ~%2 "
                                       "(availW=%3, aspect=%4)")
                            .arg(gotLandscapeH)
                            .arg(expLandscapeH)
                            .arg(availW)
                            .arg(lAspect)));

    // Sanity: portrait rows are TALLER than landscape rows (aspect applied,
    // not a fixed row height).
    QVERIFY2(gotPortraitH > gotLandscapeH + 20,
             "portrait rows must be visibly taller than landscape rows");

    // Explicit anti-regression guard against the legacy fixed 108 px row.
    // The extreme ~2.5:1 landscape aspect (see writeMixedPdf) keeps the
    // fitted landscape height unambiguously clear of 108 at BOTH tested
    // widths, so a reintroduced fixed-108 sizeHint fails here loudly instead
    // of sneaking inside the ±3 oracle above.
    QVERIFY2(std::abs(gotLandscapeH - 108) > 3,
             qPrintable(QStringLiteral("landscape row height %1 is within ±3 of "
                                       "the legacy fixed 108 px row (availW=%2, "
                                       "aspect=%3)")
                            .arg(gotLandscapeH)
                            .arg(availW)
                            .arg(lAspect)));

    // Crispness: the DecorationRole pixmap is re-rendered at the current
    // column width (debounced), so at a wider sidebar its device-pixel width
    // must GROW versus the previous narrower call — pinning ADR 0006's "stays
    // crisp" claim (not stuck at the old ~80 px box). DPR is constant across
    // calls (same screen), so comparing raw device pixels is consistent.
    const QPixmap pPix = qvariant_cast<QPixmap>(pIdx.data(Qt::DecorationRole));
    QVERIFY2(!pPix.isNull(), "portrait DecorationRole pixmap must render");
    const int pPixW = pPix.width();
    qInfo().noquote() << "PIXMAP portrait device-width" << pPixW << "prev"
                      << m_prevPortraitPixmapW << "dpr"
                      << pPix.devicePixelRatio();

    // dpr-relative crispness oracle: ThumbnailModel renders each thumbnail
    // at the column width in DEVICE pixels (round(availW * screenDpr)), so a
    // Retina screen must yield a proportionally larger raw pixmap — that is
    // what keeps it sharp. A widget that hard-codes a dpr = 1 buffer (the #55
    // bug class) leaves this at ~availW regardless of dpr. Scaling the
    // expectation by the live screen dpr keeps the same assertion honest at
    // 1, 1.5 and 2 rather than baking in a dpr = 1 pixel count.
    const int expDeviceW = int(std::lround(availW * screenDpr()));
    QVERIFY2(std::abs(pPixW - expDeviceW) <= 2,
             qPrintable(QStringLiteral("portrait thumbnail raw width %1 px, "
                                       "expected ~%2 (availW=%3 * dpr=%4) — a "
                                       "dpr-blind render would sit at ~availW")
                            .arg(pPixW)
                            .arg(expDeviceW)
                            .arg(availW)
                            .arg(screenDpr())));

    if (m_prevPortraitPixmapW > 0) {
        QVERIFY2(pPixW > m_prevPortraitPixmapW,
                 qPrintable(QStringLiteral("wider sidebar must re-render a "
                                           "crisper (wider) pixmap: got %1 px, "
                                           "previous (narrower) was %2 px")
                                .arg(pPixW)
                                .arg(m_prevPortraitPixmapW)));
    }
    m_prevPortraitPixmapW = pPixW;

    // Direct width scan on a portrait row: the thumbnail (white paper +
    // 1px border) must horizontally fill ~availW, left-aligned at the margin.
    // Grab the viewport so visualRect coordinates map directly into the image.
    // Scroll the portrait row fully into view first so its band is on-screen.
    view->scrollTo(pIdx, QAbstractItemView::PositionAtTop);
    QApplication::processEvents();
    const QRect band = view->visualRect(pIdx);
    QImage vp = view->viewport()->grab().toImage();
    const double sx =
        view->viewport()->width() > 0
            ? double(vp.width()) / double(view->viewport()->width())
            : 1.0;
    const double sy =
        view->viewport()->height() > 0
            ? double(vp.height()) / double(view->viewport()->height())
            : 1.0;
    const int scanY = int((band.top() + band.height() / 2) * sy);
    // The width scan is the ONLY pixel-level proof that the pixmap actually
    // fills the column; it must never silently pass. We scrolled the portrait
    // row to the top above, so its mid-band is on-screen — hard-assert that
    // rather than skip, so the horizontal proof can't quietly vanish.
    const bool widthScanUsable = scanY >= 0 && scanY < vp.height() && vp.width() > 8;
    QVERIFY2(widthScanUsable,
             qPrintable(QStringLiteral("width-scan band must be on-screen "
                                       "(scanY=%1, vpImgH=%2, vpImgW=%3)")
                            .arg(scanY)
                            .arg(vp.height())
                            .arg(vp.width())));
    // Background sampled at the far-left gutter (x within the margin).
    const QRgb bg = vp.pixel(1, scanY);
    auto differs = [&](QRgb c) {
        const int dr = qAbs(qRed(c) - qRed(bg));
        const int dg = qAbs(qGreen(c) - qGreen(bg));
        const int db = qAbs(qBlue(c) - qBlue(bg));
        return (dr + dg + db) > 24; // tolerant: paper/border vs sidebar base
    };
    int left = -1, right = -1;
    for (int x = 0; x < vp.width(); ++x) {
        if (differs(vp.pixel(x, scanY))) {
            if (left < 0)
                left = x;
            right = x;
        }
    }
    const double extent = (left >= 0) ? (right - left + 1) / sx : 0.0;
    qInfo().noquote() << "WIDTH-SCAN scanY" << scanY << "left" << left << "right"
                      << right << "extent" << extent << "availW" << availW;
    // Generous tolerance; the thumbnail must span most of the column.
    QVERIFY2(extent >= availW - 6,
             qPrintable(QStringLiteral("thumbnail paper extent %1 px, "
                                       "expected >= %2 (availW=%3)")
                            .arg(extent)
                            .arg(availW - 6)
                            .arg(availW)));
}

// UAT-THUMB-010 — sidebar thumbnails scale to the column width and rows fit
// each page's aspect, at two distinct sidebar widths.
void TestUatThumbnailSidebar::uat_thumb_010_scaleToWidthAndAspectRows() {
    QVERIFY(m_scratch.isValid());

    // Prove the dpr injection took: the CMake matrix runs this binary under
    // QT_SCALE_FACTOR ∈ {1, 1.5, 2}, and the whole scale-to-width oracle
    // below is only a HiDPI test if the screen actually reports that dpr.
    // A silent regression to dpr = 1 (env not plumbed, Qt behaviour change)
    // would make the {1.5, 2} runs vacuous — fail loudly instead.
    const double wantDpr = requestedDpr();
    const double gotDpr = screenDpr();
    qInfo().noquote() << "DPR requested" << wantDpr << "primaryScreen" << gotDpr;
    QVERIFY2(std::abs(gotDpr - wantDpr) < 0.01,
             qPrintable(QStringLiteral("dpr injection did not take: "
                                       "QT_SCALE_FACTOR requested %1 but the "
                                       "primary screen reports dpr %2")
                            .arg(wantDpr)
                            .arg(gotDpr)));

    // 6 pages: 0,2,4 portrait; 1,3,5 landscape.
    const QString pdfPath =
        writeMixedPdf(m_scratch.filePath(QStringLiteral("uat_thumb_010.pdf")), 6);

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    mw->resize(1100, 800);
    mw->show();
    (void)QTest::qWaitForWindowExposed(mw);
    QApplication::processEvents();

    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    IDocument *doc = dv->currentDocument();
    QVERIFY(doc);
    QCOMPARE(doc->pageCount(), 6);

    auto *sidebar = mw->findChild<Sidebar *>();
    QVERIFY2(sidebar, "MainWindow should host a Sidebar");
    // Force the Pages thumbnail view visible (short deck won't auto-open).
    sidebar->setMode(Sidebar::Mode::Pages);
    QApplication::processEvents();

    // Pick the thumbnail list specifically: the sidebar also hosts a
    // QListWidget (annotations, itself a QListView subclass), so match on
    // the ThumbnailModel instead of taking the first QListView.
    QListView *view = nullptr;
    for (auto *lv : sidebar->findChildren<QListView *>()) {
        if (qobject_cast<ThumbnailModel *>(lv->model())) {
            view = lv;
            break;
        }
    }
    QVERIFY2(view, "Sidebar should host a QListView backed by ThumbnailModel");
    QCOMPARE(view->model()->rowCount(), 6);

    auto *pdfView = mw->findChild<QPdfView *>();
    QVERIFY2(pdfView && pdfView->document(),
             "MainWindow should host a QPdfView with a document");
    QPdfDocument *qpdf = pdfView->document();

    // Use a portrait row that is NOT the selection (page 0 is selected on
    // open) for the width scan, and a landscape row for the short-row check.
    const int portraitIndex = 2; // portrait
    const int landscapeIndex = 1; // landscape

    // Selection/identity survives resize. ADR 0006 justifies setRenderWidth
    // (cache-clear + dataChanged) over setThumbnailSize (a full
    // beginResetModel) precisely so a resize does NOT reset the model and
    // drop row identity. Spy on the model-reset signal across both resizes
    // and assert it never fires — this is the faithful, robust pin of that
    // claim. (A direct currentIndex-equality check is unsuitable here: the
    // sidebar's page-sync timer legitimately drives the thumbnail
    // currentIndex from the document's current page, and the splitter resize
    // also resizes the document view, so currentIndex can shift for reasons
    // unrelated to the model. The reset spy isolates the exact mechanism.)
    QSignalSpy resetSpy(view->model(), &QAbstractItemModel::modelAboutToBeReset);
    // Also confirm a live selection stays a valid index (not invalidated by a
    // reset) across the resizes.
    view->setCurrentIndex(view->model()->index(3, 0));
    QVERIFY(view->currentIndex().isValid());

    // Two sidebar dock widths (200, 360) → two viewport widths (~180 and
    // ~340 after chrome, availW ~168 and ~328). Invoked narrow→wide so the
    // crispness assertion in checkAtWidth can compare pixmap widths.
    checkAtWidth(mw, sidebar, view, qpdf, 200, portraitIndex, landscapeIndex);
    checkAtWidth(mw, sidebar, view, qpdf, 360, portraitIndex, landscapeIndex);

    // No model reset occurred during either resize re-render.
    QVERIFY2(resetSpy.count() == 0,
             qPrintable(QStringLiteral("resize must not reset the model "
                                       "(reset fired %1 times); selection and "
                                       "row identity would be lost")
                            .arg(resetSpy.count())));
    // Selection remains a valid index (row identity preserved, not dropped).
    QVERIFY2(view->currentIndex().isValid(),
             "current selection must remain valid across resizes");
}

// UAT-THUMB-020 — an image document's single thumbnail row tracks the image's
// pixel aspect (a wide image → short row), mirroring the PDF assertions and
// exercising the new ImageDocument::pageSizeHint. Kept lightweight: one wide
// PNG, one width, the same round(availW/aspect)+padding height oracle.
void TestUatThumbnailSidebar::uat_thumb_020_imageDocAspectRow() {
    QVERIFY(m_scratch.isValid());
    // Wide 1600x400 image → aspect 4.0 (much wider than tall).
    QImage img(1600, 400, QImage::Format_RGB32);
    img.fill(Qt::white);
    {
        QPainter p(&img);
        p.setPen(Qt::black);
        p.drawText(img.rect(), Qt::AlignCenter, QStringLiteral("WIDE IMAGE 4:1"));
    }
    const QString pngPath =
        m_scratch.filePath(QStringLiteral("uat_thumb_020_wide.png"));
    QVERIFY(img.save(pngPath, "PNG"));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({pngPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    mw->resize(1100, 800);
    mw->show();
    (void)QTest::qWaitForWindowExposed(mw);
    QApplication::processEvents();

    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    IDocument *doc = dv->currentDocument();
    QVERIFY(doc);
    // A single static image supports thumbnails; if a future build routes
    // single-image docs away from the thumbnail sidebar, skip rather than
    // fail (follow-up: reconsider the image-doc thumbnail sidebar).
    if (!doc->supportsThumbnails())
        QSKIP("image document exposes no thumbnail sidebar");
    // Sanity: the new no-render hint reports the image's pixel size.
    QCOMPARE(doc->pageSizeHint(0), QSizeF(1600, 400));

    auto *sidebar = mw->findChild<Sidebar *>();
    QVERIFY2(sidebar, "MainWindow should host a Sidebar");
    sidebar->setMode(Sidebar::Mode::Pages);
    QApplication::processEvents();

    QListView *view = nullptr;
    for (auto *lv : sidebar->findChildren<QListView *>()) {
        if (qobject_cast<ThumbnailModel *>(lv->model())) {
            view = lv;
            break;
        }
    }
    QVERIFY2(view, "Sidebar should host a QListView backed by ThumbnailModel");
    if (view->model()->rowCount() < 1)
        QSKIP("image document shows no thumbnail rows");

    mw->resizeDocks({sidebar}, {300}, Qt::Horizontal);
    QApplication::processEvents();
    QTest::qWait(250);
    QApplication::processEvents();

    const int vpW = view->viewport()->width();
    const int availW = vpW - 2 * kThumbHorizontalMargin;
    grabTo(sidebar, QStringLiteral("thumbnail-image-doc-%1.png").arg(vpW));

    const QModelIndex idx = view->model()->index(0, 0);
    QVERIFY(idx.isValid());
    const double aspect = 1600.0 / 400.0; // 4.0
    const int expH =
        int(std::lround(availW / aspect)) + 2 * kThumbVerticalPadding;
    const int gotH = view->visualRect(idx).height();
    qInfo().noquote() << "IMAGE-DOC availW" << availW << "aspect" << aspect
                      << "expected" << expH << "got" << gotH;
    QVERIFY2(std::abs(gotH - expH) <= 3,
             qPrintable(QStringLiteral("image row height %1, expected ~%2 "
                                       "(availW=%3, aspect=%4)")
                            .arg(gotH)
                            .arg(expH)
                            .arg(availW)
                            .arg(aspect)));
    // A 4:1 image is a SHORT row — well under a portrait A4's height at this
    // width (round(availW/0.707)+8) and clear of the legacy fixed 108 px.
    QVERIFY2(gotH < 108,
             qPrintable(QStringLiteral("wide image row must be short (%1 px), "
                                       "not the legacy fixed 108 px")
                            .arg(gotH)));
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

    // See tests/test_image_scale.cpp's main() for why this is needed on
    // macOS: QSettings(org, app) defaults to NativeFormat there, which
    // ignores the HOME sandboxing above.
    QSettings::setDefaultFormat(QSettings::IniFormat);
    Application app(argc, argv);
    TestUatThumbnailSidebar tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_uat_thumbnail_sidebar.moc"
