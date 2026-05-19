// Unit tests for SelectableTextLayer — cursor logic, drag-selection
// snapping to block boundaries, clipboard copy on Ctrl+C, and per-
// page selection lifecycle.
//
// The layer is a QWidget; we instantiate one without a parent so the
// hit-test maths can be exercised without a real document view
// underneath.

#include "document/SelectableTextStore.h"
#include "ui/SelectableTextLayer.h"

#include <QApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QPolygon>
#include <QtTest/QtTest>

using namespace trailer;

namespace {

OcrEngine::TextBlock blockAt(const QString &text, int x, int y, int w, int h) {
    OcrEngine::TextBlock b;
    b.text = text;
    b.polygon = QPolygon({{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}});
    b.confidence = 0.95f;
    return b;
}

} // namespace

class TestSelectableTextLayer : public QObject {
    Q_OBJECT
  private slots:
    void cursorIsIBeamOverBlockOnly();
    void dragInsideSingleBlockSelectsIt();
    void dragAcrossTwoBlocksJoinsTextInReadingOrder();
    void clickingEmptySpaceClearsSelection();
    void copySelectionGoesToClipboard();
    void pageChangeClearsSelection();
};

void TestSelectableTextLayer::cursorIsIBeamOverBlockOnly() {
    SelectableTextStore store;
    store.put(0, 1ULL, {blockAt("Foo", 10, 10, 100, 20)});

    SelectableTextLayer layer;
    layer.resize(500, 500);
    layer.setStore(&store);
    layer.setCurrentPage(0);
    // identity mapping
    layer.setDocToView([](QPointF p, int) { return p; });

    // Inside the block polygon.
    QCOMPARE(layer.cursorShapeFor(QPointF(50, 20)), Qt::IBeamCursor);
    // Outside any block.
    QCOMPARE(layer.cursorShapeFor(QPointF(300, 300)), Qt::ArrowCursor);
    // Right on the boundary (corner) — defined to be inside.
    QCOMPARE(layer.cursorShapeFor(QPointF(10, 10)), Qt::IBeamCursor);
}

void TestSelectableTextLayer::dragInsideSingleBlockSelectsIt() {
    SelectableTextStore store;
    store.put(0, 1ULL, {blockAt("Hello world", 0, 0, 200, 40)});

    SelectableTextLayer layer;
    layer.resize(400, 400);
    layer.setStore(&store);
    layer.setCurrentPage(0);
    layer.setDocToView([](QPointF p, int) { return p; });

    const QString selected = layer.simulateDragForTest(QPointF(10, 10), QPointF(150, 30));
    QCOMPARE(selected, QStringLiteral("Hello world"));
    QCOMPARE(layer.selectedBlockCount(), 1);
}

void TestSelectableTextLayer::dragAcrossTwoBlocksJoinsTextInReadingOrder() {
    SelectableTextStore store;
    // First block on the top line, second block one line below it.
    store.put(0, 1ULL,
              {blockAt("First line", 0, 0, 200, 30),
               blockAt("Second line", 0, 50, 200, 30)});

    SelectableTextLayer layer;
    layer.resize(400, 400);
    layer.setStore(&store);
    layer.setCurrentPage(0);
    layer.setDocToView([](QPointF p, int) { return p; });

    // Drag a big rect that includes both block centroids.
    const QString s = layer.simulateDragForTest(QPointF(5, 5), QPointF(190, 75));
    QCOMPARE(layer.selectedBlockCount(), 2);
    // Reading order: top line first, then second.
    QCOMPARE(s, QStringLiteral("First line\nSecond line"));

    // Reverse drag (bottom-to-top) — selection order should still
    // reflect the reading order, not the drag direction.
    const QString s2 = layer.simulateDragForTest(QPointF(190, 75), QPointF(5, 5));
    QCOMPARE(s2, QStringLiteral("First line\nSecond line"));
}

void TestSelectableTextLayer::clickingEmptySpaceClearsSelection() {
    SelectableTextStore store;
    store.put(0, 1ULL, {blockAt("Foo", 10, 10, 100, 20)});

    SelectableTextLayer layer;
    layer.resize(400, 400);
    layer.setStore(&store);
    layer.setCurrentPage(0);
    layer.setDocToView([](QPointF p, int) { return p; });

    layer.simulateDragForTest(QPointF(15, 15), QPointF(100, 25));
    QCOMPARE(layer.selectedBlockCount(), 1);

    // Synthesise a press in empty space: simulateDrag with both points
    // in empty space picks zero blocks via the centroid test.
    layer.simulateDragForTest(QPointF(300, 300), QPointF(310, 310));
    QCOMPARE(layer.selectedBlockCount(), 0);
}

void TestSelectableTextLayer::copySelectionGoesToClipboard() {
    SelectableTextStore store;
    store.put(0, 1ULL, {blockAt("ClipboardLine", 0, 0, 200, 40)});

    SelectableTextLayer layer;
    layer.resize(400, 400);
    layer.setStore(&store);
    layer.setCurrentPage(0);
    layer.setDocToView([](QPointF p, int) { return p; });

    layer.simulateDragForTest(QPointF(10, 10), QPointF(150, 30));
    QCOMPARE(layer.selectedText(), QStringLiteral("ClipboardLine"));

    QClipboard *clip = QApplication::clipboard();
    clip->clear();
    QKeyEvent copy(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier);
    QApplication::sendEvent(&layer, &copy);
    QCOMPARE(clip->text(), QStringLiteral("ClipboardLine"));
}

void TestSelectableTextLayer::pageChangeClearsSelection() {
    SelectableTextStore store;
    store.put(0, 1ULL, {blockAt("PageZero", 0, 0, 200, 40)});
    store.put(1, 2ULL, {blockAt("PageOne", 0, 0, 200, 40)});

    SelectableTextLayer layer;
    layer.resize(400, 400);
    layer.setStore(&store);
    layer.setCurrentPage(0);
    layer.setDocToView([](QPointF p, int) { return p; });

    layer.simulateDragForTest(QPointF(10, 10), QPointF(150, 30));
    QCOMPARE(layer.selectedText(), QStringLiteral("PageZero"));

    layer.setCurrentPage(1);
    QCOMPARE(layer.selectedBlockCount(), 0);
    QCOMPARE(layer.selectedText(), QString());
}

QTEST_MAIN(TestSelectableTextLayer)
#include "test_selectable_text_layer.moc"
