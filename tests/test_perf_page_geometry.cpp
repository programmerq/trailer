// Structural performance test (f): page-geometry lookups are resolved once
// per loaded page graph, not once per coordinate conversion.
//
// THE DEFECT THIS GUARDS (measured, 2026-08-03). PdfDocument's
// document->view coordinate mapping resolved a page's origin by walking the
// WHOLE document twice — widest page, then running Y offset / total content
// height — calling QPdfDocument::pagePointSize() on every page each time.
// pagePointSize() is not a field read: it takes the document mutex and asks
// pdfium to resolve the page dictionary and its inherited /CropBox. Because
// that mapping runs per polygon point of every selectable-text block, per
// annotation, and per mouse press/drag, the real cost was O(points x pages).
// Opening one 2603-page PDF issued 739,417 pagePointSize calls and spent
// ~0.77 s of GUI-thread time in the first paint alone; the same document with
// twice the text per page spent ~1.53 s. Three such documents restored at
// launch is the "multiple seconds to load the app and/or the file" report.
//
// THE INVARIANT: every page's size is resolved EXACTLY ONCE per loaded page
// graph (PdfDocument::PageMetrics), so a coordinate conversion is O(1)
// arithmetic and repeating it is free.
//
// These are DETERMINISTIC, CI-safe STRUCTURAL assertions: they read
// PdfDocument's always-compiled instrumentation (a COUNT of calls that reach
// pdfium) — never elapsed time — so the result does not depend on the
// runner's speed. Uses the checked-in corpus (text_20page.pdf), whose page 0
// carries native text, so the selectable-text layer has real blocks to
// convert.

#include "document/DocumentRegistry.h"
#include "document/PdfAdapter.h"
#include "document/SelectableTextStore.h"
#include "ui/SelectableTextLayer.h"

#include <QThreadPool>
#include <QWidget>
#include <QtTest/QtTest>

#include <memory>

using namespace trailer;

namespace {
QString corpus(const QString &name) {
    return QStringLiteral("%1/%2").arg(QStringLiteral(TRAILER_PERF_CORPUS_DIR), name);
}
} // namespace

class TestPerfPageGeometry : public QObject {
    Q_OBJECT
  private slots:
    // The instrumentation counter is process-global and PdfDocument's
    // background load outlives a slot that drops its document early. Drain
    // the pool the workers run on so each slot starts from a quiesced state
    // before it resets and re-observes the counter (same reasoning as
    // test_perf_lazy_open.cpp::cleanup).
    void cleanup() { QThreadPool::globalInstance()->waitForDone(); }

    void openAndFirstConversionResolveEachPageSizeOnce();
    void repeatedCoordinateConversionsCostNoEngineLookups();
    void pageGraphMutationInvalidatesTheGeometryCache();
};

// Opening a document, building its view, AND running a full document->view
// conversion pass over page 0's text blocks together resolve each page's size
// EXACTLY once — one pass over the page graph in total, not one pass per
// consumer and certainly not one per converted point. The bound is a COUNT
// (== pageCount), so it holds identically on any machine.
//
// Fail-first: against the pre-fix code the origin lambda re-walked the whole
// document twice on every conversion, so the single rebuild below alone drove
// the counter to (2 * pageCount + 2) per converted point — this slot's
// QCOMPARE(calls, 20) failed with a number in the thousands. Confirmed by
// restoring the old loops behind the counter and re-running.
void TestPerfPageGeometry::openAndFirstConversionResolveEachPageSizeOnce() {
    PdfDocument::resetInstrumentation();

    DocumentRegistry registry;
    registry.registerAdapter(std::make_unique<PdfAdapter>());
    auto doc = registry.open(corpus(QStringLiteral("text_20page.pdf")));
    QVERIFY(doc != nullptr);
    const int pages = doc->pageCount();
    QCOMPARE(pages, 20);

    std::unique_ptr<QWidget> view(doc->createView(nullptr));
    QVERIFY(view != nullptr);

    // The open path itself must never re-walk the page graph per consumer.
    QVERIFY2(PdfDocument::pagePointSizeEngineCalls() <= qint64(pages),
             "open + createView must not resolve any page size more than once");

    auto *layer = view->findChild<SelectableTextLayer *>();
    QVERIFY2(layer != nullptr, "the PDF view must carry a SelectableTextLayer");
    auto *store = doc->selectableText();
    QVERIFY(store != nullptr);
    QVERIFY2(!store->blocks(0).empty(),
             "corpus page 0 must carry native text blocks, or this slot would "
             "assert nothing (the rebuild below would have no points to convert)");

    // One full view-block rebuild: converts every point of every text block on
    // the page through the document->view mapping.
    layer->setCurrentPage(0);
    (void)layer->isPointOverText(QPointF(10, 10));

    QCOMPARE(PdfDocument::pagePointSizeEngineCalls(), qint64(pages));
}

// The core of the fix: a coordinate conversion is O(1) arithmetic off the
// cached metrics, so driving the selectable-text layer's view-block rebuild
// repeatedly — each rebuild converting every text block on the page — costs
// ZERO further pdfium page-size lookups.
//
// Fail-first: against the pre-fix code each rebuild issued
// (2 * pageCount + 2) lookups per converted point, so this slot's
// QCOMPARE(after - before, 0) failed with tens of thousands.
void TestPerfPageGeometry::repeatedCoordinateConversionsCostNoEngineLookups() {
    DocumentRegistry registry;
    registry.registerAdapter(std::make_unique<PdfAdapter>());
    auto doc = registry.open(corpus(QStringLiteral("text_20page.pdf")));
    QVERIFY(doc != nullptr);

    std::unique_ptr<QWidget> view(doc->createView(nullptr));
    QVERIFY(view != nullptr);

    // The layer is a child of the QPdfView's viewport; createView() has
    // already ingested page 0's native text into the store.
    auto *layer = view->findChild<SelectableTextLayer *>();
    QVERIFY2(layer != nullptr, "the PDF view must carry a SelectableTextLayer");
    auto *store = doc->selectableText();
    QVERIFY(store != nullptr);
    QVERIFY2(!store->blocks(0).empty(),
             "corpus page 0 must carry native text blocks, or this slot would "
             "assert nothing (the rebuild would have no points to convert)");

    // Run one conversion pass first so the metrics are definitely warm, THEN
    // take the baseline — the delta below is purely the cost of re-converting.
    layer->setCurrentPage(0);
    (void)layer->isPointOverText(QPointF(10, 10));
    const qint64 before = PdfDocument::pagePointSizeEngineCalls();

    // setCurrentPage() dirties the layer's view-block cache; isPointOverText()
    // forces the rebuild, converting every point of every block on the page.
    // Alternate the page so every iteration is a genuine rebuild.
    for (int i = 0; i < 25; ++i) {
        layer->setCurrentPage(0);
        (void)layer->isPointOverText(QPointF(10, 10));
        layer->setCurrentPage(1);
        (void)layer->isPointOverText(QPointF(10, 10));
    }

    QCOMPARE(PdfDocument::pagePointSizeEngineCalls() - before, qint64(0));
}

// The cache must not go stale. A page-graph mutation (rotate swaps a page's
// width and height) reloads the viewer document, which must drop the metrics
// so the next conversion re-resolves them — exactly once more, not zero times
// (stale geometry) and not once per consumer (the defect returning).
//
// Unlike the two slots above this one is NOT a fail-first guard on the old
// defect (it passes against the pre-fix shape too, which had no cache to go
// stale). It guards the RISK the fix introduces — a cache whose invalidation
// is wrong would silently paint annotations and text selections at the
// pre-rotation geometry — so it must not be dropped as redundant.
void TestPerfPageGeometry::pageGraphMutationInvalidatesTheGeometryCache() {
    DocumentRegistry registry;
    registry.registerAdapter(std::make_unique<PdfAdapter>());
    auto doc = registry.open(corpus(QStringLiteral("text_20page.pdf")));
    QVERIFY(doc != nullptr);
    const int pages = doc->pageCount();
    QCOMPARE(pages, 20);

    PdfDocument::resetInstrumentation();

    // Cold read builds the metrics once; a second read is free (cache warm).
    const QSizeF beforeRotate = doc->pageSizeHint(0);
    QVERIFY(!beforeRotate.isEmpty());
    QCOMPARE(PdfDocument::pagePointSizeEngineCalls(), qint64(pages));
    (void)doc->pageSizeHint(0);
    QCOMPARE(PdfDocument::pagePointSizeEngineCalls(), qint64(pages));

    doc->rotatePage(0, 90);

    // Structural: the reload dropped the cache, so the next read pays for
    // exactly one fresh pass over the re-sized page graph — not zero (stale)
    // and not one pass per consumer (the defect returning).
    const QSizeF afterRotate = doc->pageSizeHint(0);
    QCOMPARE(PdfDocument::pagePointSizeEngineCalls(), qint64(2 * pages));

    // Behavioural: the freshly-resolved size reflects the rotation, which is
    // what a stale cache would have silently hidden.
    QCOMPARE(afterRotate.width(), beforeRotate.height());
    QCOMPARE(afterRotate.height(), beforeRotate.width());
}

QTEST_MAIN(TestPerfPageGeometry)
#include "test_perf_page_geometry.moc"
