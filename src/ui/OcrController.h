#pragma once

#include "ml/MlScheduler.h"
#include "ml/OcrEngine.h"

#include <QImage>
#include <QObject>
#include <QVector>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

class QTimer;

namespace trailer {

class Application;
class CancellationToken;
class IDocument;
class SelectableTextStore;

// Coordinates OCR submissions for the active document of a single
// MainWindow. Holds a shared OcrEngine and a small bookkeeping table
// keyed by (document*, page) so we don't submit the same work twice.
//
// Submission shape (Workstream F):
//   - On every current-page change, submit OCR for the visible page
//     at `VisiblePage` priority. Cancel any pending VisiblePage that
//     no longer matches the new page.
//   - For documents with >1 page, submit ±1 neighbours at `Prefetch`
//     priority. Pending Prefetch for far pages gets cancelled when
//     the user jumps somewhere unrelated.
//   - The explicit Recognize Text… dialog submits work at
//     `UserAction` priority (higher than VisiblePage), with the
//     user's selected pages.
//   - Large-doc guard: documents with pageCount() > 50 skip the
//     automatic VisiblePage / Prefetch enqueue. The Recognize Text
//     dialog stays available; the per-page MainWindow status bar
//     offers an explicit "Recognize text on this page" affordance.
//
// All submissions go through Application::mlScheduler() so the
// scheduler's power policy (run-on-battery, etc.) and priority
// preemption apply uniformly. Cancellation is cooperative — the
// engine polls the token between detector / per-box recognition
// steps.
//
// The controller listens for SelectableTextStore::changed signals
// from each document's store so a manual user action that invalidates
// a page (rotate, crop) automatically re-enqueues the visible page.
class OcrController : public QObject {
    Q_OBJECT
  public:
    explicit OcrController(Application *app, QObject *parent = nullptr);
    ~OcrController() override;

    // Switch which document the controller follows. Pass nullptr when
    // the window has no current document. Cancels any in-flight
    // submissions for the previous document.
    void setDocument(IDocument *doc);
    IDocument *document() const { return m_doc; }

    // Notify the controller that the visible page changed. Submits a
    // VisiblePage OCR for the new page (if not cached + not in flight)
    // and Prefetch for neighbours. Cancels pending submissions that
    // are no longer relevant.
    void onVisiblePageChanged(int page);

    // Explicit Recognize Text… submission, called from
    // RecognizeTextDialog. Submits at UserAction priority. `pages` is
    // a list of 0-based page indices; if `forceRerun` is true, the
    // cache is invalidated for those pages before submission so a doc
    // that "has a text layer in a watermark only" can be re-OCR'd
    // even though hasTextLayer() returns true.
    //
    // This is the batch that drives the status-bar progress widget
    // (ADR 0002): it emits ocrBatchStarted / ocrBatchProgress /
    // ocrBatchFinished and, after the reveal delay, ocrBatchShouldReveal.
    void submitUserPages(IDocument *doc, std::vector<int> pages, bool forceRerun);

    // Cancel the active UserAction batch (ADR 0002 §2). Flips every
    // outstanding BATCH-TRACKED token so in-flight pages discard their
    // partial result and not-yet-started pages never run, stops the
    // completion count, and emits ocrBatchFinished(true). Idempotent — a
    // no-op when no batch is active. Ambient auto-OCR (visible-page ±1)
    // is deliberately NOT affected — only batch-tracked handles are
    // cancelled.
    void cancelActiveBatch();

    // Re-derive the missing-model in-context hint for the current
    // document/page, including the doc==null and non-OCR-document cases
    // (which emit autoOcrModelMissing(false) so a stale hint hides on a
    // document switch or close). ADR 0002 §3. Cheap; MainWindow calls it
    // on every current-document change.
    void refreshModelHint();

    // Reveal-delay threshold (ADR 0002 G2). A batch that finishes before
    // this elapses never reveals the progress widget. Settable so tests
    // can drive it to 0 (reveal immediately) or a large value (never)
    // without wall-clock waiting.
    void setProgressRevealDelayMs(int ms) { m_revealDelayMs = ms; }
    int progressRevealDelayMs() const { return m_revealDelayMs; }

    // Test seams (no-ops in production). setRecognizerForTesting swaps
    // the per-page recognition step so tests can drive deterministic
    // results and hold pages mid-flight without real ONNX models; when
    // set, the worker's isModelReady() gate is bypassed. setModelReady-
    // ForTesting forces the GUI-thread readiness check used by the
    // auto-OCR missing-model affordance.
    using RecognizeFn =
        std::function<QVector<OcrEngine::TextBlock>(const QImage &, const CancellationToken *)>;
    void setRecognizerForTesting(RecognizeFn fn) { m_recognizer = std::move(fn); }
    void setModelReadyForTesting(std::optional<bool> ready) { m_modelReadyOverride = ready; }

    // Cancel everything we have in flight. Used on window close /
    // doc replace.
    void cancelAll();

    // Page-count threshold above which we skip the automatic visible-
    // page enqueue. Exposed for tests; matches the value in the spec
    // (>50 pages).
    static constexpr int kLargeDocPageThreshold = 50;

    // True when the document's page count exceeds the auto-OCR
    // threshold. The MainWindow uses this to decide whether to show
    // the in-status-bar hint chip ("Text isn't selectable here").
    bool isLargeDoc() const;

  signals:
    // Emitted at the start of a submitUserPages batch with the number
    // of pages that will be OCR'd (ADR 0002 G1).
    void ocrBatchStarted(int total);
    // Emitted each time a page's result is resolved on the GUI thread.
    void ocrBatchProgress(int completed, int total);
    // Emitted once the batch ends: cancelled=false on natural
    // completion, cancelled=true after cancelActiveBatch().
    void ocrBatchFinished(bool cancelled);
    // Emitted when an active batch is torn down SILENTLY — superseded by
    // a fresh batch, or the document was switched/closed. MainWindow
    // drives the widget straight back to idle (no "cancelled — no changes
    // saved" terminal message) and disables the scoped cancel action.
    // Distinct from ocrBatchFinished so a supersede/teardown never flashes
    // the misleading cancel message (ADR 0002 review items 2/7).
    void ocrBatchAborted();
    // Emitted after the reveal delay iff the batch is still running
    // (ADR 0002 G2). MainWindow reveals the progress widget here, not on
    // ocrBatchStarted, so sub-threshold batches never flicker it.
    void ocrBatchShouldReveal();

    // State-driven auto-OCR readiness signal (ADR 0002 §3). true when
    // the visible document would auto-OCR but the language model is not
    // installed; false otherwise. Re-derived on every document/page
    // change so the in-context hint is persistent, not fire-once.
    void autoOcrModelMissing(bool missing);

  private:
    // Submission state tracked per (doc, page) so duplicate enqueues
    // and cancellations land in O(1).
    struct PendingKey {
        IDocument *doc = nullptr;
        int page = -1;
        bool operator==(const PendingKey &o) const noexcept {
            return doc == o.doc && page == o.page;
        }
    };
    // Value stored per pending key: the scheduler task id plus whether the
    // submission is part of a user-action batch. batchTracked lets
    // cancelActiveBatch() cancel ONLY batch handles and leave ambient
    // (visible-page ±1) submissions running (ADR 0002 review item 3).
    struct PendingEntry {
        MlTaskId id = 0;
        bool batchTracked = false;
    };
    struct PendingKeyHash {
        size_t operator()(const PendingKey &k) const noexcept {
            return std::hash<IDocument *>()(k.doc) ^ (std::hash<int>()(k.page) << 1);
        }
    };

    // Outcome of a single submitPage() call. Used by the batch driver to
    // decide whether a completion callback is still coming (Submitted)
    // or the page must be counted done immediately (Cached / Skipped).
    enum class SubmitResult { Submitted, Cached, Skipped };

    SubmitResult submitPage(IDocument *doc, int page, MlPriority priority, bool forceRerun,
                            bool batchTracked);
    void cancelKey(const PendingKey &key);
    void cancelPagesNotMatching(IDocument *doc, const std::vector<int> &keep);

    // GUI-thread readiness check backing the auto-OCR missing-model
    // affordance. Honours the test override when set.
    bool modelReady() const;

    // GUI-thread callback invoked when a batch page resolves (stored or
    // discarded). `epoch` identifies the batch that scheduled the page;
    // stragglers from a superseded/torn-down batch carry a stale epoch and
    // are ignored so they can't inflate the CURRENT batch's counter (ADR
    // 0002 review item 1).
    void onBatchPageResolved(int epoch);

    // Silent teardown of the active-batch bookkeeping: deactivate, stop
    // the reveal timer, flip the apply guard, and bump the batch epoch so
    // in-flight stragglers are orphaned. Emits nothing and does not touch
    // ambient handles — callers decide what signal (if any) to emit.
    void deactivateBatch();

    // Cancel and forget every batch-tracked pending handle, leaving
    // ambient submissions in m_pending untouched.
    void cancelBatchTrackedHandles();

    // Recompute and emit autoOcrModelMissing() for the visible page.
    void evaluateAutoOcrModel(IDocument *doc, int page);

    Application *m_app;
    // IDocument isn't a QObject, so we can't use QPointer here. The
    // pointer is invalidated only by setDocument(); the MainWindow
    // calls setDocument(nullptr) before destroying a document, so
    // this never dangles in practice.
    IDocument *m_doc = nullptr;
    // shared_ptr (not unique) so worker-thread lambdas can capture
    // a copy by value and keep the engine alive past controller
    // destruction. cancelAll() in the destructor flips every active
    // token, but the worker may still be inside an inference step
    // when the controller frees; the shared_ptr defers OcrEngine
    // destruction until the lambda exits.
    std::shared_ptr<OcrEngine> m_engine;
    std::unordered_map<PendingKey, PendingEntry, PendingKeyHash> m_pending;

    // --- UserAction batch tracking (ADR 0002) ---
    bool m_batchActive = false;
    int m_batchTotal = 0;
    int m_batchCompleted = 0;
    // Monotonic per-batch identity. Bumped when a batch starts; a page's
    // apply lambda captures the value current at submit time and hands it
    // back to onBatchPageResolved(), which ignores any that don't match
    // the live batch. Guards against a superseded batch's late workers
    // inflating the new batch's completion count (ADR 0002 review item 1).
    int m_batchEpoch = 0;
    // Shared with each page's GUI-thread apply step. Flipped by
    // cancelActiveBatch(); the apply step checks it (both run on the GUI
    // thread, so no locking) and discards the interrupted page's blocks
    // rather than persisting a half-recognised page.
    std::shared_ptr<std::atomic<bool>> m_batchCancelled;
    QTimer *m_revealTimer = nullptr;
    // PHILOSOPHY: hand-tuned values stay hand-tuned. Reveal delay is the
    // grace period before an in-flight batch surfaces the progress widget
    // (ADR 0002 §1 "~1s"; B5's floor is "<1s: no looped animation"). The
    // range considered was 600–1200ms: below ~800ms fast batches flicker
    // the widget for work that's already done; above ~1200ms a genuinely
    // slow batch feels unacknowledged. 1000ms sits in the middle and
    // matches the ADR. Bump it only if users report the widget flashing on
    // trivially fast batches (raise) or feeling unresponsive on slow ones
    // (lower); it is settable per-run for tests.
    int m_revealDelayMs = 1000;
    // Set true at the very top of the destructor so cancelAll() knows not
    // to emit ocrBatchAborted() into a half-destroyed MainWindow (its
    // status-bar children may already be gone).
    bool m_destroying = false;
    // Last value emitted through autoOcrModelMissing(). Cached so the
    // signal fires only on a real state change, not on every page/document
    // re-derivation (ADR 0002 review item 9).
    std::optional<bool> m_lastModelMissing;

    // Test seams (see setters above).
    RecognizeFn m_recognizer;
    std::optional<bool> m_modelReadyOverride;
};

} // namespace trailer
