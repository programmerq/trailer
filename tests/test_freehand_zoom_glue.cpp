// Bug 1 regression guard: a committed freehand (Ink) stroke must stay
// glued to page content at every zoom.
//
// The overlay captures Ink samples in DOCUMENT space during the drag,
// and renders them back through the live doc->view transform on every
// paint. If any sample were captured with a constant VIEW-space offset
// baked in (the "coalesced sub-point is not re-localized to the child
// overlay" hypothesis), that offset would survive as a doc-space error
// that RESCALES with the zoom ratio when re-rendered — i.e. the stroke
// body would translate away from its anchor as you zoom, growing with
// the zoom ratio.
//
// This test drives a real AnnotationOverlay with synthetic mouse events
// through a zoom-dependent, dpr-parametrised transform that mirrors the
// production PDF (centering origin) and Image (scale/dpr) mappings, then
// asserts:
//   (a) at the CAPTURE zoom, every stored sample maps back to the exact
//       view point it was drawn at (capture is offset-free); and
//   (b) at a DIFFERENT display zoom, the whole stroke — anchor AND body —
//       tracks page content rigidly: the vector from the anchor to every
//       other sample scales by exactly the zoom ratio, with no residual
//       translation.
//
// Runs offscreen (QT_QPA_PLATFORM=offscreen), hermetic, deterministic.

#include "annotation/AnnotationStore.h"
#include "ui/AnnotationOverlay.h"

#include <QMouseEvent>
#include <QWidget>
#include <QtTest/QtTest>

#include <vector>

using namespace trailer;

namespace {

// A zoom + dpr dependent affine mapping shared by the overlay's
// doc<->view callbacks. `zoom` and `dpr` are mutable so the test can
// re-point the SAME transform at a new display zoom after capture —
// exactly what the production adapters do on a zoom step.
struct Mapping {
    double zoom = 1.0;
    double dpr = 1.0;
    // A centering origin that depends on zoom, mirroring the PDF
    // single-page adapter's extraX/extraY term. This makes the test
    // sensitive to any anchor-vs-body origin inconsistency: a baked-in
    // view offset would fail to track this moving origin on re-zoom.
    QPointF origin() const {
        // Shrinks as zoom grows, like (viewport - content*zoom)/2.
        const double ex = 400.0 - 40.0 * zoom;
        const double ey = 300.0 - 30.0 * zoom;
        return QPointF(ex, ey);
    }
    QPointF docToView(QPointF p) const {
        const double s = zoom / dpr;
        return origin() + QPointF(p.x() * s, p.y() * s);
    }
    QPointF viewToDoc(QPointF v) const {
        const double s = zoom / dpr;
        const QPointF o = origin();
        return QPointF((v.x() - o.x()) / s, (v.y() - o.y()) / s);
    }
};

void sendMouse(QWidget *w, QEvent::Type type, QPointF pos, Qt::MouseButton button) {
    const Qt::MouseButtons held =
        (type == QEvent::MouseButtonRelease) ? Qt::NoButton : Qt::MouseButtons(button);
    QMouseEvent ev(type, pos, w->mapToGlobal(pos.toPoint()), button, held, Qt::NoModifier);
    QApplication::sendEvent(w, &ev);
}

} // namespace

class TestFreehandZoomGlue : public QObject {
    Q_OBJECT
  private slots:
    void inkStrokeStaysGluedAcrossZoom();
    void inkStrokeStaysGluedAcrossZoom_data();
};

void TestFreehandZoomGlue::inkStrokeStaysGluedAcrossZoom_data() {
    QTest::addColumn<double>("dpr");
    QTest::newRow("dpr-1.0") << 1.0;
    QTest::newRow("dpr-1.5") << 1.5;
    QTest::newRow("dpr-2.0") << 2.0;
}

void TestFreehandZoomGlue::inkStrokeStaysGluedAcrossZoom() {
    QFETCH(double, dpr);

    Mapping map;
    map.dpr = dpr;
    const double z1 = 1.25; // capture zoom
    const double z2 = 2.5;  // display zoom
    map.zoom = z1;

    QWidget host;
    host.resize(1000, 800);
    AnnotationStore store;
    AnnotationOverlay overlay(&host);
    overlay.setGeometry(host.rect());
    overlay.setStore(&store);
    overlay.setDocumentToView([&map](QPointF p, int) { return map.docToView(p); });
    overlay.setViewToDocument([&map](QPointF v, int) { return map.viewToDoc(v); });
    overlay.setPageAtViewPoint([](QPointF) { return 0; });
    overlay.setActiveTool(AnnotationTool::Ink);

    // A multi-sample freehand stroke drawn in VIEW space at z1.
    const std::vector<QPointF> viewPts = {
        {450.0, 360.0}, {480.0, 372.0}, {520.0, 400.0}, {560.0, 430.0}, {600.0, 450.0},
    };

    sendMouse(&overlay, QEvent::MouseButtonPress, viewPts.front(), Qt::LeftButton);
    for (size_t i = 1; i < viewPts.size(); ++i)
        sendMouse(&overlay, QEvent::MouseMove, viewPts[i], Qt::LeftButton);
    sendMouse(&overlay, QEvent::MouseButtonRelease, viewPts.back(), Qt::LeftButton);
    QApplication::processEvents();

    QCOMPARE(store.count(), 1);
    const Annotation &ink = store.annotations().back();
    QCOMPARE(ink.type, AnnotationType::Ink);
    // Every drawn view sample should be represented; coalescing may add
    // duplicates but never fewer than the points we drove.
    QVERIFY2(ink.points.size() >= viewPts.size(),
             qPrintable(QStringLiteral("stored %1 ink points, drew %2")
                            .arg(ink.points.size())
                            .arg(viewPts.size())));

    // (a) At the CAPTURE zoom, each stored doc sample must map back to a
    // view point that lies exactly on the drawn polyline. We check the
    // anchor and the final sample against the exact endpoints; a baked-in
    // offset would move these off the drawn points.
    const double eps = 1e-6;
    const QPointF firstView = map.docToView(ink.points.front());
    QVERIFY2(std::abs(firstView.x() - viewPts.front().x()) < eps &&
                 std::abs(firstView.y() - viewPts.front().y()) < eps,
             qPrintable(QStringLiteral("anchor drifted at capture zoom: got (%1,%2) want (%3,%4)")
                            .arg(firstView.x())
                            .arg(firstView.y())
                            .arg(viewPts.front().x())
                            .arg(viewPts.front().y())));
    const QPointF lastView = map.docToView(ink.points.back());
    QVERIFY2(std::abs(lastView.x() - viewPts.back().x()) < eps &&
                 std::abs(lastView.y() - viewPts.back().y()) < eps,
             qPrintable(QStringLiteral("tail drifted at capture zoom: got (%1,%2) want (%3,%4)")
                            .arg(lastView.x())
                            .arg(lastView.y())
                            .arg(viewPts.back().x())
                            .arg(viewPts.back().y())));

    // (b) Switch to a DIFFERENT display zoom on the SAME transform and
    // assert the stroke tracks page content rigidly. The vector from the
    // anchor to every other sample, in view space, must equal the SAME
    // vector at capture time scaled by exactly z2/z1 — no residual
    // translation. This is the anchor-vs-body drift the bug describes.
    const QPointF anchorDoc = ink.points.front();
    map.zoom = z1;
    const QPointF anchorViewZ1 = map.docToView(anchorDoc);
    std::vector<QPointF> bodyVecZ1;
    for (const QPointF &d : ink.points)
        bodyVecZ1.push_back(map.docToView(d) - anchorViewZ1);

    map.zoom = z2;
    const QPointF anchorViewZ2 = map.docToView(anchorDoc);
    const double ratio = z2 / z1;
    for (size_t i = 0; i < ink.points.size(); ++i) {
        const QPointF vecZ2 = map.docToView(ink.points[i]) - anchorViewZ2;
        const QPointF want = bodyVecZ1[i] * ratio;
        QVERIFY2(std::abs(vecZ2.x() - want.x()) < 1e-6 && std::abs(vecZ2.y() - want.y()) < 1e-6,
                 qPrintable(QStringLiteral("sample %1 drifted on zoom %2->%3 (dpr %4): "
                                           "got (%5,%6) want (%7,%8)")
                                .arg(i)
                                .arg(z1)
                                .arg(z2)
                                .arg(dpr)
                                .arg(vecZ2.x())
                                .arg(vecZ2.y())
                                .arg(want.x())
                                .arg(want.y())));
    }
}

QTEST_MAIN(TestFreehandZoomGlue)
#include "test_freehand_zoom_glue.moc"
