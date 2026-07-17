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
#include <QImage>
#include <QPixmap>
#include <QPointF>
#include <QScrollArea>
#include <QTemporaryDir>
#include <QWidget>
#include <QtTest/QtTest>

#include <cmath>

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
    void pngRoundTripStripsDprThenRecovers();
    void pendingCaptureDprConsumedOncePerBatch();
    void coordinateRoundTripInvertsAtDpr2();
    void resampleBranchRestampsDpr();
};

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
    auto fitScaleFor = [](const QImage &img) -> double {
        auto *doc = new ImageDocument(QString());
        doc->setImageForTest(img);
        QWidget *view = doc->createView(nullptr);
        auto *scroll = qobject_cast<QScrollArea *>(view);
        Q_ASSERT(scroll);
        scroll->resize(1200, 800);
        scroll->show();
        QTest::qWait(30); // let the viewport lay out
        doc->zoomFitPage(); // FitInView -> reapplyFitMode
        const double s = doc->scaleFactor();
        delete view;
        delete doc;
        return s;
    };

    const double dpr2big = fitScaleFor(makeDprImage(2880, 1800, 2.0));   // logical 1440x900
    const double dpr1small = fitScaleFor(makeDprImage(1440, 900, 1.0));  // logical 1440x900
    const double dpr1big = fitScaleFor(makeDprImage(2880, 1800, 1.0));   // logical 2880x1800

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
    QTest::qWait(30);
    doc.triggerInitialZoomForTest();

    QCOMPARE(doc.zoomMode(), ZoomMode::FitInView);
    // A 4000x3000 logical image shrinks to fit an 1200x800 viewport.
    QVERIFY2(doc.scaleFactor() < 1.0,
             qPrintable(QStringLiteral("ordinary large image should fit-shrink, scale=%1")
                            .arg(doc.scaleFactor())));

    delete view;
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
    QVERIFY(makeDprImage(2880, 1800, 2.0).save(capPng, "PNG"));
    QVERIFY(makeDprImage(640, 480, 1.0).save(ordPng, "PNG"));

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
    // Not capture-origin: the initial zoom is never forced to Actual. (It
    // resolves to FitInView once the viewport measures; under the offscreen
    // platform the viewport may be 0, so we assert the invariant that holds
    // unconditionally — an ordinary open is never Actual by default.)
    ordDoc->triggerInitialZoomForTest();
    QVERIFY2(ordDoc->zoomMode() != ZoomMode::Actual,
             "ordinary open must not default to Actual Size");
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

// Custom main: construct the real Application (a QApplication subclass) so
// the consume-and-reset contract test can drive Application::openFiles,
// and sandbox HOME first so Settings / RecentFiles never touch the real
// config dir. Mirrors tests/test_macos_launch.cpp's scaffolding.
int main(int argc, char **argv) {
    QTemporaryDir fakeHome;
    if (!fakeHome.isValid())
        return 1;
    qputenv("HOME", fakeHome.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (fakeHome.path() + "/.config").toUtf8());
    qputenv("XDG_DATA_HOME", (fakeHome.path() + "/.local/share").toUtf8());
    QDir().mkpath(fakeHome.path() + "/.config/trailer");
    QDir().mkpath(fakeHome.path() + "/.local/share/trailer");

    trailer::Application app(argc, argv);
    TestImageScale tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_image_scale.moc"
