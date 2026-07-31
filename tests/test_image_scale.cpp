// Unit test — HiDPI-correct image display (P1 "screenshot opens blurry
// / oversized").
//
// A Retina screenshot is device pixels stamped devicePixelRatio=1 (e.g.
// 2880x1800 px for a 1440x900-pt display). Before the fix the viewer
// treated those raw device pixels as logical pixels, so:
//   * the window opened at ~2x logical size (contentSizeHint returned
//     device px), then fit-shrank ("arbitrary size then fit"),
//   * "Actual Size" produced a dpr=1 pixmap that Qt then re-scaled by
//     the screen's 2x on paint — a double resample, permanently blurry,
//   * fit computed against device px, so a screenshot that fits 1:1
//     logically was shrunk to 50%.
//
// The fix stamps the capture's real devicePixelRatio on the decoded
// image and drives all display maths in LOGICAL units, mirroring the
// scale-to-logical technique PR #70 added in src/ui/ThumbnailPaint.h.
// This pins the required post-fix behavior. dpr is injected on a
// synthetic QImage via setDevicePixelRatio() — the PR #70 pattern — so
// the HiDPI path is exercised deterministically on a dpr=1 display /
// offscreen platform.

#include "app/Application.h"
#include "document/IDocument.h"
#include "document/ImageAdapter.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QPointF>
#include <QScopeGuard>
#include <QScrollArea>
#include <QSettings>
#include <QTemporaryDir>
#include <QWidget>
#include <QtTest/QtTest>

#include <cmath>
#include <cstdio>

using namespace trailer;

namespace {

// A synthetic decoded image as a capture would produce it once the fix
// stamps dpr: raw device pixels = logical * dpr, with dpr stamped.
QImage makeDprImage(int deviceW, int deviceH, qreal dpr) {
    QImage img(deviceW, deviceH, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::white);
    img.setDevicePixelRatio(dpr);
    return img;
}

// Pump the event loop in short slices until `pred` holds or `budgetMs`
// elapses, then return pred()'s final value. Replaces bare
// qWait(N)-then-assert so a test does not depend on a fixed delay Wine
// (block-buffered stdout, slower layout) may not meet: the async
// initial-fit singleShot and the viewport layout land on the loop, and
// this waits for them deterministically instead of guessing a duration.
template <typename Pred>
bool pumpUntil(Pred pred, int budgetMs = 2000) {
    QElapsedTimer timer;
    timer.start();
    while (!pred() && timer.elapsed() < budgetMs)
        QTest::qWait(20);
    return pred();
}

// Scan every window the Application currently owns and return the most
// recently added ImageDocument (used by the consume-and-reset contract
// test, which drives real Application::openFiles calls).
ImageDocument *newestImageDoc(Application *app) {
    ImageDocument *found = nullptr;
    for (MainWindow *w : app->windows()) {
        if (!w)
            continue;
        for (int i = 0; i < w->documentCount(); ++i) {
            IDocument *d = nullptr;
            if (w->documentAt(i, &d) == 1 && d) {
                if (auto *img = dynamic_cast<ImageDocument *>(d))
                    found = img;
            }
        }
    }
    return found;
}

} // namespace

class TestImageScale : public QObject {
    Q_OBJECT
  private slots:
    // Runs after EVERY test function (QtTest's per-test teardown hook).
    // See the definition below for why this exists.
    void cleanup();

    void contentSizeHintIsLogical_data();
    void contentSizeHintIsLogical();
    void actualSizeIsPixelExact_data();
    void actualSizeIsPixelExact();
    void scaleIsInLogicalUnits_data();
    void scaleIsInLogicalUnits();
    void fitUsesLogicalDims();
    void captureOriginDefaultsToActual_data();
    void captureOriginDefaultsToActual();
    void ordinaryOpenKeepsFit();
    void smallImageResizeDoesNotUpscale();
    void readoutMatchesRenderAfterAsyncFit();
    void reapplyFitModeNotifiesOnEveryRescale();
    void reapplyFitModeSkipsNotifyWhenScaleUnchanged();
    void applyInitialFitZoomForcesDecodeForCaptureOrigin();
    void applyZoomStateSentinelLeavesInitialFitUndecided();
    void captureOriginIgnoresPersistedTypeDefault();
    void pngRoundTripStripsDprThenRecovers();
    void pendingCaptureDprConsumedOncePerBatch();
    void coordinateRoundTripInvertsAtDpr2();
    void resampleBranchRestampsDpr();

    // 2026-07-31 owner dogfooding report: a fresh 504x375 image opened
    // at a stale per-type "Custom 80%" zoom in a huge window. See DR
    // 2026-07-31-per-type-restore-excludes-content-relative-state.
    void ordinaryOpenIgnoresPersistedCustomTypeDefaultZoom();
    void ordinaryOpenIgnoresPersistedTypeDefaultWindowGeometry();
    void closingAtCustomZoomDoesNotPoisonTypeDefault();
    void smallReportedImageNaturalFitIsActual_data();
    void smallReportedImageNaturalFitIsActual();
};

void TestImageScale::cleanup() {
    // readoutMatchesRenderAfterAsyncFit() and
    // pendingCaptureDprConsumedOncePerBatch() drive real
    // Application::openFiles() calls, which create genuine MainWindows
    // with a live, armed FitModeResizeWatcher on each open document's
    // viewport -- neither test closed its window afterward, so it stayed
    // alive (and its resize watcher stayed installed and reactive) for
    // the rest of the process's lifetime, spanning every later test
    // function in this file. That is a real cross-test isolation gap: a
    // leftover watcher reacting to a LATER test's window creation/layout
    // churn is a plausible, if unconfirmed, contributor to timing-
    // sensitive behaviour differences between platforms (see the
    // 2026-07-26 investigation into a macOS-only
    // pendingCaptureDprConsumedOncePerBatch failure that could not be
    // reproduced locally on Linux). Destroying every leftover window
    // here, after every test, closes that gap for the whole file.
    //
    // `delete` (not `close()`) tears the window down directly: `close()`
    // would run MainWindow::closeEvent(), which persists RecentFiles /
    // DocumentTypeDefaults state -- exactly the kind of cross-test
    // contamination this cleanup exists to PREVENT, not introduce. A
    // no-op for the majority of tests here, which build an ImageDocument
    // directly and never touch Application::openFiles().
    auto *app = qobject_cast<Application *>(qApp);
    if (!app)
        return;
    const QList<MainWindow *> windows = app->windows();
    for (MainWindow *w : windows) {
        delete w;
    }
}

void TestImageScale::contentSizeHintIsLogical_data() {
    QTest::addColumn<int>("deviceW");
    QTest::addColumn<int>("deviceH");
    QTest::addColumn<qreal>("dpr");
    QTest::newRow("dpr1") << 1440 << 900 << qreal(1.0);
    QTest::newRow("dpr1.5") << 2160 << 1350 << qreal(1.5);
    QTest::newRow("dpr2") << 2880 << 1800 << qreal(2.0);
}

void TestImageScale::contentSizeHintIsLogical() {
    QFETCH(int, deviceW);
    QFETCH(int, deviceH);
    QFETCH(qreal, dpr);

    ImageDocument doc{QString()};
    doc.setImageForTest(makeDprImage(deviceW, deviceH, dpr));

    // The window is sized from contentSizeHint(); it must be the LOGICAL
    // size (device px / dpr) so a Retina screenshot opens at 1440x900,
    // not 2880x1800-then-shrunk.
    const QSize hint = doc.contentSizeHint();
    const int expW = int(std::lround(deviceW / dpr));
    const int expH = int(std::lround(deviceH / dpr));
    QVERIFY2(std::abs(hint.width() - expW) <= 1 && std::abs(hint.height() - expH) <= 1,
             qPrintable(QStringLiteral("dpr=%1: contentSizeHint %2x%3, expected logical %4x%5")
                            .arg(dpr)
                            .arg(hint.width())
                            .arg(hint.height())
                            .arg(expW)
                            .arg(expH)));
}

void TestImageScale::actualSizeIsPixelExact_data() {
    QTest::addColumn<int>("deviceW");
    QTest::addColumn<int>("deviceH");
    QTest::addColumn<qreal>("dpr");
    QTest::newRow("dpr1") << 1440 << 900 << qreal(1.0);
    QTest::newRow("dpr1.5") << 2160 << 1350 << qreal(1.5);
    QTest::newRow("dpr2") << 2880 << 1800 << qreal(2.0);
}

void TestImageScale::actualSizeIsPixelExact() {
    QFETCH(int, deviceW);
    QFETCH(int, deviceH);
    QFETCH(qreal, dpr);

    ImageDocument doc{QString()};
    doc.setImageForTest(makeDprImage(deviceW, deviceH, dpr), /*captureOrigin=*/true);
    QWidget *view = doc.createView(nullptr);
    QVERIFY(view != nullptr);

    doc.zoomActual();

    // At Actual Size the pixmap handed to the label must carry the
    // source dpr and map 1 source device px -> 1 screen device px: no
    // resample. So devicePixelRatio == dpr and the RAW pixel size equals
    // the image's device size exactly.
    const QPixmap pm = doc.labelPixmapForTest();
    QVERIFY(!pm.isNull());
    QCOMPARE(pm.devicePixelRatio(), dpr);
    QCOMPARE(pm.width(), deviceW);
    QCOMPARE(pm.height(), deviceH);

    delete view;
}

void TestImageScale::scaleIsInLogicalUnits_data() {
    QTest::addColumn<int>("deviceW");
    QTest::addColumn<int>("deviceH");
    QTest::addColumn<qreal>("dpr");
    QTest::newRow("dpr1") << 1440 << 900 << qreal(1.0);
    QTest::newRow("dpr1.5") << 2160 << 1350 << qreal(1.5);
    QTest::newRow("dpr2") << 2880 << 1800 << qreal(2.0);
}

void TestImageScale::scaleIsInLogicalUnits() {
    QFETCH(int, deviceW);
    QFETCH(int, deviceH);
    QFETCH(qreal, dpr);

    ImageDocument doc{QString()};
    doc.setImageForTest(makeDprImage(deviceW, deviceH, dpr), /*captureOrigin=*/true);
    QWidget *view = doc.createView(nullptr);
    QVERIFY(view != nullptr);

    doc.zoomActual(); // scale factor 1.0

    // At 100% the DRAWN logical size == deviceWidth / dpr (the pixmap's
    // raw px / its dpr), not deviceWidth.
    const QPixmap pm = doc.labelPixmapForTest();
    QVERIFY(!pm.isNull());
    const qreal pmDpr = pm.devicePixelRatio() > 0 ? pm.devicePixelRatio() : 1.0;
    const int drawnLogicalW = int(std::lround(pm.width() / pmDpr));
    const int drawnLogicalH = int(std::lround(pm.height() / pmDpr));
    const int expW = int(std::lround(deviceW / dpr));
    const int expH = int(std::lround(deviceH / dpr));
    QVERIFY2(std::abs(drawnLogicalW - expW) <= 1 && std::abs(drawnLogicalH - expH) <= 1,
             qPrintable(QStringLiteral("dpr=%1: drawn logical %2x%3, expected %4x%5")
                            .arg(dpr)
                            .arg(drawnLogicalW)
                            .arg(drawnLogicalH)
                            .arg(expW)
                            .arg(expH)));

    delete view;
}

void TestImageScale::fitUsesLogicalDims() {
    // Fit must divide the LOGICAL viewport by the LOGICAL image size, not
    // by device pixels. Two invariants that hold for any (shared) viewport:
    //   1. Two images with the SAME logical size fit to the SAME scale,
    //      regardless of dpr.
    //   2. A dpr=2 image (logical = half the device px) fits at exactly
    //      2x the scale of a dpr=1 image of the same DEVICE size.
    // Sentinel returned when the shown viewport never lays out under the
    // offscreen platform; distinct from the -1.0 wrong-view-type sentinel so
    // the caller can tell "unsettled" (skip) from a genuine failure.
    constexpr double kUnsettled = -2.0;
    auto fitScaleFor = [kUnsettled](const QImage &img) -> double {
        auto *doc = new ImageDocument(QString());
        doc->setImageForTest(img);
        QWidget *view = doc->createView(nullptr);
        auto *scroll = qobject_cast<QScrollArea *>(view);
        if (!scroll) {
            // createView must yield a QScrollArea; a null here would
            // segfault on the resize below in a Release build (Q_ASSERT is
            // compiled out under -DNDEBUG). Return a sentinel so the
            // caller's positive-scale QVERIFY2 fails loudly instead.
            delete view;
            delete doc;
            return -1.0;
        }
        scroll->resize(1200, 800);
        scroll->show();
        // Deterministic: pump until the viewport has laid out rather than
        // trusting a fixed qWait Wine may not honour. If it never settles the
        // fit is computed against an unmeasured (0-width) viewport and would
        // be meaningless, so return a distinct "unsettled" sentinel (kUnsettled,
        // != the -1.0 wrong-view-type sentinel above) and let the caller skip.
        if (!pumpUntil([&] { return scroll->viewport()->width() > 0; })) {
            delete view;
            delete doc;
            return kUnsettled;
        }
        doc->zoomFitPage(); // FitInView -> reapplyFitMode
        const double s = doc->scaleFactor();
        delete view;
        delete doc;
        return s;
    };

    const double dpr2big = fitScaleFor(makeDprImage(2880, 1800, 2.0));   // logical 1440x900
    const double dpr1small = fitScaleFor(makeDprImage(1440, 900, 1.0));  // logical 1440x900
    const double dpr1big = fitScaleFor(makeDprImage(2880, 1800, 1.0));   // logical 2880x1800

    // If any viewport never laid out under the offscreen platform the fit is
    // unobservable — skip that specific unmeasurable path rather than falsely
    // fail. The real dpr-ratio assertions below are untouched for the settled
    // case.
    if (dpr2big == kUnsettled || dpr1small == kUnsettled || dpr1big == kUnsettled)
        QSKIP("viewport does not settle under offscreen platform");

    QVERIFY2(dpr2big > 0.0 && dpr1small > 0.0 && dpr1big > 0.0,
             "fit did not compute a positive scale (viewport unmeasured?)");
    QVERIFY2(std::abs(dpr2big - dpr1small) < 0.02,
             qPrintable(QStringLiteral("same logical size must fit to same scale: "
                                       "dpr2=%1 vs dpr1=%2")
                            .arg(dpr2big)
                            .arg(dpr1small)));
    QVERIFY2(std::abs(dpr2big - 2.0 * dpr1big) < 0.02,
             qPrintable(QStringLiteral("dpr=2 image (half logical) must fit at 2x the "
                                       "dpr=1 device-equal image: %1 vs 2*%2")
                            .arg(dpr2big)
                            .arg(dpr1big)));
}

void TestImageScale::captureOriginDefaultsToActual_data() {
    QTest::addColumn<int>("deviceW");
    QTest::addColumn<int>("deviceH");
    QTest::addColumn<qreal>("dpr");
    QTest::newRow("dpr1") << 1440 << 900 << qreal(1.0);
    QTest::newRow("dpr1.5") << 2160 << 1350 << qreal(1.5);
    QTest::newRow("dpr2") << 2880 << 1800 << qreal(2.0);
}

void TestImageScale::captureOriginDefaultsToActual() {
    QFETCH(int, deviceW);
    QFETCH(int, deviceH);
    QFETCH(qreal, dpr);

    ImageDocument doc{QString()};
    doc.setImageForTest(makeDprImage(deviceW, deviceH, dpr), /*captureOrigin=*/true);
    QWidget *view = doc.createView(nullptr);
    QVERIFY(view != nullptr);

    // A screenshot / clipboard-origin image defaults to Actual Size
    // (1:1 pixel-exact), not FitInView-capped-at-100%.
    doc.triggerInitialZoomForTest();
    QCOMPARE(doc.zoomMode(), ZoomMode::Actual);
    QVERIFY2(std::abs(doc.scaleFactor() - 1.0) < 1e-6,
             qPrintable(QStringLiteral("capture default scale %1, expected 1.0")
                            .arg(doc.scaleFactor())));

    delete view;
}

void TestImageScale::ordinaryOpenKeepsFit() {
    // Regression guard: an ordinary file open (not capture-origin) keeps
    // the existing fit-to-content-capped-at-100% default, NOT Actual.
    ImageDocument doc{QString()};
    doc.setImageForTest(makeDprImage(4000, 3000, 1.0), /*captureOrigin=*/false);
    QWidget *view = doc.createView(nullptr);
    auto *scroll = qobject_cast<QScrollArea *>(view);
    QVERIFY(scroll != nullptr);
    scroll->resize(1200, 800);
    scroll->show();
    // Deterministic wait for layout; the initial fit needs a measured
    // viewport. If it never settles under the offscreen platform the fit
    // is unobservable, so skip rather than crash/hang.
    if (!pumpUntil([&] { return scroll->viewport()->width() > 0; })) {
        delete view;
        QSKIP("viewport does not settle under offscreen platform");
    }
    doc.triggerInitialZoomForTest();

    QCOMPARE(doc.zoomMode(), ZoomMode::FitInView);
    // A 4000x3000 logical image shrinks to fit an 1200x800 viewport.
    QVERIFY2(doc.scaleFactor() < 1.0,
             qPrintable(QStringLiteral("ordinary large image should fit-shrink, scale=%1")
                            .arg(doc.scaleFactor())));

    delete view;
}

void TestImageScale::smallImageResizeDoesNotUpscale() {
    // Primary bug (render): an ordinary open of an image that already
    // fits must stay at Actual Size (<=100%) and must NOT be re-fit and
    // upscaled when the viewport later settles. Before the fix the
    // initial fit parked the mode at FitInView even though the image fit
    // at 100%, so the resize watcher's reapplyFitMode() upscaled it
    // (uncapped) past 100% — a 600x420 image blew up to ~194% once the
    // viewport measured ~1360x815.
    ImageDocument doc{QString()};
    doc.setImageForTest(makeDprImage(600, 420, 1.0), /*captureOrigin=*/false);
    QWidget *view = doc.createView(nullptr);
    auto *scroll = qobject_cast<QScrollArea *>(view);
    QVERIFY(scroll != nullptr);
    scroll->resize(1400, 900);
    scroll->show();
    // Deterministic wait for the viewport to lay out instead of a fixed
    // qWait. If it never settles under the offscreen platform the initial
    // fit can't measure it, so skip the unmeasurable path rather than
    // silently crash/hang under Wine.
    if (!pumpUntil([&] { return scroll->viewport()->width() > 0; })) {
        delete view;
        QSKIP("viewport does not settle under offscreen platform");
    }

    // Initial fit: a 600x420 image fits a ~1400x900 viewport, so it opens
    // at Actual Size (capped at 100%). This already passes today.
    doc.triggerInitialZoomForTest();
    QVERIFY2(std::abs(doc.scaleFactor() - 1.0) < 1e-6,
             qPrintable(QStringLiteral("initial open should cap at 100%%, got %1")
                            .arg(doc.scaleFactor())));

    // The viewport settles to its final size and the resize watcher fires
    // reapplyFitMode(). An ordinary open that already fits must NOT be
    // upscaled past 100%, and its mode must be Actual (so the watcher
    // no-ops), not FitInView.
    scroll->viewport()->resize(1360, 815);
    doc.reapplyFitMode();
    QVERIFY2(doc.scaleFactor() <= 1.0 + 1e-6,
             qPrintable(QStringLiteral("ordinary open must not upscale past 100%% on "
                                       "resize, got %1")
                            .arg(doc.scaleFactor())));
    QCOMPARE(doc.zoomMode(), ZoomMode::Actual);

    delete view;
}

void TestImageScale::readoutMatchesRenderAfterAsyncFit() {
    // Secondary bug (readout): the status-bar zoom indicator is set
    // synchronously when a document opens (reading the pre-fit 100%
    // scale), but the real scale for a large image is decided by the
    // ASYNC applyInitialFitZoom that fires on the event loop. The
    // indicator was frozen at "100%" while the render shrank to fit —
    // a visible mismatch. After the fix the readout must reflect the
    // post-fit scale.
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app != nullptr);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString png = dir.path() + "/large_async_fit.png";
    // An image comfortably larger than the shown window's viewport so it must
    // shrink to fit (render below 100%). Kept modest (1975x1300, ~10 MB
    // decoded) rather than the former 4000x3000 (~48 MB): the huge fixture
    // faulted during decode/resample under Wine's offscreen GDI (a silent
    // crash), while 1975x1300 still exceeds the window (sized to content but
    // capped at 90% of the offscreen 800x800 screen -> 720x720) and forces
    // the same shrink-to-fit path.
    //
    // The width (1975) still forces the same multi-step layout settle this
    // test wants to exercise (the viewport can be measured more than once
    // before it reaches its final size, e.g. 704 px then 718 px). That used
    // to matter a great deal: reapplyFitMode() (called by
    // FitModeResizeWatcher on every viewport Resize, not just the first)
    // recomputed the render scale on the LATER measurement but never told
    // the zoom readout, which was set once from an EARLIER measurement —
    // so whether this test passed depended on both widths happening to
    // round to the same percentage (at 1975 px, 704/1975 = 35.65% and
    // 718/1975 = 36.35% both round to 36; at 2000 px, 35.2%/35.9% straddle
    // 35.5% and don't). That was a real product bug (a "lying control"
    // per PHILOSOPHY.md) masked by this fixture's dimensions, not merely a
    // test artifact — caught when the self-hosted macOS runner's window
    // geometry landed on a straddling pair of widths instead of an
    // agreeing one (docs/backlog/2026-07-24-test-image-scale-macos-failure.md).
    // Fixed at the source: reapplyFitMode() now calls notifyChanged() on
    // every rescale (src/document/ImageAdapter.cpp), so the readout always
    // tracks the CURRENT render scale regardless of how many measurement
    // passes it took to get there — the exact-1975 choice is no longer
    // load-bearing for correctness, only for reliably forcing the
    // shrink-to-fit + multi-step-settle path this test exercises.
    // reapplyFitModeNotifiesOnEveryRescale() below pins the fix directly,
    // independent of any particular viewport width.
    QImage big(1975, 1300, QImage::Format_ARGB32);
    big.fill(qRgb(180, 190, 200));
    QVERIFY(big.save(png, "PNG"));

    app->openFiles({png});

    // Locate the freshly-opened large-image document and its window. Runs
    // on every pump slice because the window/document appear on the event
    // loop after openFiles returns.
    ImageDocument *doc = nullptr;
    MainWindow *mw = nullptr;
    auto locate = [&] {
        doc = nullptr;
        mw = nullptr;
        for (MainWindow *w : app->windows()) {
            if (!w)
                continue;
            for (int i = 0; i < w->documentCount(); ++i) {
                IDocument *d = nullptr;
                if (w->documentAt(i, &d) == 1 && d) {
                    if (auto *img = dynamic_cast<ImageDocument *>(d)) {
                        if (img->imagePixelSize() == QSize(1975, 1300)) {
                            doc = img;
                            mw = w;
                        }
                    }
                }
            }
        }
    };

    // Deterministic wait: pump until the document exists AND its ASYNC
    // initial-fit singleShot(0) has shrunk the render below 100%, instead
    // of a fixed qWait(200) Wine may not meet. `settled` records whether
    // the fit actually landed within the budget.
    const bool settled = pumpUntil([&] {
        locate();
        return doc && doc->scaleFactor() < 1.0;
    });

    // Null-guard the lookups before any dereference: a QVERIFY fails
    // loudly (with output) where an unguarded deref would segfault
    // silently under Wine's block-buffered stdout.
    QVERIFY2(doc != nullptr, "the 1975x1300 document should open");
    QVERIFY(mw != nullptr);

    if (!settled) {
        // Under the offscreen platform a shown window's viewport can
        // legitimately never gain a real size, so applyInitialFitZoom
        // keeps rescheduling and the render never shrinks — the
        // readout/render match is unobservable here. Skip only this
        // specific unmeasurable path; the assertions below are unweakened.
        QSKIP("async initial fit does not settle under offscreen platform");
    }

    // The async fit shrank the render below 100%.
    QVERIFY2(doc->scaleFactor() < 1.0,
             qPrintable(QStringLiteral("expected shrink-to-fit below 100%%, got %1")
                            .arg(doc->scaleFactor())));

    auto *indicator = mw->findChild<QLabel *>(QStringLiteral("zoomIndicator"));
    QVERIFY2(indicator != nullptr, "MainWindow should host a zoomIndicator label");
    const QString expected =
        QStringLiteral("%1%").arg(qRound(doc->zoomFactor() * 100.0));
    // The readout is refreshed by a MainWindow singleShot(0) that fires
    // AFTER the doc's fit tick, so pump until it catches up to the render
    // (bounded). This tolerates the async delay deterministically without
    // weakening the check: if the fix regressed and the readout never
    // refreshes, the pump times out and the QVERIFY2 below fails on the
    // stale text.
    pumpUntil([&] { return indicator->text() == expected; });
    QVERIFY2(indicator->text() == expected,
             qPrintable(QStringLiteral("readout '%1' must match render %2 after async fit")
                            .arg(indicator->text())
                            .arg(expected)));
}

void TestImageScale::reapplyFitModeNotifiesOnEveryRescale() {
    // Direct, platform-independent regression for the root cause behind
    // the macOS CI failure in readoutMatchesRenderAfterAsyncFit() above:
    // reapplyFitMode() is called by FitModeResizeWatcher on EVERY viewport
    // Resize event (a live window resize, or -- as on the self-hosted
    // macOS runner -- a platform's own multi-pass layout settling after
    // the initial fit already committed a scale), not just once. Before
    // the fix, only applyInitialFitZoom() called
    // capabilityNotifier()->notifyChanged() (the signal MainWindow's zoom
    // readout listens to); a LATER resize-driven re-fit changed
    // scaleFactor() silently. That is a real product bug -- a "lying
    // control" per PHILOSOPHY.md, since the status-bar zoom percentage
    // could freeze at a stale value while the image kept rescaling
    // underneath it -- not a quirk of any one test fixture's dimensions.
    // This test forces the exact shape of the race directly (two viewport
    // widths that give genuinely different fit scales) instead of relying
    // on incidental window-layout timing, so it holds on every platform,
    // independent of whatever the OS's real settle widths happen to be.
    ImageDocument doc{QString()};
    doc.setImageForTest(makeDprImage(4000, 3000, 1.0), /*captureOrigin=*/false);
    QWidget *view = doc.createView(nullptr);
    auto *scroll = qobject_cast<QScrollArea *>(view);
    QVERIFY(scroll != nullptr);
    scroll->resize(1200, 800);
    scroll->show();
    if (!pumpUntil([&] { return scroll->viewport()->width() > 0; })) {
        delete view;
        QSKIP("viewport does not settle under offscreen platform");
    }

    doc.triggerInitialZoomForTest();
    QCOMPARE(doc.zoomMode(), ZoomMode::FitInView);
    const double firstScale = doc.scaleFactor();

    auto *notifier = doc.capabilityNotifier();
    QVERIFY2(notifier != nullptr, "ImageDocument should expose a CapabilityNotifier");
    int notifyCount = 0;
    double scaleAtLastNotify = -1.0;
    QObject::connect(notifier, &CapabilityNotifier::capabilitiesChanged, [&]() {
        ++notifyCount;
        scaleAtLastNotify = doc.scaleFactor();
    });

    // Simulate the platform settling the viewport to a different size on a
    // later tick -- the same shape as the macOS runner's 704px -> 718px
    // second measurement, just forced deterministically instead of hoping
    // the offscreen layout happens to produce two differing passes.
    scroll->viewport()->resize(1360, 815);
    doc.reapplyFitMode();

    QVERIFY2(std::abs(doc.scaleFactor() - firstScale) > 1e-6,
             qPrintable(QStringLiteral("test setup should force a genuine rescale on the "
                                       "second resize, got %1 both times")
                            .arg(doc.scaleFactor())));
    QVERIFY2(notifyCount >= 1,
             "reapplyFitMode() must notify (so MainWindow can refresh the zoom readout) "
             "whenever it changes the render scale, not just on the one-shot initial fit");
    QVERIFY2(std::abs(scaleAtLastNotify - doc.scaleFactor()) < 1e-9,
             qPrintable(QStringLiteral("the notified scale (%1) must match the CURRENT "
                                       "render scale (%2) -- one source of truth")
                            .arg(scaleAtLastNotify)
                            .arg(doc.scaleFactor())));

    delete view;
}

void TestImageScale::reapplyFitModeSkipsNotifyWhenScaleUnchanged() {
    // Direct regression for the "notify only on genuine change" guard
    // added to reapplyFitMode() (2026-07-26 hardening, reviewed in the
    // same pass that produced reapplyFitModeNotifiesOnEveryRescale
    // above): a reapply that recomputes the SAME effective scale (e.g. a
    // redundant resize event, or a viewport settling back to an
    // identical size) must not re-fire notifyChanged() -- there is
    // nothing new for the zoom readout to learn. Before this hardening
    // (the shape PR #122 originally shipped), every reapply notified
    // unconditionally regardless of whether the scale actually moved.
    ImageDocument doc{QString()};
    doc.setImageForTest(makeDprImage(4000, 3000, 1.0), /*captureOrigin=*/false);
    QWidget *view = doc.createView(nullptr);
    auto *scroll = qobject_cast<QScrollArea *>(view);
    QVERIFY(scroll != nullptr);
    scroll->resize(1200, 800);
    scroll->show();
    if (!pumpUntil([&] { return scroll->viewport()->width() > 0; })) {
        delete view;
        QSKIP("viewport does not settle under offscreen platform");
    }

    doc.triggerInitialZoomForTest();
    QCOMPARE(doc.zoomMode(), ZoomMode::FitInView);

    auto *notifier = doc.capabilityNotifier();
    QVERIFY2(notifier != nullptr, "ImageDocument should expose a CapabilityNotifier");
    int notifyCount = 0;
    QObject::connect(notifier, &CapabilityNotifier::capabilitiesChanged,
                      [&]() { ++notifyCount; });

    // Reapply with the SAME viewport size the initial fit already used --
    // the fit computation must land on the identical scale, so this is a
    // genuine no-op and must not notify.
    doc.reapplyFitMode();

    QCOMPARE(notifyCount, 0);

    delete view;
}

void TestImageScale::applyInitialFitZoomForcesDecodeForCaptureOrigin() {
    // Direct regression for the ensureDecoded() call added to the top of
    // applyInitialFitZoom() (2026-07-26 hardening). triggerInitialZoomForTest()
    // calls applyInitialFitZoom() directly, bypassing the normal
    // installDecodedContent() -> singleShot(0) chain that guarantees the
    // off-thread decode has already landed in production use. Without
    // ensureDecoded(), a capture-origin document whose decode had not yet
    // completed would hit the (!m_image.isNull()) guard and silently
    // return, leaving zoomMode stuck at its Custom default instead of the
    // documented Actual Size -- the same SHAPE of symptom (Custom instead
    // of Actual) as the reported macOS CI failure in
    // pendingCaptureDprConsumedOncePerBatch, though that specific test is
    // NOT itself a repro of this path: it calls capDoc->image() (which
    // internally calls ensureDecoded()) before triggerInitialZoomForTest(),
    // so m_image is already non-null by the time this method runs there
    // (a review-before-push finding on the first version of this fix).
    // This test constructs the race this fix actually protects against
    // directly: no decode-forcing accessor is called before
    // triggerInitialZoomForTest().
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + "/race.png";
    QImage img(1200, 900, QImage::Format_ARGB32);
    img.fill(Qt::darkGray);
    QVERIFY(img.save(path, "PNG"));

    ImageDocument doc(path); // real file path -> constructor starts async off-thread decode
    doc.markCaptureOrigin(2.0);
    QWidget *view = doc.createView(nullptr);
    QVERIFY(view != nullptr);

    // Deliberately NO ensureDecoded() / image() / awaitDecodeForTest()
    // call here: call the test-only trigger immediately, maximizing the
    // chance the off-thread decode has not yet landed. (A thread-pool
    // context switch takes far longer than the handful of C++ calls
    // between construction and this line, so this reliably catches the
    // decode still in flight in practice, though it is inherently a
    // race rather than a hard guarantee.)
    doc.triggerInitialZoomForTest();

    QCOMPARE(doc.zoomMode(), ZoomMode::Actual);
    QVERIFY2(std::abs(doc.scaleFactor() - 1.0) < 1e-6,
             qPrintable(QStringLiteral("capture-origin should resolve to 100%% Actual "
                                       "even when the decode raced the trigger, got %1")
                            .arg(doc.scaleFactor())));

    delete view;
}

void TestImageScale::applyZoomStateSentinelLeavesInitialFitUndecided() {
    // Direct regression for the applyZoomState() bug found while tracing
    // the macOS-only pendingCaptureDprConsumedOncePerBatch failure
    // (2026-07-26; see docs/backlog for the fuller mechanism). A
    // (Custom, 0.0) pair is RecentEntry / DocumentTypeDefault's "not
    // captured" sentinel (RecentEntry.h: "-1 / 0.0 sentinel values mean
    // 'not yet captured' -- the open path leaves the document at its
    // natural defaults in that case"). Before the fix, applyZoomState()
    // set m_initialZoomApplied = true UNCONDITIONALLY before checking the
    // sentinel, so this call applied nothing yet permanently pre-empted
    // applyInitialFitZoom() from ever running -- stranding the document
    // at its raw ZoomMode::Custom constructor default. Root-caused this
    // as the actual failure: MainWindow::onCurrentDocumentChanged()
    // applies a leftover, persisted DocumentTypeDefault to every
    // newly-opened document of that type BEFORE its initial fit tick
    // runs, and a real default-constructed-but-hasState()-true entry
    // (e.g. sidebar mode captured, zoom never was) is exactly a
    // (Custom, 0.0) pair.
    ImageDocument doc{QString()};
    doc.setImageForTest(makeDprImage(1600, 1000, 2.0), /*captureOrigin=*/true);
    QWidget *view = doc.createView(nullptr);
    QVERIFY(view != nullptr);

    // Simulate exactly what onCurrentDocumentChanged() does with a
    // "not captured" sentinel.
    doc.applyZoomState(ZoomMode::Custom, 0.0);

    // The sentinel must be a true no-op: the natural, capture-origin
    // Actual-Size decision must still be reachable afterward, not
    // permanently blocked.
    doc.triggerInitialZoomForTest();
    QCOMPARE(doc.zoomMode(), ZoomMode::Actual);

    delete view;
}

void TestImageScale::captureOriginIgnoresPersistedTypeDefault() {
    // Direct regression for the missing isCaptureOrigin() guard in
    // MainWindow::onCurrentDocumentChanged() (2026-07-26). Simulates the
    // exact "poisoned" state the self-hosted macOS runner had --  a
    // persisted Image-type DocumentTypeDefault with a genuine non-Actual
    // Custom zoom, as if an unrelated ordinary image had been closed at
    // 42% -- without needing to actually close a window (which on macOS
    // would itself require the QSettings::IniFormat fix in this file's
    // main() to land in the sandboxed store rather than a developer's
    // real ~/Library/Preferences/ domain). A persisted per-type default
    // must never override a fresh capture-origin document's forced
    // Actual-Size default (ImageDocument::applyInitialFitZoom()'s
    // documented "Screenshot / clipboard-origin images open at Actual
    // Size... matching Preview's default for screen captures").
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app != nullptr);

    // Correctness-skeptic review finding on an earlier version of this
    // test: poisoning the SHARED, process-wide documentTypeDefaults()
    // without resetting it afterward silently corrupted a LATER test in
    // this same binary (pendingCaptureDprConsumedOncePerBatch's ordDoc
    // assertion kept passing, but only by accident -- its own
    // triggerInitialZoomForTest() call became a no-op against the still-
    // poisoned default, exactly the defect class this whole investigation
    // is about, reintroduced by this test against the very test that
    // motivated it). qScopeGuard restores the default-constructed ("no
    // captured state") value even if a QVERIFY/QCOMPARE below returns
    // early.
    const auto resetTypeDefault = qScopeGuard([app]() {
        app->documentTypeDefaults().setForType(DocumentType::Image, DocumentTypeDefault{});
    });

    DocumentTypeDefault poisoned;
    poisoned.zoomMode = ZoomMode::Custom;
    poisoned.zoomFactor = 0.42;
    app->documentTypeDefaults().setForType(DocumentType::Image, poisoned);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString capPng = dir.path() + "/cap-poisoned.png";
    QVERIFY(makeDprImage(1600, 1000, 2.0).save(capPng, "PNG"));

    app->setPendingCaptureDpr(2.0);
    app->openFiles({capPng});
    ImageDocument *capDoc = newestImageDoc(app);
    QVERIFY(capDoc != nullptr);
    QCOMPARE(capDoc->image().devicePixelRatio(), qreal(2.0));
    capDoc->triggerInitialZoomForTest();
    QVERIFY2(capDoc->zoomMode() == ZoomMode::Actual,
             "a persisted per-type zoom default must never override a "
             "capture-origin document's forced Actual-Size default");
}

void TestImageScale::pngRoundTripStripsDprThenRecovers() {
    // The seam that made screenshots blurry/oversized: a capture is saved
    // to PNG, and PNG carries no devicePixelRatio metadata, so the reload
    // comes back as raw device pixels stamped dpr=1. The capture path must
    // therefore RE-STAMP the real dpr (markCaptureOrigin) after decode.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString png = dir.path() + "/capture.png";

    // A dpr=2 capture: 2880x1800 device px for a 1440x900-pt display.
    QImage stamped = makeDprImage(2880, 1800, 2.0);
    QCOMPARE(stamped.devicePixelRatio(), qreal(2.0));
    QVERIFY(stamped.save(png, "PNG"));

    // Reload straight off disk, as the file reader would: the dpr stamp is
    // GONE — this documents that PNG strips it.
    QImage reloaded(png);
    QVERIFY(!reloaded.isNull());
    QCOMPARE(reloaded.devicePixelRatio(), qreal(1.0));
    QCOMPARE(reloaded.size(), QSize(2880, 1800));

    // Recovery: the capture path stamps the real dpr onto the freshly
    // decoded (dpr=1) image via markCaptureOrigin. After that the window
    // sizes to the LOGICAL size and Actual Size is pixel-exact 1:1.
    ImageDocument doc{QString()};
    doc.setImageForTest(reloaded); // dpr=1, as decoded from PNG
    doc.markCaptureOrigin(2.0);

    // contentSizeHint is logical (device px / dpr), not raw device px.
    QCOMPARE(doc.contentSizeHint(), QSize(1440, 900));

    QWidget *view = doc.createView(nullptr);
    QVERIFY(view != nullptr);
    doc.zoomActual();
    const QPixmap pm = doc.labelPixmapForTest();
    QVERIFY(!pm.isNull());
    // Actual Size: 1 source device px -> 1 screen device px, no resample.
    QCOMPARE(pm.devicePixelRatio(), qreal(2.0));
    QCOMPARE(pm.width(), 2880);
    QCOMPARE(pm.height(), 1800);

    delete view;
}

void TestImageScale::pendingCaptureDprConsumedOncePerBatch() {
    // Pins the consume-and-reset contract: a captured open stamps the
    // staged dpr, and a SUBSEQUENT ordinary open with NO staged dpr must
    // NOT be treated as capture-origin (dpr stays 1, zoom is never forced
    // to Actual). Guards against a staged dpr leaking across calls.
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app != nullptr);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString capPng = dir.path() + "/cap.png";
    const QString ordPng = dir.path() + "/ordinary.png";
    // Both are plain PNGs on disk (dpr stripped by the save); the capture
    // distinction comes purely from the staged pending dpr, not the file.
    // Capture image kept modest (1600x1000, ~6 MB decoded) — its size never
    // affects the assertions (capture-origin forces Actual regardless), but
    // the former 2880x1800 (~20 MB) risked the same Wine-offscreen decode
    // fault the large fixtures triggered.
    QVERIFY(makeDprImage(1600, 1000, 2.0).save(capPng, "PNG"));
    // The ordinary image must be comfortably LARGER than the window's
    // viewport so it SHRINKS to fit, so a correct (non-capture) open parks
    // FitInView (zoomMode != Actual). If a leaked capture flag wrongly forced
    // Actual Size, the image would open at 100% instead of shrinking — which
    // the `!= Actual` assertion below catches. (A small ordinary image that
    // already fits now legitimately parks Actual@100%, so a larger-than-
    // viewport one is needed to keep this invariant discriminating.) Shrunk
    // from 4000x3000 (~48 MB, faulted under Wine-offscreen) to 2000x1500
    // (~12 MB), which still exceeds the content-sized-but-90%-screen-capped
    // window and forces the same shrink-to-fit park.
    QVERIFY(makeDprImage(2000, 1500, 1.0).save(ordPng, "PNG"));

    // Captured open: stage dpr=2, then open.
    app->setPendingCaptureDpr(2.0);
    app->openFiles({capPng});
    ImageDocument *capDoc = newestImageDoc(app);
    QVERIFY(capDoc != nullptr);
    // The staged dpr was consumed and stamped onto the image.
    QCOMPARE(capDoc->image().devicePixelRatio(), qreal(2.0));
    capDoc->triggerInitialZoomForTest();
    QCOMPARE(capDoc->zoomMode(), ZoomMode::Actual);

    // Subsequent ordinary open with NO staged dpr: must not inherit the
    // prior capture's dpr, and must not default to Actual.
    app->openFiles({ordPng});
    ImageDocument *ordDoc = newestImageDoc(app);
    QVERIFY(ordDoc != nullptr);
    QVERIFY(ordDoc != capDoc);
    QCOMPARE(ordDoc->image().devicePixelRatio(), qreal(1.0));
    // Not capture-origin: this large image must shrink to fit, so the
    // initial zoom resolves to FitInView — it is never force-parked at
    // Actual Size the way a capture-origin open is. (Under the offscreen
    // platform the viewport may be 0, in which case applyInitialFitZoom
    // reschedules and the mode stays Custom; either way it is never
    // Actual, the tell-tale of a leaked capture flag forcing 100%.)
    ordDoc->triggerInitialZoomForTest();
    QVERIFY2(ordDoc->zoomMode() != ZoomMode::Actual,
             "a leaked capture flag would force an ordinary open to Actual Size");
}

void TestImageScale::coordinateRoundTripInvertsAtDpr2() {
    // The annotation overlay / selectable-text layer map doc<->view through
    // mapDocToView / mapViewToDoc. At dpr>1 and a non-1.0 zoom the two must
    // be exact inverses, or annotations land off the pointer. Fails if
    // either direction drops the dpr or the scale factor.
    ImageDocument doc{QString()};
    doc.setImageForTest(makeDprImage(2880, 1800, 2.0), /*captureOrigin=*/true);
    QWidget *view = doc.createView(nullptr);
    QVERIFY(view != nullptr);

    // Drive a genuine non-1.0 logical scale so both the scale and dpr
    // factors are exercised (0.5 with dpr 2 gives a doc->view factor of
    // 0.25 — nothing cancels to a trivial identity).
    doc.applyZoomState(ZoomMode::Custom, 0.5);
    QVERIFY2(std::abs(doc.scaleFactor() - 0.5) < 1e-9, "scale did not take");

    const QPointF pts[] = {QPointF(0, 0), QPointF(123, 45),
                           QPointF(2879, 1799), QPointF(640.5, 400.25)};
    for (const QPointF &p : pts) {
        const QPointF rt = doc.docToViewForTest(doc.viewToDocForTest(p));
        QVERIFY2(std::abs(rt.x() - p.x()) <= 1.0 && std::abs(rt.y() - p.y()) <= 1.0,
                 qPrintable(QStringLiteral("round-trip drift: (%1,%2) -> (%3,%4)")
                                .arg(p.x())
                                .arg(p.y())
                                .arg(rt.x())
                                .arg(rt.y())));
    }

    delete view;
}

void TestImageScale::resampleBranchRestampsDpr() {
    // Pins the re-stamp in the RESAMPLE branch of buildDisplayPixmap (the
    // non-Actual path): a dpr=2 image at 0.5 logical scale must produce a
    // pixmap that (a) still carries dpr=2 so Qt draws it crisp, and (b) has
    // raw width == round(deviceW * scale) so the on-screen logical size is
    // correct. This branch had zero coverage.
    const int deviceW = 2880;
    const int deviceH = 1800;
    ImageDocument doc{QString()};
    doc.setImageForTest(makeDprImage(deviceW, deviceH, 2.0), /*captureOrigin=*/true);
    QWidget *view = doc.createView(nullptr);
    QVERIFY(view != nullptr);

    doc.applyZoomState(ZoomMode::Custom, 0.5); // non-1.0 -> resample branch
    const QPixmap pm = doc.labelPixmapForTest();
    QVERIFY(!pm.isNull());
    QCOMPARE(pm.devicePixelRatio(), qreal(2.0));
    QCOMPARE(pm.width(), int(std::lround(deviceW * 0.5)));
    QCOMPARE(pm.height(), int(std::lround(deviceH * 0.5)));

    delete view;
}

void TestImageScale::ordinaryOpenIgnoresPersistedCustomTypeDefaultZoom() {
    // Direct regression for the reported bug (owner dogfooding,
    // 2026-07-31): a fresh 504x375 JPEG opened at "Zoom: Custom (80%)"
    // per the in-app Feedback Report, with 34 recent files -- an
    // earlier, larger image's manual 80% zoom had become the per-type
    // Image default and was blindly reapplied to this unrelated, much
    // smaller image. A corroborating second report showed a DIFFERENT
    // image landing at Custom (64%) -- confirming the value drifts with
    // whatever the last-closed image happened to be at, not a frozen
    // constant. See DR 2026-07-31-per-type-restore-excludes-content-
    // relative-state.
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app != nullptr);

    const auto resetTypeDefault = qScopeGuard([app]() {
        app->documentTypeDefaults().setForType(DocumentType::Image, DocumentTypeDefault{});
    });

    DocumentTypeDefault poisoned;
    poisoned.zoomMode = ZoomMode::Custom;
    poisoned.zoomFactor = 0.8; // exactly the reported "Custom (80%)"
    app->documentTypeDefaults().setForType(DocumentType::Image, poisoned);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString jpgPath = dir.path() + "/report-504x375.jpg";
    // Exact dimensions from the report: 504x375 px.
    QImage img(504, 375, QImage::Format_RGB32);
    img.fill(qRgb(210, 220, 230));
    QVERIFY(img.save(jpgPath, "JPEG"));

    app->openFiles({jpgPath});
    ImageDocument *doc = newestImageDoc(app);
    QVERIFY(doc != nullptr);

    // Let the async initial fit land (staged open, ADR 0008). Under the
    // pre-fix code this never resolves -- applyZoomState(Custom, 0.8)
    // strands the document at Custom forever -- so this pump times out
    // and the test fails there rather than at a wrong-value QCOMPARE.
    QVERIFY2(pumpUntil([&] { return doc->zoomMode() != ZoomMode::Custom; }),
             "the poisoned Custom 80% per-type default must not strand the "
             "document at Custom -- the natural fit must be allowed to run");

    QCOMPARE(doc->zoomMode(), ZoomMode::Actual);
    QVERIFY2(std::abs(doc->scaleFactor() - 1.0) < 1e-6,
             qPrintable(QStringLiteral("504x375 comfortably fits any real window; expected "
                                       "natural Actual Size (100%%), got %1%% (an unrelated "
                                       "document's stale per-type Custom 80%% default would "
                                       "land here)")
                            .arg(doc->scaleFactor() * 100.0)));
}

void TestImageScale::ordinaryOpenIgnoresPersistedTypeDefaultWindowGeometry() {
    // Direct regression for the window-sizing half of the same reported
    // bug: a fresh 504x375 image opened in a HUGE window because a
    // previous, unrelated Image-type close's window geometry (simulating
    // a maximized/large-document session) was blindly restored,
    // clobbering applyInitialWindowSize()'s already-computed content-fit
    // size. See DR 2026-07-31-per-type-restore-excludes-content-relative-
    // state.
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app != nullptr);

    const auto resetTypeDefault = qScopeGuard([app]() {
        app->documentTypeDefaults().setForType(DocumentType::Image, DocumentTypeDefault{});
    });

    // A generously large saved geometry, as a maximized/large-document
    // window would leave behind.
    QMainWindow probe;
    probe.resize(2400, 1500);
    const QByteArray hugeGeometry = probe.saveGeometry();
    QVERIFY(!hugeGeometry.isEmpty());

    DocumentTypeDefault poisoned;
    poisoned.windowGeometry = hugeGeometry;
    app->documentTypeDefaults().setForType(DocumentType::Image, poisoned);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString jpgPath = dir.path() + "/report-504x375-window.jpg";
    QImage img(504, 375, QImage::Format_RGB32);
    img.fill(qRgb(210, 220, 230));
    QVERIFY(img.save(jpgPath, "JPEG"));

    app->openFiles({jpgPath});
    ImageDocument *doc = newestImageDoc(app);
    QVERIFY(doc != nullptr);

    MainWindow *mw = nullptr;
    for (MainWindow *w : app->windows()) {
        if (w && w->documentCount() > 0)
            mw = w;
    }
    QVERIFY2(mw != nullptr, "the opened document's window should be found");

    // The offscreen test platform's own virtual screen is small and
    // fixed (observed 800x800), so an absolute pixel threshold can't
    // discriminate "restored the huge geometry" from "computed a
    // content-fit size" -- BOTH get clamped well under any generous
    // absolute bound by the tiny screen alone. Instead, measure what
    // actually restoring the poisoned geometry produces UNDER THIS SAME
    // SCREEN via a reference window, and assert the real window's size
    // does NOT match it -- a same-environment comparison that stays
    // meaningful regardless of the test platform's screen size.
    QMainWindow referenceRestore;
    referenceRestore.restoreGeometry(hugeGeometry);
    referenceRestore.show();
    QApplication::processEvents();
    const QSize buggyWouldBe = referenceRestore.size();

    QVERIFY2(mw->size() != buggyWouldBe,
             qPrintable(QStringLiteral("a 504x375 image must not inherit an unrelated "
                                       "document's huge per-type window geometry -- got "
                                       "%1x%2, matching what restoring the poisoned "
                                       "geometry directly produces under this screen")
                            .arg(mw->width())
                            .arg(mw->height())));
}

void TestImageScale::closingAtCustomZoomDoesNotPoisonTypeDefault() {
    // Direct regression for the WRITE side of the same fix: closing a
    // document at a manual Custom zoom must not write that value into
    // the per-type default in the first place (symmetric with the
    // restore-side guard above) -- this is what actually produces the
    // "poisoned" state the other two tests simulate directly, in real
    // usage. See DR 2026-07-31-per-type-restore-excludes-content-
    // relative-state's write-side trace.
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app != nullptr);

    const auto resetTypeDefault = qScopeGuard([app]() {
        app->documentTypeDefaults().setForType(DocumentType::Image, DocumentTypeDefault{});
    });
    app->documentTypeDefaults().setForType(DocumentType::Image, DocumentTypeDefault{});

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString jpgPath = dir.path() + "/closes-at-custom-zoom.jpg";
    QImage img(900, 700, QImage::Format_RGB32);
    img.fill(qRgb(200, 200, 200));
    QVERIFY(img.save(jpgPath, "JPEG"));

    app->openFiles({jpgPath});
    ImageDocument *doc = newestImageDoc(app);
    QVERIFY(doc != nullptr);
    QVERIFY(pumpUntil([&] { return doc->zoomMode() != ZoomMode::Custom; }));

    MainWindow *mw = nullptr;
    for (MainWindow *w : app->windows()) {
        if (w && w->documentCount() > 0)
            mw = w;
    }
    QVERIFY(mw != nullptr);

    // A manual zoom to an arbitrary Custom percentage, exactly as a real
    // pinch/scroll/keyboard zoom would leave the document.
    doc->applyZoomState(ZoomMode::Custom, 0.64); // the second report's 64%
    QCOMPARE(doc->zoomMode(), ZoomMode::Custom);

    // close() (not delete) is what actually runs MainWindow::closeEvent()
    // and its state-capture path; the C++ object is left alive (Qt does
    // not delete-on-close here) for the shared cleanup() teardown below.
    mw->close();

    const DocumentTypeDefault def = app->documentTypeDefaults().forType(DocumentType::Image);
    QVERIFY2(def.zoomMode != ZoomMode::Custom || !(def.zoomFactor > 0.0),
             qPrintable(QStringLiteral("closing at a manual Custom zoom (64%%) must not "
                                       "write into the per-type default; got mode=%1 "
                                       "factor=%2")
                            .arg(int(def.zoomMode))
                            .arg(def.zoomFactor)));
}

void TestImageScale::smallReportedImageNaturalFitIsActual_data() {
    QTest::addColumn<qreal>("dpr");
    QTest::newRow("dpr1") << qreal(1.0);
    QTest::newRow("dpr2") << qreal(2.0);
}

void TestImageScale::smallReportedImageNaturalFitIsActual() {
    // Companion to ordinaryOpenIgnoresPersistedCustomTypeDefaultZoom:
    // confirms the CORRECT natural-fit outcome (Actual Size, 100%) for
    // the exact reported dimensions holds regardless of the image's own
    // devicePixelRatio -- i.e. this is not a DPR bug. The owner's
    // Retina display was the strongest environmental lead named in the
    // investigation, but an ordinary file open always decodes at dpr 1
    // on disk (PNG/JPEG round-trip strips any stamp -- see
    // pngRoundTripStripsDprThenRecovers above); dpr 2 is exercised here
    // directly via setImageForTest to rule out any dpr-dependent path in
    // the fit computation itself, independent of that on-disk fact.
    QFETCH(qreal, dpr);
    ImageDocument doc{QString()};
    // 504x375 DEVICE px at the given dpr -> logical size is 504/dpr x
    // 375/dpr, still comfortably smaller than any real viewport.
    doc.setImageForTest(makeDprImage(504, 375, dpr), /*captureOrigin=*/false);
    QWidget *view = doc.createView(nullptr);
    auto *scroll = qobject_cast<QScrollArea *>(view);
    QVERIFY(scroll != nullptr);
    scroll->resize(1400, 900);
    scroll->show();
    if (!pumpUntil([&] { return scroll->viewport()->width() > 0; })) {
        delete view;
        QSKIP("viewport does not settle under offscreen platform");
    }

    doc.triggerInitialZoomForTest();
    QCOMPARE(doc.zoomMode(), ZoomMode::Actual);
    QVERIFY2(std::abs(doc.scaleFactor() - 1.0) < 1e-6,
             qPrintable(QStringLiteral("dpr=%1: expected natural Actual Size (100%%), got %2%%")
                            .arg(dpr)
                            .arg(doc.scaleFactor() * 100.0)));

    delete view;
}

// Custom main: construct the real Application (a QApplication subclass) so
// the consume-and-reset contract test can drive Application::openFiles,
// and sandbox HOME first so Settings / RecentFiles never touch the real
// config dir. Mirrors tests/test_macos_launch.cpp's scaffolding.
int main(int argc, char **argv) {
    // Unbuffer stdio before anything runs. Wine block-buffers stdout, so a
    // segfault/abort inside a test discards the buffer and QtTest's output
    // (even the "Start testing" banner) never reaches CI — the job then
    // reports a silent crash with zero diagnostics. Unbuffered streams flush
    // each line immediately, so any residual crash is finally visible.
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);

    QTemporaryDir fakeHome;
    if (!fakeHome.isValid())
        return 1;
    qputenv("HOME", fakeHome.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (fakeHome.path() + "/.config").toUtf8());
    qputenv("XDG_DATA_HOME", (fakeHome.path() + "/.local/share").toUtf8());
    QDir().mkpath(fakeHome.path() + "/.config/trailer");
    QDir().mkpath(fakeHome.path() + "/.local/share/trailer");
    // 2026-07-26: Application's DocumentTypeDefaults uses the plain
    // QSettings(org, app) constructor, which on macOS resolves to
    // QSettings::NativeFormat -- CFPreferences, keyed off the process's
    // REAL UID via getpwuid(), not the $HOME env var above. (RecentFiles
    // is NOT affected -- it persists via QFile/QJsonDocument to a path
    // AppPaths derives from QDir::homePath(), which DOES respect $HOME on
    // every platform including macOS, since it's Qt's own portable
    // implementation rather than a native preferences API.) The HOME
    // sandboxing this file (and several siblings) relies on is therefore a
    // no-op for DocumentTypeDefaults on macOS: every run reads/writes the
    // SAME real, persistent ~/Library/Preferences/ domain as every other
    // test binary and the actual shipped app on that machine, so state
    // from an unrelated earlier test (or a developer's own Trailer.app)
    // leaks in.
    // This was the root cause of a macOS-only pendingCaptureDprConsumedOncePerBatch
    // failure: a leftover Image-type DocumentTypeDefault applied a non-
    // Actual zoom underneath a fresh capture-origin document. Forcing
    // IniFormat makes QSettings(org, app) use Qt's own portable backend
    // (a file under $HOME/.config, which the sandboxing above DOES
    // control) on every platform, closing the leak.
    QSettings::setDefaultFormat(QSettings::IniFormat);

    trailer::Application app(argc, argv);
    TestImageScale tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_image_scale.moc"
