// Bug 3 regression guard: with a freehand (Ink) drawing tool active, a
// press that lands on top of an existing annotation must START A NEW
// STROKE, not hijack into select/move of the annotation underneath.
//
// This mirrors Preview/Acrobat: a drawing tool press is always "new
// mark", regardless of what is under the cursor. The select-and-move
// gesture belongs to the Select tool only (UAT-ANN-120 pins single-click
// selection as a Select-tool affordance).
//
// Runs offscreen, hermetic, deterministic.

#include "annotation/AnnotationStore.h"
#include "ui/AnnotationOverlay.h"

#include <QMouseEvent>
#include <QWidget>
#include <QtTest/QtTest>

#include <vector>

using namespace trailer;

namespace {

void sendMouse(QWidget *w, QEvent::Type type, QPointF pos, Qt::MouseButton button) {
    const Qt::MouseButtons held =
        (type == QEvent::MouseButtonRelease) ? Qt::NoButton : Qt::MouseButtons(button);
    QMouseEvent ev(type, pos, w->mapToGlobal(pos.toPoint()), button, held, Qt::NoModifier);
    QApplication::sendEvent(w, &ev);
}

// Drives a freehand stroke: press, N moves, release. Uses several
// intermediate samples so the Ink commit (which needs >= 2 points) fires.
void drawStroke(AnnotationOverlay *overlay, const std::vector<QPointF> &pts) {
    sendMouse(overlay, QEvent::MouseButtonPress, pts.front(), Qt::LeftButton);
    for (size_t i = 1; i < pts.size(); ++i)
        sendMouse(overlay, QEvent::MouseMove, pts[i], Qt::LeftButton);
    sendMouse(overlay, QEvent::MouseButtonRelease, pts.back(), Qt::LeftButton);
    QApplication::processEvents();
}

} // namespace

class TestFreehandSelectionPrecedence : public QObject {
    Q_OBJECT
  private slots:
    void inkPressOnExistingAnnotationStartsNewStroke();
    void selectToolStillSelectsOnClick();
};

// With Ink active, pressing INSIDE an existing Ink annotation's bounds
// must create a second, independent stroke — the original must be
// neither selected nor moved.
void TestFreehandSelectionPrecedence::inkPressOnExistingAnnotationStartsNewStroke() {
    QWidget host;
    host.resize(800, 600);
    AnnotationStore store;
    AnnotationOverlay overlay(&host);
    overlay.setGeometry(host.rect());
    overlay.setStore(&store);
    // Identity transform: view coords == doc coords, so press positions
    // land directly inside annotation doc-space bounds.
    overlay.setDocumentToView([](QPointF p, int) { return p; });
    overlay.setViewToDocument([](QPointF v, int) { return v; });
    overlay.setPageAtViewPoint([](QPointF) { return 0; });
    overlay.setActiveTool(AnnotationTool::Ink);

    // First stroke — a committed Ink annotation spanning (100,100)-(200,200).
    drawStroke(&overlay, {{100, 100}, {150, 150}, {200, 200}});
    QCOMPARE(store.count(), 1);
    const Annotation first = store.annotations().back();
    QCOMPARE(first.type, AnnotationType::Ink);
    const int firstId = first.id;
    const std::vector<QPointF> firstPoints = first.points;
    const QRectF firstBounds = first.bounds;
    // Committing an Ink stroke does not select it.
    QCOMPARE(overlay.selectedAnnotationId(), 0);

    // Second stroke — press STARTS INSIDE the first annotation's bounds.
    QVERIFY2(firstBounds.contains(QPointF(130, 130)),
             "test setup: press point must be inside the first annotation's bounds");
    drawStroke(&overlay, {{130, 130}, {300, 130}, {360, 130}});

    // A NEW Ink annotation must exist; the original must be untouched
    // and unselected.
    QCOMPARE(store.count(), 2);
    QCOMPARE(store.annotations().back().type, AnnotationType::Ink);
    QVERIFY2(store.annotations().back().id != firstId,
             "second stroke must be a distinct annotation, not a mutation of the first");
    QCOMPARE(overlay.selectedAnnotationId(), 0);

    const Annotation *orig = store.find(firstId);
    QVERIFY2(orig != nullptr, "original annotation must still exist");
    QCOMPARE(orig->points.size(), firstPoints.size());
    for (size_t i = 0; i < firstPoints.size(); ++i) {
        QVERIFY2(qFuzzyCompare(orig->points[i].x() + 1.0, firstPoints[i].x() + 1.0) &&
                     qFuzzyCompare(orig->points[i].y() + 1.0, firstPoints[i].y() + 1.0),
                 "original stroke points must be unchanged (not moved)");
    }
}

// Guard the other side of the precedence rule: with the Select tool
// active, a click on an existing annotation still selects it. This is
// the UAT-ANN-120 behaviour the Bug 3 fix must preserve.
void TestFreehandSelectionPrecedence::selectToolStillSelectsOnClick() {
    QWidget host;
    host.resize(800, 600);
    AnnotationStore store;
    AnnotationOverlay overlay(&host);
    overlay.setGeometry(host.rect());
    overlay.setStore(&store);
    overlay.setDocumentToView([](QPointF p, int) { return p; });
    overlay.setViewToDocument([](QPointF v, int) { return v; });
    overlay.setPageAtViewPoint([](QPointF) { return 0; });

    // Create an Ink annotation with the Ink tool, then switch to Select.
    overlay.setActiveTool(AnnotationTool::Ink);
    drawStroke(&overlay, {{100, 100}, {150, 150}, {200, 200}});
    QCOMPARE(store.count(), 1);
    const int id = store.annotations().back().id;

    overlay.setActiveTool(AnnotationTool::Select);
    QCOMPARE(overlay.selectedAnnotationId(), 0);
    sendMouse(&overlay, QEvent::MouseButtonPress, QPointF(130, 130), Qt::LeftButton);
    sendMouse(&overlay, QEvent::MouseButtonRelease, QPointF(130, 130), Qt::LeftButton);
    QApplication::processEvents();

    QCOMPARE(overlay.selectedAnnotationId(), id);
    // A pure click (no drag) must not create anything.
    QCOMPARE(store.count(), 1);
}

QTEST_MAIN(TestFreehandSelectionPrecedence)
#include "test_freehand_selection_precedence.moc"
