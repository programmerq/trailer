// Structural performance test (c): paint/relayout budget for one basic
// interaction.
//
// A single direct interaction (here: one zoom-in step on an open image)
// must not trigger a storm of repaints or relayouts — the view repaints
// its content a small, bounded number of times, not once per child or in
// a feedback loop. This is a DETERMINISTIC, CI-safe STRUCTURAL assertion:
// it counts QEvent::Paint / QEvent::LayoutRequest against a FIXED budget
// and never measures elapsed time. Event pumping (processEvents / qWait)
// is used only to flush queued events, not as a timing oracle.
//
// Runs under QT_QPA_PLATFORM=offscreen (set by CTest env / the workflow),
// so it is hermetic: no real display, no network, no external state
// beyond the checked-in corpus image.

#include "document/ImageAdapter.h"

#include <QEvent>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>
#include <QtTest/QtTest>

using namespace trailer;

namespace {
QString corpus(const QString &name) {
    return QStringLiteral("%1/%2").arg(QStringLiteral(TRAILER_PERF_CORPUS_DIR), name);
}

// Counts paint and layout-request events delivered to the widget it is
// installed on.
class EventCounter : public QObject {
  public:
    int paints = 0;
    int layouts = 0;

  protected:
    bool eventFilter(QObject *obj, QEvent *e) override {
        if (e->type() == QEvent::Paint)
            ++paints;
        else if (e->type() == QEvent::LayoutRequest)
            ++layouts;
        return QObject::eventFilter(obj, e);
    }
};
} // namespace

class TestPerfPaintBudget : public QObject {
    Q_OBJECT
  private slots:
    void singleZoomStepStaysWithinPaintBudget();
};

void TestPerfPaintBudget::singleZoomStepStaysWithinPaintBudget() {
    ImageAdapter adapter;
    auto doc = adapter.open(corpus(QStringLiteral("photo.jpg")));
    QVERIFY2(doc != nullptr, "image corpus must open");

    QWidget host;
    auto *layout = new QVBoxLayout(&host);
    layout->setContentsMargins(0, 0, 0, 0);
    QWidget *view = doc->createView(&host);
    QVERIFY(view);
    layout->addWidget(view);
    host.resize(800, 600);
    host.show();

    // Drain the initial show: first paints, plus the deferred
    // fit-to-content zoom (a QTimer::singleShot(0) inside createView).
    for (int i = 0; i < 5; ++i)
        QCoreApplication::processEvents();
    QTest::qWait(100);
    QCoreApplication::processEvents();

    // The image surface is the QScrollArea's inner widget (a QLabel).
    auto *scroll = qobject_cast<QScrollArea *>(view);
    QVERIFY2(scroll, "image view is a QScrollArea");
    QWidget *surface = scroll->widget();
    QVERIFY2(surface, "scroll area must host an image surface");

    EventCounter counter;
    surface->installEventFilter(&counter);
    scroll->installEventFilter(&counter);

    // ONE basic interaction: a single zoom-in step.
    doc->zoomIn();

    // Flush the repaint(s) the interaction queued.
    for (int i = 0; i < 5; ++i)
        QCoreApplication::processEvents();
    QTest::qWait(50);
    QCoreApplication::processEvents();

    qInfo() << "single zoom step produced" << counter.paints << "paints and" << counter.layouts
            << "layout requests";

    // Budget rationale: a single zoom step re-scales the pixmap and
    // updates the surface geometry once. Under the offscreen backing
    // store, paints coalesce, so the surface should repaint only a
    // handful of times. We set a FIXED budget of 8 with headroom over
    // the ~1-2 paints a clean single re-scale produces, so the test is
    // robust on a slow, variable CI runner but still catches a
    // repaint-storm regression (dozens of paints from a feedback loop).
    constexpr int kPaintBudget = 8;
    constexpr int kLayoutBudget = 8;
    QVERIFY2(counter.paints <= kPaintBudget,
             qPrintable(QStringLiteral("one zoom step exceeded the paint budget: %1 > %2")
                            .arg(counter.paints)
                            .arg(kPaintBudget)));
    QVERIFY2(counter.layouts <= kLayoutBudget,
             qPrintable(QStringLiteral("one zoom step exceeded the layout budget: %1 > %2")
                            .arg(counter.layouts)
                            .arg(kLayoutBudget)));
}

QTEST_MAIN(TestPerfPaintBudget)
#include "test_perf_paint_budget.moc"
