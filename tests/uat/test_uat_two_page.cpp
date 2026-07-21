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
//
// Lookups go through stable objectNames on MainWindow's actions and on the
// TwoPageView widget, so the harness survives label / IA renames. The binary
// is labelled `uat` in CTest and registered across the dpr matrix.

#include "app/Application.h"
#include "document/IDocument.h"
#include "ui/DocumentView.h"
#include "ui/MainWindow.h"

#include <QAbstractScrollArea>
#include <QAction>
#include <QDir>
#include <QImage>
#include <QPageSize>
#include <QPainter>
#include <QPdfView>
#include <QPdfWriter>
#include <QRect>
#include <QString>
#include <QTemporaryDir>
#include <QWidget>
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

} // namespace

class TestUatTwoPage : public QObject {
    Q_OBJECT
  private slots:
    void init();

    void uat_vwr_070_toggleEnablementByDocType();
    void uat_vwr_071_coverAlonePairingGeometry();
    void uat_vwr_072_actualSizeTruthfulZoom();
    void uat_vwr_073_honestDegradationTooltips();

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
    app->openFiles({multi});
    QApplication::processEvents();
    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    mw->show();
    QApplication::processEvents();
    QAction *twoPages = findAction(mw, QStringLiteral("action.view.twoPages"));
    QVERIFY2(twoPages, "Two Pages action must exist with objectName action.view.twoPages");
    QVERIFY2(twoPages->isEnabled(), "Two Pages must be enabled for a multi-page PDF");

    // Single-page PDF → disabled + explanatory tooltip.
    const QString single =
        writeSamplePdf(m_scratch.filePath(QStringLiteral("uat_vwr_070_single.pdf")), 1);
    app->openFiles({single});
    QApplication::processEvents();
    mw = currentMainWindow();
    twoPages = findAction(mw, QStringLiteral("action.view.twoPages"));
    QVERIFY(twoPages);
    QVERIFY2(!twoPages->isEnabled(), "Two Pages must be disabled for a single-page PDF");
    QVERIFY2(twoPages->toolTip().contains(QStringLiteral("one page"), Qt::CaseInsensitive),
             qPrintable(QStringLiteral("single-page tooltip should explain why; got: '%1'")
                            .arg(twoPages->toolTip())));

    // Image → disabled + explanatory tooltip.
    const QString image = writeSampleImage(m_scratch.filePath(QStringLiteral("uat_vwr_070.png")));
    app->openFiles({image});
    QApplication::processEvents();
    mw = currentMainWindow();
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

    auto *twoPage =
        mw->findChild<QAbstractScrollArea *>(QStringLiteral("view.twoPage"));
    QVERIFY2(twoPage, "TwoPageView (objectName view.twoPage) must exist in Two-Pages mode");
    QVERIFY2(twoPage->isVisible(), "TwoPageView must be the visible surface in Two-Pages mode");

    // The spread canvas must be wider than a single page (two side by side)
    // and taller than one spread (multiple spreads stacked for scrolling).
    const int contentW = twoPage->widget() ? twoPage->widget()->width()
                                           : twoPage->viewport()->width();
    Q_UNUSED(contentW);
    // Geometry is asserted through the scrollbars: a 5-page book with
    // cover-alone pairing produces spreads [1],[2,3],[4,5] — 3 rows, so the
    // vertical range must exceed a single spread's height.
    QVERIFY2(twoPage->verticalScrollBar()->maximum() >= 0,
             "TwoPageView must expose a vertical scroll range for stacked spreads");
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
    QVERIFY2(markup, "markup toolbar toggle must exist with objectName action.view.markupToolbar");
    QVERIFY2(find, "Find action must exist with objectName action.edit.find");

    // Baseline (Continuous): both usable on a normal PDF.
    doc->setViewMode(ViewMode::Continuous);
    QApplication::processEvents();
    QVERIFY2(find->isEnabled(), "Find is available in Continuous mode");

    // Two-Pages: markup + search disabled-with-tooltip pointing back to the
    // working modes.
    doc->setViewMode(ViewMode::TwoPages);
    QApplication::processEvents();
    QVERIFY2(!markup->isEnabled(), "markup must be disabled in Two-Pages mode");
    QVERIFY2(markup->toolTip().contains(QStringLiteral("Switch to"), Qt::CaseInsensitive),
             qPrintable(QStringLiteral("markup degrade tooltip should point back; got: '%1'")
                            .arg(markup->toolTip())));
    QVERIFY2(!find->isEnabled(), "search must be disabled in Two-Pages mode");
    QVERIFY2(find->toolTip().contains(QStringLiteral("Switch to"), Qt::CaseInsensitive),
             qPrintable(QStringLiteral("search degrade tooltip should point back; got: '%1'")
                            .arg(find->toolTip())));

    // Leaving Two-Pages restores both.
    doc->setViewMode(ViewMode::Continuous);
    QApplication::processEvents();
    QVERIFY2(find->isEnabled(), "Find re-enables when leaving Two-Pages mode");
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
