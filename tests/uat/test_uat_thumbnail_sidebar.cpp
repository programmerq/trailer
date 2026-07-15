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
#include <QImage>
#include <QListView>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfDocument>
#include <QPdfView>
#include <QPdfWriter>
#include <QPixmap>
#include <QSizeF>
#include <QString>
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

// Build a mixed-orientation PDF: alternating A4 PORTRAIT and A4 LANDSCAPE
// pages. Even indices (0,2,4) are portrait, odd indices (1,3,5) landscape.
// Each page carries a page-numbered line so the thumbnails are visually
// distinct. QPdfWriter honours per-page orientation via setPageOrientation
// before newPage().
QString writeMixedPdf(const QString &path, int pages) {
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setPageOrientation(QPageLayout::Portrait);
    QPainter p(&writer);
    for (int i = 0; i < pages; ++i) {
        if (i > 0) {
            writer.setPageOrientation((i % 2 == 0) ? QPageLayout::Portrait
                                                   : QPageLayout::Landscape);
            writer.newPage();
        }
        p.drawText(QRect(50, 50, 700, 200), Qt::AlignCenter,
                   QStringLiteral("Page %1 %2")
                       .arg(i + 1)
                       .arg(i % 2 == 0 ? QStringLiteral("PORTRAIT")
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

  private:
    // Drive one sidebar width: resize the dock, settle the debounce, capture
    // evidence, and assert the per-row height oracle + a paper-width scan.
    void checkAtWidth(MainWindow *mw, Sidebar *sidebar, QListView *view,
                      QPdfDocument *qpdf, int dockWidth, int portraitIndex,
                      int landscapeIndex);

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
    bool widthScanUsable = scanY >= 0 && scanY < vp.height() && vp.width() > 8;
    if (widthScanUsable) {
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
        qInfo().noquote() << "WIDTH-SCAN scanY" << scanY << "left" << left
                          << "right" << right << "extent" << extent << "availW"
                          << availW;
        // Generous tolerance; the thumbnail must span most of the column.
        QVERIFY2(extent >= availW - 6,
                 qPrintable(QStringLiteral("thumbnail paper extent %1 px, "
                                           "expected >= %2 (availW=%3)")
                                .arg(extent)
                                .arg(availW - 6)
                                .arg(availW)));
    } else {
        qInfo().noquote() << "WIDTH-SCAN skipped (band off-screen)";
    }
}

// UAT-THUMB-010 — sidebar thumbnails scale to the column width and rows fit
// each page's aspect, at two distinct sidebar widths.
void TestUatThumbnailSidebar::uat_thumb_010_scaleToWidthAndAspectRows() {
    QVERIFY(m_scratch.isValid());
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

    // Two sidebar widths → two viewport widths (~160 and ~320 after chrome).
    checkAtWidth(mw, sidebar, view, qpdf, 200, portraitIndex, landscapeIndex);
    checkAtWidth(mw, sidebar, view, qpdf, 360, portraitIndex, landscapeIndex);
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
    TestUatThumbnailSidebar tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_uat_thumbnail_sidebar.moc"
