// Bug 2 regression guard: freehand (Ink) drawing must repaint an
// interactive, BOUNDED region per mouse-move — not the whole widget.
//
// The interactivity defect: every Ink mouse-move called a bare update()
// (unclipped full-widget repaint) which re-rendered all committed
// annotations + all search highlights + the entire growing in-progress
// QPainterPath, antialiased, on EVERY move. Cost grew with committed
// annotation count and stroke length, so a long stroke over a busy page
// felt very slow.
//
// The fix clips the mid-drag update() to the newly-added stroke segment.
// This test asserts the DETERMINISTIC structural proxy the fix targets:
// the dirty-region area delivered to paintEvent per mouse-move stays
// under a fixed budget that is a small fraction of the widget area, and
// crucially does NOT grow as the stroke gets longer. Before the fix each
// mid-drag paint covered the full 900x700 widget (630000 px^2); after,
// each covers only a small segment neighbourhood.
//
// Structural + deterministic: it counts painted pixel AREA (from the
// QPaintEvent region), never wall-clock time. Runs offscreen.

#include "annotation/Annotation.h"
#include "annotation/AnnotationStore.h"
#include "ui/AnnotationOverlay.h"

#include <QMouseEvent>
#include <QPaintEvent>
#include <QWidget>
#include <QtTest/QtTest>

#include <vector>

using namespace trailer;

namespace {

// Records the bounding-rect area of every Paint event delivered to the
// widget it is installed on.
class PaintAreaProbe : public QObject {
  public:
    std::vector<qint64> areas;
    bool armed = false;

  protected:
    bool eventFilter(QObject *obj, QEvent *e) override {
        if (armed && e->type() == QEvent::Paint) {
            auto *pe = static_cast<QPaintEvent *>(e);
            const QRect r = pe->rect();
            areas.push_back(static_cast<qint64>(r.width()) * r.height());
        }
        return QObject::eventFilter(obj, e);
    }
};

void sendMouse(QWidget *w, QEvent::Type type, QPointF pos, Qt::MouseButton button) {
    const Qt::MouseButtons held =
        (type == QEvent::MouseButtonRelease) ? Qt::NoButton : Qt::MouseButtons(button);
    QMouseEvent ev(type, pos, w->mapToGlobal(pos.toPoint()), button, held, Qt::NoModifier);
    QApplication::sendEvent(w, &ev);
}

} // namespace

class TestPerfFreehandRepaint : public QObject {
    Q_OBJECT
  private slots:
    void inkMoveRepaintsBoundedRegion();
};

void TestPerfFreehandRepaint::inkMoveRepaintsBoundedRegion() {
    QWidget host;
    const int W = 900, H = 700;
    host.resize(W, H);

    AnnotationStore store;
    // Seed the page with committed annotations so the "redraw everything
    // each move" regression would be expensive and full-widget.
    for (int i = 0; i < 40; ++i) {
        Annotation a;
        a.type = AnnotationType::Rectangle;
        a.page = 0;
        a.bounds = QRectF(10 + i * 5, 10 + i * 5, 60, 40);
        store.add(a);
    }

    auto *overlay = new AnnotationOverlay(&host);
    overlay->setGeometry(host.rect());
    overlay->setStore(&store);
    overlay->setDocumentToView([](QPointF p, int) { return p; });
    overlay->setViewToDocument([](QPointF v, int) { return v; });
    overlay->setPageAtViewPoint([](QPointF) { return 0; });
    overlay->setActiveTool(AnnotationTool::Ink);

    host.show();
    for (int i = 0; i < 5; ++i)
        QCoreApplication::processEvents();
    QTest::qWait(30);
    QCoreApplication::processEvents();

    PaintAreaProbe probe;
    overlay->installEventFilter(&probe);

    // A long freehand stroke: many samples spread across the widget so a
    // full-stroke-bbox repaint would be large and grow with length.
    std::vector<QPointF> pts;
    for (int i = 0; i < 30; ++i)
        pts.push_back(QPointF(100 + i * 20, 100 + i * 15));

    sendMouse(overlay, QEvent::MouseButtonPress, pts.front(), Qt::LeftButton);
    QCoreApplication::processEvents();

    // Arm the probe AFTER the press so we only measure mid-drag moves.
    probe.armed = true;
    for (size_t i = 1; i < pts.size(); ++i) {
        sendMouse(overlay, QEvent::MouseMove, pts[i], Qt::LeftButton);
        QCoreApplication::processEvents();
    }
    probe.armed = false;
    sendMouse(overlay, QEvent::MouseButtonRelease, pts.back(), Qt::LeftButton);
    QCoreApplication::processEvents();

    QCOMPARE(store.count(), 41); // 40 seeded + 1 committed stroke

    QVERIFY2(!probe.areas.empty(), "no mid-drag repaints were observed");

    qint64 maxArea = 0;
    for (qint64 a : probe.areas)
        maxArea = std::max(maxArea, a);
    const qint64 fullWidget = static_cast<qint64>(W) * H;
    qInfo() << "mid-drag paints:" << probe.areas.size() << "max dirty area:" << maxArea
            << "px^2 (full widget:" << fullWidget << "px^2)";

    // Budget: the widest mid-drag repaint must be a small fraction of the
    // full widget. A single Ink segment here spans ~25px inflated by the
    // pen-width margin, so its dirty rect is well under a 220x220 box.
    // The pre-fix bare update() painted the entire 900x700 widget every
    // move (630000 px^2), which blows past this budget by >13x.
    constexpr qint64 kBudget = 220 * 220; // 48400 px^2
    QVERIFY2(maxArea <= kBudget,
             qPrintable(QStringLiteral("mid-drag repaint area %1 px^2 exceeds budget %2 px^2 "
                                       "(full widget %3) — repaint is not clipped to the segment")
                            .arg(maxArea)
                            .arg(kBudget)
                            .arg(fullWidget)));
}

QTEST_MAIN(TestPerfFreehandRepaint)
#include "test_perf_freehand_repaint.moc"
