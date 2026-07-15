// Structural performance test (c): lazy document open.
//
// P0 regression guard for docs/backlog/2026-07-13-startup-hang-large-pdf.md.
// Opening a large PDF used to freeze the app because the PdfDocument ctor
// ran three synchronous whole-document passes: the QPdfDocument load
// (kept — it is the bounded progressive read that yields pageCount +
// page-0 render), the qpdf processFile parse, and an all-pages annotation
// sweep. The latter two are now lazy — they must not run until a genuine
// editor / annotation access, not at construction / DocumentRegistry::open().
//
// These are DETERMINISTIC, CI-safe STRUCTURAL assertions: they read
// PdfEditor's always-compiled instrumentation counters (page visits /
// parse count), never wall-clock time, so the result is independent of
// the runner's speed. Uses the small checked-in corpus (text_20page.pdf).

#include "document/DocumentRegistry.h"
#include "document/PdfAdapter.h"
#include "document/PdfEditor.h"

#include <QtTest/QtTest>

using namespace trailer;

namespace {
QString corpus(const QString &name) {
    return QStringLiteral("%1/%2").arg(QStringLiteral(TRAILER_PERF_CORPUS_DIR), name);
}

std::unique_ptr<IDocument> openPdf(DocumentRegistry &registry, const QString &name) {
    return registry.open(corpus(name));
}
} // namespace

class TestPerfLazyOpen : public QObject {
    Q_OBJECT
  private slots:
    void annotationSweepDeferredUntilAccess();
    void editorParseDeferredUntilNeeded();
};

// Proxy #1 (non-negotiable): the all-pages annotation sweep does not run
// during DocumentRegistry::open(). It runs lazily on the first genuine
// annotation access (annotations()).
void TestPerfLazyOpen::annotationSweepDeferredUntilAccess() {
    PdfEditor::resetInstrumentation();

    DocumentRegistry registry;
    registry.registerAdapter(std::make_unique<PdfAdapter>());
    auto doc = openPdf(registry, QStringLiteral("text_20page.pdf"));

    QVERIFY(doc != nullptr);
    QCOMPARE(doc->pageCount(), 20);
    // The dominant cost — the per-page /Annots walk that forces
    // whole-document object resolution — must NOT have run at open.
    QCOMPARE(PdfEditor::annotationPageVisits(), 0);

    // First genuine access forces the sweep.
    (void)doc->annotations();
    QVERIFY(PdfEditor::annotationPageVisits() > 0);
}

// Proxy #2 (best-effort against call sites): the qpdf processFile parse
// does not run when pageCount() first returns. It loads lazily on the
// first genuine editor need (here, an annotation access, which needs the
// editor).
void TestPerfLazyOpen::editorParseDeferredUntilNeeded() {
    PdfEditor::resetInstrumentation();

    DocumentRegistry registry;
    registry.registerAdapter(std::make_unique<PdfAdapter>());
    auto doc = openPdf(registry, QStringLiteral("text_20page.pdf"));

    QVERIFY(doc != nullptr);
    QVERIFY(doc->pageCount() > 0);
    // No second full-file parse before the viewer is usable.
    QCOMPARE(PdfEditor::parseCount(), 0);

    // A genuine editor need loads it exactly once.
    (void)doc->annotations();
    QVERIFY(PdfEditor::parseCount() >= 1);
}

QTEST_MAIN(TestPerfLazyOpen)
#include "test_perf_lazy_open.moc"
