// Direct-manipulation page crop (backlog
// 2026-07-15-crop-pages-direct-manipulation) — page-anchoring / dpr-safety
// and tool-precedence guard.
//
// This drives the REAL ImageDocument adapter end-to-end, exactly like
// tests/test_freehand_zoom_glue.cpp: the overlay's doc<->view callbacks are
// the ones the adapter wires to ImageDocument::mapDocToView / mapViewToDoc,
// so the assertions exercise the actual origin / scale / dpr maths and the
// actual live transform after a zoom change. The PDF path needs a live
// QPdfView with scrollbars (not cleanly drivable offscreen); the Image
// adapter shares the SAME overlay capture + crop code (handleCropPress /
// handleCropMove / handleCropRelease), so it is the honest surface to pin
// the geometry on.
//
// Threshold (G1, from the backlog item): "Cropping is achievable by
// dragging a crop rectangle directly on the page ... the crop rect must be
// page-anchored and dpr-safe." Verified here by drawing the crop rect over
// an independently-chosen page rectangle at capture zoom Z and injected dpr
// D, then asserting the recovered DOCUMENT-space crop rectangle equals that
// page rectangle for every Z in {1.0, 1.5, 2.0} and D in {1, 1.5, 2}, and
// that it still renders over the same page region after a display-zoom
// change (glue across zoom, the #91/#94 family).
//
// Owner ruling (2026-07-20): with the crop tool active a press starts the
// crop rectangle and NEVER selects an annotation underneath — pinned by
// cropPressDoesNotSelectAnnotation().
//
// Runs offscreen (QT_QPA_PLATFORM=offscreen), hermetic, deterministic.

#include "annotation/Annotation.h"
#include "annotation/AnnotationStore.h"
#include "document/IDocument.h"
#include "document/ImageAdapter.h"
#include "ui/AnnotationOverlay.h"

#include <QDir>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QSignalSpy>
#include <QWidget>
#include <QtTest/QtTest>

#include <cmath>
#include <cstdlib>

using namespace trailer;

namespace {

QImage makeDprImage(int deviceW, int deviceH, qreal dpr) {
    QImage img(deviceW, deviceH, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::white);
    img.setDevicePixelRatio(dpr);
    return img;
}

void sendMouse(QWidget *w, QEvent::Type type, QPointF pos, Qt::MouseButton button) {
    const Qt::MouseButtons held =
        (type == QEvent::MouseButtonRelease) ? Qt::NoButton : Qt::MouseButtons(button);
    QMouseEvent ev(type, pos, w->mapToGlobal(pos.toPoint()), button, held, Qt::NoModifier);
    QApplication::sendEvent(w, &ev);
}

double rectDist(const QRectF &a, const QRectF &b) {
    return std::hypot(a.left() - b.left(), a.top() - b.top()) +
           std::hypot(a.right() - b.right(), a.bottom() - b.bottom());
}

// Drive a full crop drag over the view positions that `keepDoc`'s corners
// map to at the document's current zoom/dpr, and return the overlay.
AnnotationOverlay *drawCrop(ImageDocument &doc, AnnotationOverlay *overlay,
                            const QRectF &keepDoc) {
    const QPointF tl = doc.docToViewForTest(keepDoc.topLeft());
    const QPointF br = doc.docToViewForTest(keepDoc.bottomRight());
    sendMouse(overlay, QEvent::MouseButtonPress, tl, Qt::LeftButton);
    sendMouse(overlay, QEvent::MouseMove, QPointF((tl.x() + br.x()) / 2, (tl.y() + br.y()) / 2),
              Qt::LeftButton);
    sendMouse(overlay, QEvent::MouseMove, br, Qt::LeftButton);
    sendMouse(overlay, QEvent::MouseButtonRelease, br, Qt::LeftButton);
    QCoreApplication::processEvents();
    return overlay;
}

} // namespace

class TestCropDirectManipulation : public QObject {
    Q_OBJECT
  private slots:
    void cropRectIsPageAnchoredAndDprSafe();
    void cropRectIsPageAnchoredAndDprSafe_data();
    void cropPressDoesNotSelectAnnotation();
    void enterCommitsCropRect();
    // Emits G2 before/after + drag-sequence PNGs when TRAILER_GRAB_DIR
    // is set; a no-op otherwise (so CI stays clean and headless).
    void grabG2Evidence();
};

void TestCropDirectManipulation::cropRectIsPageAnchoredAndDprSafe_data() {
    QTest::addColumn<double>("dpr");
    QTest::addColumn<double>("captureZoom");
    for (double dpr : {1.0, 1.5, 2.0}) {
        for (double z : {1.0, 1.5, 2.0}) {
            QTest::newRow(qPrintable(QStringLiteral("dpr-%1_z-%2").arg(dpr).arg(z)))
                << dpr << z;
        }
    }
}

void TestCropDirectManipulation::cropRectIsPageAnchoredAndDprSafe() {
    QFETCH(double, dpr);
    QFETCH(double, captureZoom);

    ImageDocument doc{QString()};
    doc.setImageForTest(makeDprImage(2400, 1800, dpr), /*captureOrigin=*/true);
    QWidget *view = doc.createView(nullptr);
    QVERIFY(view != nullptr);
    auto *overlay = view->findChild<AnnotationOverlay *>();
    QVERIFY2(overlay, "ImageDocument view must host an AnnotationOverlay");

    // Drain the deferred fit-to-window, then pin the capture zoom.
    for (int i = 0; i < 5; ++i)
        QCoreApplication::processEvents();
    doc.applyZoomState(ZoomMode::Custom, captureZoom);
    QVERIFY2(std::abs(doc.scaleFactor() - captureZoom) < 1e-9, "capture scale did not take");

    overlay->setActiveTool(AnnotationTool::CropRect);

    // An independently-chosen page rectangle (document / device-pixel
    // space), NOT touched by the capture path.
    const QRectF keepDoc(200.0, 150.0, 800.0, 600.0);
    drawCrop(doc, overlay, keepDoc);

    QVERIFY2(overlay->hasPendingCrop(), "a crop drag must leave a pending crop rect");
    const QRectF got = overlay->pendingCropRectDoc();
    // (A) PAGE-ANCHORING + DPR-SAFETY: the recovered doc-space rect equals
    // the page rectangle for EVERY (dpr, zoom) row — i.e. it is invariant
    // in document space, which is the whole point.
    QVERIFY2(rectDist(got, keepDoc) < 1.0,
             qPrintable(QStringLiteral("crop rect not page-anchored at dpr %1 zoom %2: got "
                                       "(%3,%4 %5x%6) expected (%7,%8 %9x%10)")
                            .arg(dpr).arg(captureZoom)
                            .arg(got.left()).arg(got.top()).arg(got.width()).arg(got.height())
                            .arg(keepDoc.left()).arg(keepDoc.top()).arg(keepDoc.width())
                            .arg(keepDoc.height())));

    // (B) GLUE ACROSS ZOOM: at a DIFFERENT display zoom, the stored doc
    // rect must render over the same page region it was drawn on.
    const double displayZoom = captureZoom * 1.7 + 0.3;
    doc.applyZoomState(ZoomMode::Custom, displayZoom);
    QVERIFY2(std::abs(doc.scaleFactor() - displayZoom) < 1e-9, "display scale did not take");
    const QRectF gotView(doc.docToViewForTest(got.topLeft()),
                         doc.docToViewForTest(got.bottomRight()));
    const QRectF featureView(doc.docToViewForTest(keepDoc.topLeft()),
                             doc.docToViewForTest(keepDoc.bottomRight()));
    QVERIFY2(rectDist(gotView, featureView) < 1.5,
             "stored crop rect drifted off its page region after a zoom change");

    // Teeth: prove the tolerance is not vacuous — a doc-space-corrupted
    // rect must render far enough away to exceed the (B) tolerance.
    const QRectF poison = keepDoc.translated(40.0, 40.0);
    const QRectF poisonView(doc.docToViewForTest(poison.topLeft()),
                            doc.docToViewForTest(poison.bottomRight()));
    QVERIFY2(rectDist(poisonView, featureView) > 1.5,
             "glue tolerance is too loose — a deliberately-offset rect would pass");

    delete view;
}

void TestCropDirectManipulation::cropPressDoesNotSelectAnnotation() {
    ImageDocument doc{QString()};
    doc.setImageForTest(makeDprImage(1600, 1200, 2.0), /*captureOrigin=*/true);
    QWidget *view = doc.createView(nullptr);
    QVERIFY(view != nullptr);
    auto *overlay = view->findChild<AnnotationOverlay *>();
    QVERIFY(overlay);
    AnnotationStore *store = doc.annotations();
    QVERIFY(store);
    for (int i = 0; i < 5; ++i)
        QCoreApplication::processEvents();
    doc.applyZoomState(ZoomMode::Custom, 1.0);

    // A rectangle annotation sitting exactly where the crop drag will
    // start, so a hit-test WOULD select it if the crop path ran one.
    Annotation a;
    a.type = AnnotationType::Rectangle;
    a.page = 0;
    a.bounds = QRectF(180.0, 130.0, 200.0, 160.0);
    const int annId = store->add(std::move(a));
    QVERIFY(annId != 0);
    const int countBefore = store->count();

    overlay->setActiveTool(AnnotationTool::CropRect);
    const QRectF keepDoc(200.0, 150.0, 500.0, 400.0); // TL (200,150) is inside the annotation
    drawCrop(doc, overlay, keepDoc);

    QCOMPARE(overlay->selectedAnnotationId(), 0); // crop owns the pointer — no selection
    QCOMPARE(store->count(), countBefore);        // no annotation added or removed
    QVERIFY2(overlay->hasPendingCrop(), "crop drag over an annotation must still start a crop");
    QVERIFY2(rectDist(overlay->pendingCropRectDoc(), keepDoc) < 1.0,
             "crop rect over an annotation must be the drawn page rect, not the annotation");

    delete view;
}

void TestCropDirectManipulation::enterCommitsCropRect() {
    ImageDocument doc{QString()};
    doc.setImageForTest(makeDprImage(1600, 1200, 1.5), /*captureOrigin=*/true);
    QWidget *view = doc.createView(nullptr);
    QVERIFY(view != nullptr);
    auto *overlay = view->findChild<AnnotationOverlay *>();
    QVERIFY(overlay);
    for (int i = 0; i < 5; ++i)
        QCoreApplication::processEvents();
    doc.applyZoomState(ZoomMode::Custom, 1.25);

    overlay->setActiveTool(AnnotationTool::CropRect);
    const QRectF keepDoc(120.0, 90.0, 500.0, 360.0);
    drawCrop(doc, overlay, keepDoc);
    QVERIFY(overlay->hasPendingCrop());

    QSignalSpy spy(overlay, &AnnotationOverlay::cropCommitted);
    // Real key path: Enter over the crop tool commits.
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(overlay, &enter);
    QCoreApplication::processEvents();

    QCOMPARE(spy.count(), 1);
    const QRectF emitted = spy.at(0).at(0).toRectF();
    const int page = spy.at(0).at(1).toInt();
    QCOMPARE(page, 0);
    QVERIFY2(rectDist(emitted, keepDoc) < 1.0, "committed crop rect must be the page-space keep rect");
    QVERIFY2(!overlay->hasPendingCrop(), "committing must clear the pending crop");

    delete view;
}

void TestCropDirectManipulation::grabG2Evidence() {
    const char *dir = std::getenv("TRAILER_GRAB_DIR");
    if (!dir || !*dir)
        QSKIP("TRAILER_GRAB_DIR not set — evidence grabbing disabled");
    QDir().mkpath(QString::fromLocal8Bit(dir));
    const QDir out(QString::fromLocal8Bit(dir));

    // A content-rich "page" so the dimmed crop preview reads clearly:
    // a light page with a grid + a heading and body block, at dpr 1 for
    // a crisp 1:1 grab.
    QImage page(900, 640, QImage::Format_ARGB32_Premultiplied);
    page.fill(QColor(250, 249, 245));
    {
        QPainter g(&page);
        g.setPen(QPen(QColor(225, 223, 214), 1));
        for (int x = 0; x < page.width(); x += 40)
            g.drawLine(x, 0, x, page.height());
        for (int y = 0; y < page.height(); y += 40)
            g.drawLine(0, y, page.width(), y);
        g.setPen(QColor(40, 40, 46));
        QFont h = g.font();
        h.setPointSize(26);
        h.setBold(true);
        g.setFont(h);
        g.drawText(QRect(60, 40, 780, 60), Qt::AlignLeft, QStringLiteral("Quarterly Report"));
        QFont b = g.font();
        b.setPointSize(13);
        b.setBold(false);
        g.setFont(b);
        for (int i = 0; i < 6; ++i)
            g.drawText(QRect(60, 130 + i * 34, 780, 30), Qt::AlignLeft,
                       QStringLiteral("The scanned edge and margin noise should be trimmed away."));
    }

    ImageDocument doc{QString()};
    doc.setImageForTest(page, /*captureOrigin=*/false);
    QWidget *view = doc.createView(nullptr);
    QVERIFY(view);
    view->resize(920, 660);
    view->show();
    auto *overlay = view->findChild<AnnotationOverlay *>();
    QVERIFY(overlay);
    for (int i = 0; i < 6; ++i)
        QCoreApplication::processEvents();
    doc.applyZoomState(ZoomMode::Custom, 1.0);
    QWidget *grabTarget = overlay->parentWidget(); // page label + overlay child
    QVERIFY(grabTarget);

    auto grab = [&](const QString &name) {
        for (int i = 0; i < 3; ++i)
            QCoreApplication::processEvents();
        grabTarget->grab().save(out.filePath(name));
    };

    // BEFORE: crop tool active, no rectangle yet — the plain page.
    overlay->setActiveTool(AnnotationTool::CropRect);
    grab(QStringLiteral("crop-drag-before.png"));

    // The keep-region in doc space and its view positions.
    const QRectF keepDoc(150.0, 110.0, 560.0, 430.0);
    const QPointF tl = doc.docToViewForTest(keepDoc.topLeft());
    const QPointF br = doc.docToViewForTest(keepDoc.bottomRight());

    // MID 1: partway through the initial rubber-band drag.
    sendMouse(overlay, QEvent::MouseButtonPress, tl, Qt::LeftButton);
    sendMouse(overlay, QEvent::MouseMove,
              QPointF(tl.x() + (br.x() - tl.x()) * 0.55, tl.y() + (br.y() - tl.y()) * 0.55),
              Qt::LeftButton);
    grab(QStringLiteral("crop-drag-mid1-rubberband.png"));
    // MID 2: rectangle fully drawn — dimmed preview + rule-of-thirds.
    sendMouse(overlay, QEvent::MouseMove, br, Qt::LeftButton);
    sendMouse(overlay, QEvent::MouseButtonRelease, br, Qt::LeftButton);
    grab(QStringLiteral("crop-drag-mid2-drawn.png"));

    // MID 3: adjust via a corner handle — drag the bottom-right handle in.
    const QPointF handlePt = doc.docToViewForTest(overlay->pendingCropRectDoc().bottomRight());
    const QPointF handleTo = doc.docToViewForTest(QPointF(keepDoc.right() - 90, keepDoc.bottom() - 70));
    sendMouse(overlay, QEvent::MouseButtonPress, handlePt, Qt::LeftButton);
    sendMouse(overlay, QEvent::MouseMove, handleTo, Qt::LeftButton);
    grab(QStringLiteral("crop-drag-mid3-handle.png"));
    sendMouse(overlay, QEvent::MouseButtonRelease, handleTo, Qt::LeftButton);

    // AFTER: the adjusted crop rectangle ready to commit (Enter).
    grab(QStringLiteral("crop-drag-after.png"));

    delete view;
}

QTEST_MAIN(TestCropDirectManipulation)
#include "test_crop_direct_manipulation.moc"
