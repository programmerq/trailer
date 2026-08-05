// UAT harness — Viewer (navigation)
//
// Drives the Application + MainWindow in-process under
// QT_QPA_PLATFORM=offscreen. Each slot maps to a case in
// docs/uat/02-viewer.md; the slot name ends with the UAT ID so a
// failure points straight at the spec case.
//
// Today this covers continuous-mode keyboard navigation. It is the
// natural home for future viewer-navigation pins (single-page
// next/prev, wheel scroll, zoom-step) as they are added.
//
// The binary is labelled `uat` in CTest: normal CI runs `-LE uat` to
// skip it; UAT runs `-L uat`.

#include "app/Application.h"
#include "document/IDocument.h"
#include "document/ImageAdapter.h"
#include "settings/DocumentTypeDefaults.h"
#include "ui/DocumentView.h"
#include "ui/MainWindow.h"

#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QKeyEvent>
#include <QMainWindow>
#include <QPageSize>
#include <QPainter>
#include <QPdfView>
#include <QPdfWriter>
#include <QRect>
#include <QScopeGuard>
#include <QScrollBar>
#include <QString>
#include <QSettings>
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

// Multi-page A4 PDF, one page-numbered line per page so the stacked
// pages have real content and a real combined height in MultiPage mode.
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

// Same as writeSamplePdf, but the page number is drawn large enough to
// read in a grabbed window shot — the G2 evidence pair is worthless if a
// reviewer cannot see WHICH page is on screen.
QString writeBigNumberedPdf(const QString &path, int pages) {
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    QPainter p(&writer);
    QFont f = p.font();
    f.setPointSize(96);
    f.setBold(true);
    p.setFont(f);
    for (int i = 0; i < pages; ++i) {
        p.drawText(QRect(0, 0, writer.width(), writer.height()), Qt::AlignCenter,
                   QStringLiteral("Page %1").arg(i + 1));
        if (i < pages - 1)
            writer.newPage();
    }
    p.end();
    return path;
}

} // namespace

class TestUatViewer : public QObject {
    Q_OBJECT
  private slots:
    void init();

    void uat_vwr_025_continuousArrowStepsByViewport();
    void uat_vwr_101_smallImageIgnoresStalePerTypeZoom();
    void uat_vwr_102_smallImageIgnoresStalePerTypeWindowGeometry();
    void uat_vwr_111_resizingTheWindowKeepsTheCurrentPage();
    void uat_vwr_111_nonRescalingResizeLeavesTheViewAlone();
    void uat_vwr_111_resizeKeepsPageEvidence();

  private:
    QTemporaryDir m_scratch;
};

void TestUatViewer::init() {
    for (auto *w : QApplication::topLevelWidgets()) {
        if (qobject_cast<MainWindow *>(w))
            w->close();
    }
    QApplication::processEvents();
}

// UAT-VWR-025 — Continuous-mode Down/Up/Space step by a screenful.
//
// In continuous (MultiPage) layout, QPdfView would otherwise delegate
// the arrow keys to QAbstractScrollArea, which scrolls a tiny
// line-step — so crossing a page on a long document takes dozens to
// hundreds of presses. NavigablePdfView remaps Down/Up (and Space,
// which follows Down) to a viewport-height step. PageDown/PageUp are
// deliberately left alone so they keep firing MainWindow's
// Next/Previous Page shortcuts. This pins that: one Down advances the
// vertical scrollbar by roughly its pageStep (a bounded, screenful-sized
// band, far more than a single line-step); Space does the same; Up
// reverses it.
void TestUatViewer::uat_vwr_025_continuousArrowStepsByViewport() {
    QVERIFY(m_scratch.isValid());
    const QString pdfPath =
        writeSamplePdf(m_scratch.filePath(QStringLiteral("uat_vwr_025.pdf")), 8);

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    mw->resize(900, 700);
    mw->show();
    QApplication::processEvents();

    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    IDocument *doc = dv->currentDocument();
    QVERIFY(doc);
    QVERIFY2(doc->supportsViewModes(), "PDF documents support view modes");

    auto *view = mw->findChild<QPdfView *>();
    QVERIFY2(view && view->document(),
             "MainWindow should host a QPdfView with a document");

    // Switch to continuous layout the same way View -> Continuous does.
    doc->setViewMode(ViewMode::Continuous);
    QApplication::processEvents();
    QCOMPARE(view->pageMode(), QPdfView::PageMode::MultiPage);

    // Fit-to-width stacks 8 A4 pages several screens tall, guaranteeing
    // a real vertical scroll range to step through. Pin the horizontal
    // scrollbar off so the viewport height (hence pageStep) stays stable
    // as pages lay out — otherwise an appearing/disappearing horizontal
    // bar jitters the page-step by a scrollbar-thickness mid-scroll.
    view->setZoomMode(QPdfView::ZoomMode::FitToWidth);
    view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    QApplication::processEvents();

    QScrollBar *vbar = view->verticalScrollBar();
    QVERIFY(vbar);
    const int pageStep = vbar->pageStep();
    const int singleStep = vbar->singleStep();
    QVERIFY2(pageStep > singleStep,
             "viewport page-step must exceed the line single-step");

    // QPdfView lays out pages lazily, so the scroll range near the top
    // reflects only the first few pages — a second page-step from the
    // top would otherwise hit that premature ceiling. Drive to the
    // bottom repeatedly until the maximum stops growing, forcing every
    // page to be sized.
    int prevMax = -1;
    for (int i = 0; i < 16 && vbar->maximum() != prevMax; ++i) {
        prevMax = vbar->maximum();
        vbar->setValue(vbar->maximum());
        QApplication::processEvents();
    }
    const int fullMax = vbar->maximum();
    QVERIFY2(fullMax > pageStep,
             "continuous 8-page view must scroll more than one screenful");

    // Deliver key press+release straight to the view's event handler.
    // sendEvent (rather than QTest::keyClick) avoids depending on focus
    // or window activation, which is unreliable under the offscreen
    // platform; the keyPressEvent override keys off pageMode() alone.
    auto sendKey = [view](int key) {
        QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
        QApplication::sendEvent(view, &press);
        QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
        QApplication::sendEvent(view, &release);
        QApplication::processEvents();
    };

    // Assert the *intent*: each press advances by roughly one page-step
    // (a screenful) and far more than the line single-step — the bug was
    // arrow keys moving by a single line. Each press starts from a fresh
    // position with room to move so a document edge can't truncate the
    // step; the step is read fresh (the same instant the handler reads
    // it) and a quarter-page tolerance absorbs lazy-layout jitter while
    // still rejecting a line-step (or half-step) regression.

    // Down from the top.
    {
        vbar->setValue(vbar->minimum());
        QApplication::processEvents();
        const int step = vbar->pageStep();
        const int v0 = vbar->value();
        sendKey(Qt::Key_Down);
        const int delta = vbar->value() - v0;
        QVERIFY2(delta > singleStep * 3 && delta <= step + 8,
                 qPrintable(QStringLiteral("Down delta %1 must be a screenful-sized step "
                                           "(line single-step %2, page-step %3)")
                                .arg(delta)
                                .arg(singleStep)
                                .arg(step)));
    }

    // Space from the top (follows Down for a consistent advance key).
    {
        vbar->setValue(vbar->minimum());
        QApplication::processEvents();
        const int step = vbar->pageStep();
        const int v0 = vbar->value();
        sendKey(Qt::Key_Space);
        const int delta = vbar->value() - v0;
        QVERIFY2(delta > singleStep * 3 && delta <= step + 8,
                 qPrintable(QStringLiteral("Space delta %1 must be a screenful-sized step "
                                           "(line single-step %2, page-step %3)")
                                .arg(delta)
                                .arg(singleStep)
                                .arg(step)));
    }

    // Up from the bottom (a full screenful of room above to step into).
    {
        vbar->setValue(vbar->maximum());
        QApplication::processEvents();
        const int step = vbar->pageStep();
        const int v0 = vbar->value();
        sendKey(Qt::Key_Up);
        const int delta = v0 - vbar->value();
        QVERIFY2(delta > singleStep * 3 && delta <= step + 8,
                 qPrintable(QStringLiteral("Up delta %1 must be a screenful-sized step "
                                           "(line single-step %2, page-step %3)")
                                .arg(delta)
                                .arg(singleStep)
                                .arg(step)));
    }
}

// UAT-VWR-101 — Small image ignores a stale per-type Custom zoom
// default. Owner dogfooding report (2026-07-31): a fresh 504x375 JPEG
// opened at "Zoom: Custom (80%)" because an earlier, unrelated, larger
// image's manual zoom had become the per-type Image default. See DR
// 2026-07-31-per-type-restore-excludes-content-relative-state.
void TestUatViewer::uat_vwr_101_smallImageIgnoresStalePerTypeZoom() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);

    const auto resetTypeDefault = qScopeGuard([app]() {
        app->documentTypeDefaults().setForType(DocumentType::Image, DocumentTypeDefault{});
    });
    DocumentTypeDefault poisoned;
    poisoned.zoomMode = ZoomMode::Custom;
    poisoned.zoomFactor = 0.8;
    app->documentTypeDefaults().setForType(DocumentType::Image, poisoned);

    QImage img(504, 375, QImage::Format_RGB32);
    img.fill(qRgb(210, 220, 230));
    const QString jpgPath = m_scratch.filePath(QStringLiteral("uat_vwr_101.jpg"));
    QVERIFY(img.save(jpgPath, "JPEG"));

    app->openFiles({jpgPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    mw->show();

    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    auto *doc = dynamic_cast<ImageDocument *>(dv->currentDocument());
    QVERIFY(doc);

    QElapsedTimer timer;
    timer.start();
    while (doc->zoomMode() == ZoomMode::Custom && timer.elapsed() < 2000) {
        QTest::qWait(20);
    }

    QCOMPARE(doc->zoomMode(), ZoomMode::Actual);
    QVERIFY2(std::abs(doc->zoomFactor() - 1.0) < 1e-6,
             qPrintable(QStringLiteral("expected natural Actual Size (100%%), got %1%%")
                            .arg(doc->zoomFactor() * 100.0)));
}

// UAT-VWR-102 — Small image ignores a stale per-type window-geometry
// default. Same report as UAT-VWR-101: the window also opened HUGE
// because an unrelated image's window geometry had become the per-type
// Image default, clobbering applyInitialWindowSize()'s already-computed
// content-fit size.
void TestUatViewer::uat_vwr_102_smallImageIgnoresStalePerTypeWindowGeometry() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);

    const auto resetTypeDefault = qScopeGuard([app]() {
        app->documentTypeDefaults().setForType(DocumentType::Image, DocumentTypeDefault{});
    });

    QMainWindow probe;
    probe.resize(2400, 1500);
    const QByteArray hugeGeometry = probe.saveGeometry();
    QVERIFY(!hugeGeometry.isEmpty());

    DocumentTypeDefault poisoned;
    poisoned.windowGeometry = hugeGeometry;
    app->documentTypeDefaults().setForType(DocumentType::Image, poisoned);

    QImage img(504, 375, QImage::Format_RGB32);
    img.fill(qRgb(210, 220, 230));
    const QString jpgPath = m_scratch.filePath(QStringLiteral("uat_vwr_102.jpg"));
    QVERIFY(img.save(jpgPath, "JPEG"));

    app->openFiles({jpgPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    mw->show();
    QApplication::processEvents();

    // Same-environment comparison (see the unit-test counterpart in
    // tests/test_image_scale.cpp for the full rationale): the offscreen
    // platform's own small, fixed virtual screen makes an absolute pixel
    // threshold unable to discriminate "restored the huge geometry" from
    // "computed a content-fit size", since both get clamped by the tiny
    // screen. Measure what restoring the poisoned geometry directly
    // produces under this same screen and assert the real window
    // doesn't match it.
    QMainWindow referenceRestore;
    referenceRestore.restoreGeometry(hugeGeometry);
    referenceRestore.show();
    QApplication::processEvents();

    QVERIFY2(mw->size() != referenceRestore.size(),
             qPrintable(QStringLiteral("a 504x375 image must not inherit an unrelated "
                                       "document's huge per-type window geometry; got %1x%2, "
                                       "matching a direct restore of the poisoned geometry")
                            .arg(mw->width())
                            .arg(mw->height())));
}

// UAT-VWR-111 — Resizing the window keeps you on the page you were
// reading. Resizing is not a navigation command.
//
// The defect this pins, measured on Linux/offscreen at origin/main
// 6606081: in continuous layout at a FIT zoom, QPdfView re-lays-out on
// every viewport resize and rescales every page, but the vertical
// scrollbar keeps its ABSOLUTE pixel value across that rescale. A
// 720x720 -> 680x690 window nudge moved a 300-page A4 document from
// page 212 to page 223 — an 11-page jump the user never asked for —
// with the scroll value unchanged at 127872 px. Fixed in
// NavigablePdfView::resizeEvent (src/document/PdfAdapter.cpp), which
// re-anchors the page the re-layout moved.
//
// Surfaced by uat_fnd_095_restoredPageStaysPutAfterLaterLayout failing
// on the gating Linux nightly lane. That slot reaches the same defect
// through quit/reopen, which entangles it with the restore path; this
// one isolates the resize itself, with no restore involved, so a future
// failure names the mechanism directly.
void TestUatViewer::uat_vwr_111_resizingTheWindowKeepsTheCurrentPage() {
    QVERIFY(m_scratch.isValid());
    // Deep enough into a long document that a proportional drift is
    // pages, not rounding: the pre-fix miss was ~5% of the page index.
    constexpr int kPages = 200;
    constexpr int kTargetPage = 140;
    const QString pdfPath =
        writeSamplePdf(m_scratch.filePath(QStringLiteral("uat_vwr_111.pdf")), kPages);

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    mw->resize(900, 700);
    mw->show();
    QApplication::processEvents();

    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    IDocument *doc = dv->currentDocument();
    QVERIFY(doc);

    auto *view = mw->findChild<QPdfView *>();
    QVERIFY2(view && view->document(), "MainWindow should host a QPdfView with a document");

    QScrollBar *vbar = view->verticalScrollBar();
    QVERIFY(vbar);

    // A zoom-mode change re-lays-out over later event-loop turns, and
    // navigating before it has settled computes the target page's offset
    // against the OLD layout — the scrollbar then keeps that stale pixel
    // value (measured: goToPage(140) straight after zoomFitWidth() left
    // the view at 51% of the document, page 104). Wait for the scroll
    // range to stop moving before navigating, so the precondition this
    // slot sets up is real rather than reported.
    auto settleLayout = [&vbar]() {
        int previous = -1;
        for (int i = 0; i < 16 && vbar->maximum() != previous; ++i) {
            previous = vbar->maximum();
            QApplication::processEvents();
        }
    };

    // Set both halves of the precondition explicitly rather than
    // inheriting whatever the first-open heuristic picked: continuous
    // layout (so the reported page tracks the scroll offset) and a fit
    // zoom (the only modes whose layout rescales with the viewport).
    doc->setViewMode(ViewMode::Continuous);
    doc->zoomFitPage();
    settleLayout();
    QCOMPARE(view->pageMode(), QPdfView::PageMode::MultiPage);
    QCOMPARE(doc->zoomMode(), ZoomMode::FitInView);

    doc->goToPage(kTargetPage);
    QApplication::processEvents();
    QCOMPARE(doc->currentPage(), kTargetPage);

    const int maxBefore = vbar->maximum();

    mw->resize(mw->width() - 40, mw->height() - 30);
    QApplication::processEvents();

    // Guard against a vacuous pass: if a platform ever stops re-laying
    // the document out on resize, "the page didn't move" would be true
    // for the wrong reason and this slot would silently stop testing
    // anything. The scroll range changing is the observable proof that
    // the rescale under test actually happened.
    QVERIFY2(vbar->maximum() != maxBefore,
             qPrintable(QStringLiteral("the resize must actually re-lay-out the document "
                                       "(scroll range stayed at %1) or this slot proves nothing")
                            .arg(maxBefore)));

    QCOMPARE(doc->currentPage(), kTargetPage);
    // Re-anchoring must not cost the fit mode that caused the rescale —
    // the view has to keep re-fitting on the NEXT resize too.
    QCOMPARE(doc->zoomMode(), ZoomMode::FitInView);
}

// UAT-VWR-111, second half — a resize that does NOT rescale must leave
// the view completely alone, so the anchor above can never become its
// own source of unrequested movement.
//
// Under Fit-Width a height-only resize changes no page's size, and the
// anchor is gated on exactly that: the laid-out document height (scroll
// maximum + page step) is invariant to a pure viewport-height change,
// because the maximum grows by whatever the page step loses. Without
// that gate the anchor would scroll the document merely because the
// viewport-relative line QPdfView derives "current page" from had
// crossed a page boundary — moving a document the reader could see was
// already still. This pins both the invariant the gate reads and the
// outcome it protects.
void TestUatViewer::uat_vwr_111_nonRescalingResizeLeavesTheViewAlone() {
    QVERIFY(m_scratch.isValid());
    constexpr int kPages = 200;
    constexpr int kTargetPage = 140;
    const QString pdfPath =
        writeSamplePdf(m_scratch.filePath(QStringLiteral("uat_vwr_111_fw.pdf")), kPages);

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    mw->resize(900, 700);
    mw->show();
    QApplication::processEvents();

    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    IDocument *doc = dv->currentDocument();
    QVERIFY(doc);
    auto *view = mw->findChild<QPdfView *>();
    QVERIFY(view && view->document());
    QScrollBar *vbar = view->verticalScrollBar();
    QVERIFY(vbar);

    // Choose the zoom mode BEFORE navigating anywhere. Navigating first
    // and switching mode after would leave the page navigator reporting
    // the old page while the scroll offset means a different one — a
    // separate, pre-existing defect, filed as
    // docs/backlog/2026-08-05-zoom-mode-change-drops-the-current-page.md
    // and deliberately not entangled with this slot.
    doc->setViewMode(ViewMode::Continuous);
    doc->zoomFitWidth();
    for (int i = 0, previous = -1; i < 16 && vbar->maximum() != previous; ++i) {
        previous = vbar->maximum();
        QApplication::processEvents();
    }
    QCOMPARE(doc->zoomMode(), ZoomMode::FitToWidth);

    doc->goToPage(kTargetPage);
    QApplication::processEvents();
    QCOMPARE(doc->currentPage(), kTargetPage);

    // Park the reader just past the top of the target page, so the
    // viewport-relative line QPdfView reads "current page" off sits close
    // enough to the page boundary that losing kShrink px of viewport
    // height carries it over — i.e. the reported page flips WITHOUT the
    // document having moved at all. That is the exact case the gate has
    // to sit out; without it this slot's scroll assertion fails.
    constexpr int kShrink = 200;
    const int pageTop = vbar->value();
    const int lineOffset = vbar->pageStep() * 2 / 5; // QPdfView reads at 40%
    vbar->setValue(pageTop - lineOffset + 40);
    QApplication::processEvents();
    QCOMPARE(doc->currentPage(), kTargetPage);

    const int laidOutBefore = vbar->maximum() + vbar->pageStep();
    const int valueBefore = vbar->value();

    mw->resize(mw->width(), mw->height() - kShrink);
    QApplication::processEvents();

    // A pure height change under Fit-Width rescales nothing: whatever the
    // page step loses, the scroll maximum gains.
    QCOMPARE(vbar->maximum() + vbar->pageStep(), laidOutBefore);
    // The point of the slot: the document did not move a pixel.
    QCOMPARE(vbar->value(), valueBefore);
    // Setup guard, so this can never pass vacuously: the scenario is only
    // meaningful while the resize really does move the reported page
    // across a boundary. Flipping the READOUT with the document still is
    // truthful — it names what is now under the line — and is the
    // deliberate trade: correcting it would scroll a document the reader
    // can see is not moving.
    QVERIFY2(doc->currentPage() != kTargetPage,
             "this slot only proves anything while the height change moves the "
             "reported page; re-tune the parking offset if QPdfView's rule changed");
}

// UAT-VWR-111 — curated G2 before/after evidence for the resize anchor.
//
// Uses ONLY pre-existing public API, so this identical slot builds and
// runs against a tree WITHOUT the fix as well as with it — which is how
// the BEFORE shot is produced (stash src/document/PdfAdapter.cpp,
// rebuild, run; then restore and run again for AFTER). Both shots are
// the same document, the same window, and the same resize, so the only
// difference on the page is the defect.
//
// The filename is chosen from the OBSERVED page rather than passed in,
// so a shot can never be mislabelled as the run it wasn't.
//
// No-op unless $TRAILER_RESIZE_ANCHOR_EVIDENCE_DIR is set (mirrors
// uat_fnd_094_quitPageRestoreEvidence / test_uat_deference_evidence).
void TestUatViewer::uat_vwr_111_resizeKeepsPageEvidence() {
    const QString dir = QString::fromLocal8Bit(qgetenv("TRAILER_RESIZE_ANCHOR_EVIDENCE_DIR"));
    if (dir.isEmpty())
        QSKIP("Set TRAILER_RESIZE_ANCHOR_EVIDENCE_DIR to emit the G2 evidence pair");
    QVERIFY(m_scratch.isValid());
    QDir().mkpath(dir);

    constexpr int kPages = 200;
    constexpr int kTargetPage = 140; // 0-based; the page prints "Page 141"
    const QString pdfPath =
        writeBigNumberedPdf(m_scratch.filePath(QStringLiteral("uat_vwr_111_evidence.pdf")), kPages);

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    mw->resize(900, 700);
    mw->show();
    QApplication::processEvents();

    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    IDocument *doc = dv->currentDocument();
    QVERIFY(doc);

    doc->setViewMode(ViewMode::Continuous);
    doc->zoomFitPage();
    QApplication::processEvents();
    doc->goToPage(kTargetPage);
    QApplication::processEvents();
    QCOMPARE(doc->currentPage(), kTargetPage);

    // The one action under test: nudge the window smaller.
    mw->resize(mw->width() - 40, mw->height() - 30);
    QApplication::processEvents();

    const bool anchored = doc->currentPage() == kTargetPage;
    const QString name = anchored ? QStringLiteral("2026-08-05-resize-keeps-page-after.png")
                                  : QStringLiteral("2026-08-05-resize-keeps-page-before.png");
    const QPixmap pm = mw->grab();
    QVERIFY(!pm.isNull());
    QVERIFY2(pm.save(QDir(dir).filePath(name), "PNG"), qPrintable(name));
    qInfo().noquote() << "G2-SCREENSHOT" << QDir(dir).filePath(name)
                      << "— asked for page" << (kTargetPage + 1) << ", showing page"
                      << (doc->currentPage() + 1);
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
    TestUatViewer tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_uat_viewer.moc"
