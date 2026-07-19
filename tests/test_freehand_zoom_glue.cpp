// Bug 1 regression guard: a committed freehand (Ink) stroke must stay
// glued to page content across a zoom change.
//
// This drives the REAL ImageDocument adapter end-to-end — not a
// re-derived transform. The overlay's doc<->view callbacks are the ones
// ImageAdapter wires to ImageDocument::mapDocToView / mapViewToDoc, so
// the assertions exercise the actual capture path, the actual origin /
// scale / dpr maths, and the actual live transform after a zoom change.
// (The full PDF path needs a live QPdfView with scrollbars, which is not
// cleanly drivable offscreen; the Image adapter IS drivable and shares
// the same overlay capture code — mousePressEvent / mouseMoveEvent — so
// it is the honest surface to pin this on. A prior version of this file
// re-implemented the transform with a local lambda and asserted a
// tautology that passed for arbitrary stored points; that has been
// removed.)
//
// The reported bug: a committed stroke "translates across the page" as
// you zoom. If the overlay baked a constant VIEW-space offset into the
// captured samples, the stored DOCUMENT-space points would be wrong, and
// re-rendering them through the live transform at a different zoom would
// slide the stroke off the page feature it was drawn over.
//
// Runs offscreen (QT_QPA_PLATFORM=offscreen), hermetic, deterministic.

#include "annotation/Annotation.h"
#include "annotation/AnnotationStore.h"
#include "document/IDocument.h"
#include "document/ImageAdapter.h"
#include "ui/AnnotationOverlay.h"

#include <QImage>
#include <QMouseEvent>
#include <QWidget>
#include <QtTest/QtTest>

#include <cmath>
#include <vector>

using namespace trailer;

namespace {

// A synthetic decoded image carrying a stamped devicePixelRatio — same
// helper shape as tests/test_image_scale.cpp, so the dpr>1 display path
// (mapDocToView's /dpr term) is exercised on a dpr=1 offscreen platform.
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

double dist(QPointF a, QPointF b) {
    return std::hypot(a.x() - b.x(), a.y() - b.y());
}

} // namespace

class TestFreehandZoomGlue : public QObject {
    Q_OBJECT
  private slots:
    void inkStrokeStaysGluedThroughRealAdapter();
    void inkStrokeStaysGluedThroughRealAdapter_data();
};

void TestFreehandZoomGlue::inkStrokeStaysGluedThroughRealAdapter_data() {
    QTest::addColumn<double>("dpr");
    QTest::newRow("dpr-1.0") << 1.0;
    QTest::newRow("dpr-1.5") << 1.5;
    QTest::newRow("dpr-2.0") << 2.0;
}

void TestFreehandZoomGlue::inkStrokeStaysGluedThroughRealAdapter() {
    QFETCH(double, dpr);

    const double z1 = 0.5; // capture zoom
    const double z2 = 1.6; // display zoom (different scale, exercises live transform)

    ImageDocument doc{QString()};
    doc.setImageForTest(makeDprImage(2400, 1800, dpr), /*captureOrigin=*/true);
    QWidget *view = doc.createView(nullptr);
    QVERIFY(view != nullptr);

    auto *overlay = view->findChild<AnnotationOverlay *>();
    QVERIFY2(overlay, "ImageDocument view must host an AnnotationOverlay");
    AnnotationStore *store = doc.annotations();
    QVERIFY(store);

    // Drain the deferred fit-to-window (QTimer::singleShot(0) in
    // createView) so it can't change the scale mid-capture, then pin a
    // known capture zoom.
    for (int i = 0; i < 5; ++i)
        QCoreApplication::processEvents();
    doc.applyZoomState(ZoomMode::Custom, z1);
    QVERIFY2(std::abs(doc.scaleFactor() - z1) < 1e-9, "capture scale did not take");

    overlay->setActiveTool(AnnotationTool::Ink);

    // Ground truth is defined in DOCUMENT (device-pixel) space,
    // INDEPENDENTLY of the capture path: pick real page features D_i,
    // then map each forward to its z1 view position via the adapter's
    // docToView and draw the stroke exactly there. Capture recovers doc
    // coords via the adapter's viewToDoc, so asserting stored == D_i
    // exercises the full round-trip (viewToDoc ∘ docToView == identity)
    // anchored on a truth that capture never touched. A capture-side
    // offset that is NOT mirrored in docToView — the reported bug — makes
    // stored != D_i and fails. (Verified to fail against a deliberately
    // offset mapViewToDoc during development.)
    const std::vector<QPointF> featureDoc = {
        {160.0, 140.0}, {224.0, 188.0}, {300.0, 176.0}, {380.0, 244.0}, {448.0, 220.0},
    };
    std::vector<QPointF> viewPts;
    for (const QPointF &d : featureDoc)
        viewPts.push_back(doc.docToViewForTest(d));

    sendMouse(overlay, QEvent::MouseButtonPress, viewPts.front(), Qt::LeftButton);
    for (size_t i = 1; i < viewPts.size(); ++i)
        sendMouse(overlay, QEvent::MouseMove, viewPts[i], Qt::LeftButton);
    sendMouse(overlay, QEvent::MouseButtonRelease, viewPts.back(), Qt::LeftButton);
    QCoreApplication::processEvents();

    QCOMPARE(store->count(), 1);
    const Annotation ink = store->annotations().back();
    QCOMPARE(ink.type, AnnotationType::Ink);
    QVERIFY2(ink.points.size() >= viewPts.size(),
             qPrintable(QStringLiteral("stored %1 ink points, drew %2")
                            .arg(ink.points.size())
                            .arg(viewPts.size())));
    QVERIFY2(std::abs(doc.scaleFactor() - z1) < 1e-9, "scale drifted during capture");

    // (A) CAPTURE CORRECTNESS at z1, through the real adapter: each stored
    // sample must equal the independently-chosen page feature D_i it was
    // drawn over. The anchor is ink.points.front(); the tail is the last
    // point. A capture-side view offset makes these miss D_i.
    QVERIFY2(dist(ink.points.front(), featureDoc.front()) < 0.5,
             qPrintable(QStringLiteral("anchor captured off page feature: stored (%1,%2) "
                                       "expected (%3,%4)")
                            .arg(ink.points.front().x())
                            .arg(ink.points.front().y())
                            .arg(featureDoc.front().x())
                            .arg(featureDoc.front().y())));
    QVERIFY2(dist(ink.points.back(), featureDoc.back()) < 0.5,
             "tail sample captured off its page feature");

    // (B) GLUE ACROSS ZOOM, through the real adapter. Change to a
    // different display zoom on the SAME document, then assert every
    // stored sample renders (via the LIVE docToView, now at z2) exactly
    // where its page feature D_i renders. This exercises the changed
    // scale + dpr and fails if the stored doc points carry any offset.
    doc.applyZoomState(ZoomMode::Custom, z2);
    QVERIFY2(std::abs(doc.scaleFactor() - z2) < 1e-9, "display scale did not take");

    for (size_t i = 0; i < ink.points.size() && i < featureDoc.size(); ++i) {
        const QPointF storedAtZ2 = doc.docToViewForTest(ink.points[i]);
        const QPointF featureAtZ2 = doc.docToViewForTest(featureDoc[i]);
        QVERIFY2(dist(storedAtZ2, featureAtZ2) < 0.75,
                 qPrintable(QStringLiteral("sample %1 drifted off its page feature at zoom %2 "
                                           "(dpr %3): stroke at (%4,%5), feature at (%6,%7)")
                                .arg(i)
                                .arg(z2)
                                .arg(dpr)
                                .arg(storedAtZ2.x())
                                .arg(storedAtZ2.y())
                                .arg(featureAtZ2.x())
                                .arg(featureAtZ2.y())));
    }

    // Teeth check: prove the glue tolerance is not vacuous. A stored point
    // corrupted by a doc-space offset (what an origin/offset capture bug
    // produces) must render far enough from its page feature at z2 to
    // exceed the tolerance the real assertion above uses.
    const QPointF poison = featureDoc.front() + QPointF(40.0, 40.0);
    QVERIFY2(dist(doc.docToViewForTest(poison), doc.docToViewForTest(featureDoc.front())) > 0.75,
             "glue tolerance is too loose — a deliberately-offset point would pass");

    delete view;
}

QTEST_MAIN(TestFreehandZoomGlue)
#include "test_freehand_zoom_glue.moc"
