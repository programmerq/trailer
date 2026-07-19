// Structural performance test (c): asynchronous document open.
//
// P0 regression guard for the startup-hang fix (closed by #63; residual in
// docs/backlog/2026-07-15-offthread-pdf-open-placeholder.md).
// Opening a large annotated PDF used to freeze the app: the all-pages
// annotation sweep (PdfEditor::readAnnotations, ~12s on a 195MB/1M-annotation
// document) ran synchronously on the GUI thread — first in the PdfDocument
// ctor, then (after the lazy refactor) via annotations() at view-attach.
// The sweep now runs on a BACKGROUND worker loading a throwaway, isolated
// qpdf instance; annotations() kicks it and returns the (initially empty)
// store immediately, and the result is committed on the GUI thread later.
//
// These are DETERMINISTIC, CI-safe STRUCTURAL assertions: they read
// PdfEditor's always-compiled instrumentation (page-visit count, sweep
// thread identity) and a QSignalSpy on the store — never wall-clock time —
// so the result is independent of the runner's speed. Uses the small
// checked-in corpus (text_20page.pdf).

#include "annotation/AnnotationStore.h"
#include "document/DocumentRegistry.h"
#include "document/PdfAdapter.h"
#include "document/PdfEditor.h"

#include <QSignalSpy>
#include <QThread>
#include <QThreadPool>
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
    // The instrumentation counters (parse / page-visit / sweep-thread) are
    // process-global, and ~PdfDocument deliberately does NOT await the
    // detached annotation-sweep worker (closing a tab mid-load must never
    // re-freeze the GUI). A prior slot's still-running sweep would otherwise
    // tick this slot's counters right after resetInstrumentation(), tripping
    // the QCOMPARE(annotationPageVisits(), 0) / sweepThread == nullptr checks
    // non-deterministically on a loaded runner. Drain the global pool the
    // workers run on after each slot so every slot starts from a fully quiesced
    // pool before it resets and re-observes the counters.
    void cleanup() { QThreadPool::globalInstance()->waitForDone(); }

    void annotationSweepDoesNotRunSynchronously();
    void annotationSweepRunsOnWorkerThread();
    void annotationCommitEmitsSingleChanged();
    void editorParseDeferredUntilNeeded();
    void formCapabilityProbeDoesNotParseSynchronously();
    void formCapabilityBecomesAvailableAfterAsyncLoad();
};

// Proxy #1 (non-negotiable): the all-pages annotation sweep does NOT run on
// the calling (GUI) thread during open()+annotations(). It runs
// asynchronously, so the page-visit counter is still zero the instant
// annotations() returns, and only becomes non-zero after the event loop
// turns.
//
// Fail-first: against the synchronous (pre-async) code, annotations()
// forces the sweep inline, so annotationPageVisits() would already be 20
// here and the QCOMPARE(..., 0) would fail.
void TestPerfLazyOpen::annotationSweepDoesNotRunSynchronously() {
    PdfEditor::resetInstrumentation();

    DocumentRegistry registry;
    registry.registerAdapter(std::make_unique<PdfAdapter>());
    auto doc = openPdf(registry, QStringLiteral("text_20page.pdf"));

    QVERIFY(doc != nullptr);
    QCOMPARE(doc->pageCount(), 20);

    AnnotationStore *store = doc->annotations(); // kicks the async load
    QVERIFY(store != nullptr);

    // IMMEDIATELY, before spinning the event loop: the store is empty and the
    // per-page sweep has not run on this thread.
    QVERIFY(store->isEmpty());
    QCOMPARE(PdfEditor::annotationPageVisits(), 0);

    // Pump the event loop: the worker runs and the finished slot commits.
    QTRY_VERIFY(PdfEditor::annotationPageVisits() > 0);
}

// Liveness / ordering proxy (deterministic): the sweep executes on a
// DIFFERENT QThread than the caller — proving the GUI thread is not the one
// blocked doing the ~12s walk. Also proves ordering: the store is available
// (empty) synchronously and populates only after event-loop turns.
//
// Fail-first: against the synchronous code the sweep runs inline on the
// caller thread, so annotationSweepThread() would equal the test thread and
// the QVERIFY(sweepThread != guiThread) would fail (and the store would be
// non-empty synchronously).
void TestPerfLazyOpen::annotationSweepRunsOnWorkerThread() {
    PdfEditor::resetInstrumentation();

    DocumentRegistry registry;
    registry.registerAdapter(std::make_unique<PdfAdapter>());
    auto doc = openPdf(registry, QStringLiteral("text_20page.pdf"));
    QVERIFY(doc != nullptr);

    QThread *guiThread = QThread::currentThread();
    AnnotationStore *store = doc->annotations();
    QVERIFY(store != nullptr);

    // Ordering: view/store are usable before the sweep completes.
    QVERIFY(store->isEmpty());
    QVERIFY(PdfEditor::annotationSweepThread() == nullptr);

    // Wait for the sweep to run, then assert it ran off the GUI thread.
    QTRY_VERIFY(PdfEditor::annotationSweepThread() != nullptr);
    QVERIFY(PdfEditor::annotationSweepThread() != guiThread);
}

// The GUI-thread commit publishes the whole loaded set with EXACTLY ONE
// AnnotationStore::changed — never one signal per annotation. On the tiny
// corpus the loaded set is empty, but the single coalesced commit signal
// must still fire exactly once so consumers (overlay/sidebar/inspector)
// refresh out of their initial empty state.
void TestPerfLazyOpen::annotationCommitEmitsSingleChanged() {
    PdfEditor::resetInstrumentation();

    DocumentRegistry registry;
    registry.registerAdapter(std::make_unique<PdfAdapter>());
    auto doc = openPdf(registry, QStringLiteral("text_20page.pdf"));
    QVERIFY(doc != nullptr);

    AnnotationStore *store = doc->annotations();
    QVERIFY(store != nullptr);
    QSignalSpy spy(store, &AnnotationStore::changed);

    // No synchronous commit.
    QCOMPARE(spy.count(), 0);

    // The async commit emits changed() exactly once (batched populate).
    QTRY_COMPARE(spy.count(), 1);
    // And it does not keep emitting: settle a couple more turns.
    QTest::qWait(20);
    QCOMPARE(spy.count(), 1);
}

// Proxy #2 (best-effort against call sites): the qpdf processFile parse does
// not run when pageCount() first returns. It loads lazily — here on the
// annotation worker (a throwaway PdfEditor::load), so the parse counter is
// still zero right after open and becomes non-zero after the async sweep.
void TestPerfLazyOpen::editorParseDeferredUntilNeeded() {
    PdfEditor::resetInstrumentation();

    DocumentRegistry registry;
    registry.registerAdapter(std::make_unique<PdfAdapter>());
    auto doc = openPdf(registry, QStringLiteral("text_20page.pdf"));

    QVERIFY(doc != nullptr);
    QVERIFY(doc->pageCount() > 0);
    // No qpdf parse before the viewer is usable.
    QCOMPARE(PdfEditor::parseCount(), 0);

    // Kicking the annotation load parses (on the worker) exactly once.
    (void)doc->annotations();
    QTRY_VERIFY(PdfEditor::parseCount() >= 1);
}

// Owner feedback on PR #63: the qpdf editor parse (~0.55s) that
// supportsFormFilling() forced synchronously at open must NOT block the GUI
// thread. supportsFormFilling() now returns a provisional "not ready" (false)
// and the parse + form detection run on the SAME background worker as the
// annotation sweep; the forms toolbar becomes available a moment after open.
//
// Proxy: the capability probe does not parse on the synchronous open path.
// Reset counters, open, simulate the onCurrentDocumentChanged capability
// queries — parseCount() is still 0 the instant they return (no sync parse).
// Then pump the event loop: the parse runs (parseCount() >= 1) on a WORKER
// thread (parseThread() != GUI thread).
//
// Fail-first: against the pre-change code supportsFormFilling() forces a
// synchronous ensureEditorLoaded() → PdfEditor::load() on the GUI thread, so
// parseCount() is already 1 (QCOMPARE(..., 0) fails) and, if it did reach the
// pump, parseThread() would equal the GUI thread.
void TestPerfLazyOpen::formCapabilityProbeDoesNotParseSynchronously() {
    PdfEditor::resetInstrumentation();

    DocumentRegistry registry;
    registry.registerAdapter(std::make_unique<PdfAdapter>());
    auto doc = openPdf(registry, QStringLiteral("form_1page.pdf"));
    QVERIFY(doc != nullptr);
    QCOMPARE(doc->pageCount(), 1);

    QThread *guiThread = QThread::currentThread();

    // Simulate onCurrentDocumentChanged's capability queries. None of these
    // may force a qpdf parse on the calling (GUI) thread.
    (void)doc->supportsFormFilling();
    (void)doc->annotations(); // kicks the unified background load

    // IMMEDIATELY, before spinning the event loop: no parse has run here.
    QCOMPARE(PdfEditor::parseCount(), 0);

    // Pump the event loop: the background worker parses + detects forms.
    QTRY_VERIFY(PdfEditor::parseCount() >= 1);
    QVERIFY(PdfEditor::parseThread() != nullptr);
    QVERIFY(PdfEditor::parseThread() != guiThread);
}

// The forms capability resolves asynchronously: supportsFormFilling() is false
// synchronously right after open (the editor parse has not run yet) and turns
// true once the background load has produced the editor + form detection.
//
// Fail-first: against the pre-change code supportsFormFilling() forces the
// parse inline and returns true synchronously, so QVERIFY(!supportsFormFilling())
// fails immediately.
void TestPerfLazyOpen::formCapabilityBecomesAvailableAfterAsyncLoad() {
    PdfEditor::resetInstrumentation();

    DocumentRegistry registry;
    registry.registerAdapter(std::make_unique<PdfAdapter>());
    auto doc = openPdf(registry, QStringLiteral("form_1page.pdf"));
    QVERIFY(doc != nullptr);

    // Provisional "not ready yet" synchronously at open — kicks the load.
    QVERIFY(!doc->supportsFormFilling());

    // After the background load completes, the AcroForm is detected.
    QTRY_VERIFY(doc->supportsFormFilling());
    QVERIFY(!doc->formFields().empty());
}

QTEST_MAIN(TestPerfLazyOpen)
#include "test_perf_lazy_open.moc"
