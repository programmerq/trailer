#include "OcrController.h"

#include "app/Application.h"
#include "document/IDocument.h"
#include "document/SelectableTextStore.h"
#include "ml/OcrEngine.h"
#include "settings/Settings.h"

#include <QImage>
#include <QMetaObject>
#include <QPointer>
#include <QString>
#include <QTimer>

#include <algorithm>

namespace trailer {

OcrController::OcrController(Application *app, QObject *parent)
    : QObject(parent), m_app(app),
      m_engine(std::make_shared<OcrEngine>(app ? &app->modelRegistry() : nullptr)) {
    // Single-shot reveal timer (ADR 0002 G2). Fires once per batch after
    // the reveal delay; if the batch is still running we ask MainWindow
    // to surface the progress widget. Ops that finish first stop it.
    m_revealTimer = new QTimer(this);
    m_revealTimer->setSingleShot(true);
    connect(m_revealTimer, &QTimer::timeout, this, [this]() {
        if (m_batchActive)
            emit ocrBatchShouldReveal();
    });
}

OcrController::~OcrController() {
    cancelAll();
}

void OcrController::setDocument(IDocument *doc) {
    if (m_doc == doc)
        return;
    cancelAll();
    m_doc = doc;
}

bool OcrController::isLargeDoc() const {
    if (!m_doc)
        return false;
    return m_doc->pageCount() > kLargeDocPageThreshold;
}

void OcrController::onVisiblePageChanged(int page) {
    if (!m_doc || !m_app)
        return;
    auto *doc = m_doc;
    // Re-derive the auto-OCR missing-model state on every page change so
    // the in-context hint is persistent, not fire-once (ADR 0002 §3).
    evaluateAutoOcrModel(doc, page);
    if (!doc->supportsSelectableText())
        return;
    // Settings gate: if the user has switched background recognition
    // off, do nothing here. The Recognize Text… dialog is unaffected
    // (it's an explicit user action).
    if (!m_app->settings().mlRecognizeTextInBackground())
        return;
    if (page < 0 || page >= doc->pageCount())
        return;
    // Documents that already expose a text layer (born-digital PDFs)
    // don't need auto-OCR by default. The user can still force a re-
    // run via the Recognize Text dialog for raster-text-in-watermark
    // edge cases.
    if (doc->hasTextLayer())
        return;
    if (isLargeDoc()) {
        // Cancel anything still pending — large docs only get an
        // explicit user-action submission.
        cancelPagesNotMatching(doc, {});
        return;
    }
    // ADR 0002 §3: on the ambient path, if the language model isn't
    // installed we no longer silently no-op. evaluateAutoOcrModel() above
    // has already surfaced the non-modal in-context hint; skip the
    // submission (there is nothing to run yet) and re-evaluate on the
    // next page/document change once the model may have landed.
    if (!modelReady())
        return;
    // Build the list of pages we want OCR'd: the visible page, plus
    // ±1 neighbours if they exist.
    std::vector<int> wanted;
    wanted.push_back(page);
    if (page - 1 >= 0)
        wanted.push_back(page - 1);
    if (page + 1 < doc->pageCount())
        wanted.push_back(page + 1);
    // Cancel anything pending for pages we no longer care about.
    cancelPagesNotMatching(doc, wanted);
    // Visible page is highest of the auto priorities. Neighbours run
    // as Prefetch. Ambient submissions are never batch-tracked — they
    // must not drive the progress widget nor be user-cancellable.
    submitPage(doc, page, MlPriority::VisiblePage, /*forceRerun=*/false, /*batchTracked=*/false);
    if (page - 1 >= 0)
        submitPage(doc, page - 1, MlPriority::Prefetch, /*forceRerun=*/false,
                   /*batchTracked=*/false);
    if (page + 1 < doc->pageCount())
        submitPage(doc, page + 1, MlPriority::Prefetch, /*forceRerun=*/false,
                   /*batchTracked=*/false);
}

void OcrController::submitUserPages(IDocument *doc, std::vector<int> pages, bool forceRerun) {
    if (!doc || !m_app)
        return;
    if (!doc->supportsSelectableText())
        return;
    // Update m_doc so the engine instance follows. The
    // setDocument() call clears unrelated state; if the doc is
    // already current it's a no-op. (setDocument() cancels any prior
    // batch via cancelAll(), so we begin the new batch afterwards.)
    if (m_doc != doc)
        setDocument(doc);

    // A fresh explicit batch supersedes any still-active one.
    if (m_batchActive)
        cancelActiveBatch();

    // Count the in-range pages up front — that's the batch total the
    // progress widget reports (ADR 0002 G1). Out-of-range indices are
    // dropped and never counted.
    std::vector<int> valid;
    for (int page : pages) {
        if (page >= 0 && page < doc->pageCount())
            valid.push_back(page);
    }
    if (valid.empty())
        return;

    m_batchActive = true;
    m_batchTotal = static_cast<int>(valid.size());
    m_batchCompleted = 0;
    m_batchCancelled = std::make_shared<std::atomic<bool>>(false);
    emit ocrBatchStarted(m_batchTotal);

    // Start the reveal timer: the widget only surfaces if we're still
    // running when it fires. Ops that finish first never reveal it.
    m_revealTimer->start(m_revealDelayMs);

    for (int page : valid) {
        const SubmitResult r =
            submitPage(doc, page, MlPriority::UserAction, forceRerun, /*batchTracked=*/true);
        // A page that was already cached (or that we couldn't render)
        // will never produce a worker completion callback — count it as
        // resolved now so the batch total is always reachable.
        if (r != SubmitResult::Submitted)
            onBatchPageResolved();
    }
}

void OcrController::onBatchPageResolved() {
    if (!m_batchActive)
        return;
    ++m_batchCompleted;
    emit ocrBatchProgress(m_batchCompleted, m_batchTotal);
    if (m_batchCompleted >= m_batchTotal) {
        m_batchActive = false;
        m_revealTimer->stop();
        emit ocrBatchFinished(/*cancelled=*/false);
    }
}

void OcrController::cancelActiveBatch() {
    if (!m_batchActive)
        return;
    m_batchActive = false;
    m_revealTimer->stop();
    // Flip the shared apply guard so any GUI-thread apply steps still to
    // come discard their (possibly partial) page rather than persisting a
    // half-recognised page (ADR 0002 §2, per-page granularity).
    if (m_batchCancelled)
        m_batchCancelled->store(true);
    // Cancel every outstanding scheduler task for this document so
    // not-yet-started pages never run and in-flight recognition bails at
    // its next checkpoint.
    if (m_app) {
        MlScheduler &sched = m_app->mlScheduler();
        for (const auto &kv : m_pending)
            sched.cancel(kv.second);
    }
    m_pending.clear();
    emit ocrBatchFinished(/*cancelled=*/true);
}

bool OcrController::modelReady() const {
    if (m_modelReadyOverride.has_value())
        return *m_modelReadyOverride;
    return m_engine && m_engine->isModelReady();
}

void OcrController::evaluateAutoOcrModel(IDocument *doc, int page) {
    // "Would auto-OCR" mirrors the guard sequence in onVisiblePageChanged
    // for the small-doc ambient path: a supported, no-text-layer document
    // with background recognition enabled and the page in range. Large
    // docs are excluded — their skipped-auto state is covered by the
    // separate large-doc hint chip.
    const bool wouldAutoOcr = doc && m_app && doc->supportsSelectableText() &&
                              m_app->settings().mlRecognizeTextInBackground() &&
                              !doc->hasTextLayer() && page >= 0 && page < doc->pageCount() &&
                              !isLargeDoc();
    const bool missing = wouldAutoOcr && !modelReady();
    emit autoOcrModelMissing(missing);
}

void OcrController::cancelAll() {
    // Quietly tear down any active batch (doc replace / window close):
    // flip the apply guard and deactivate so late GUI-thread applies are
    // discarded and no stale completion fires. No ocrBatchFinished signal
    // here — this path is driven by document teardown, not a user cancel.
    if (m_batchActive) {
        m_batchActive = false;
        if (m_revealTimer)
            m_revealTimer->stop();
        if (m_batchCancelled)
            m_batchCancelled->store(true);
    }
    if (!m_app)
        return;
    MlScheduler &sched = m_app->mlScheduler();
    for (const auto &kv : m_pending) {
        sched.cancel(kv.second);
    }
    m_pending.clear();
}

void OcrController::cancelKey(const PendingKey &key) {
    auto it = m_pending.find(key);
    if (it == m_pending.end())
        return;
    if (m_app) {
        m_app->mlScheduler().cancel(it->second);
    }
    m_pending.erase(it);
}

void OcrController::cancelPagesNotMatching(IDocument *doc, const std::vector<int> &keep) {
    if (!m_app)
        return;
    MlScheduler &sched = m_app->mlScheduler();
    for (auto it = m_pending.begin(); it != m_pending.end();) {
        if (it->first.doc != doc) {
            sched.cancel(it->second);
            it = m_pending.erase(it);
            continue;
        }
        if (std::find(keep.begin(), keep.end(), it->first.page) == keep.end()) {
            sched.cancel(it->second);
            it = m_pending.erase(it);
            continue;
        }
        ++it;
    }
}

OcrController::SubmitResult OcrController::submitPage(IDocument *doc, int page, MlPriority priority,
                                                     bool forceRerun, bool batchTracked) {
    if (!doc || !m_app)
        return SubmitResult::Skipped;
    auto *store = doc->selectableText();
    if (!store)
        return SubmitResult::Skipped;
    PendingKey key{doc, page};

    // If this page is already cached and the caller doesn't request a
    // force-rerun, the work is done — bail before we burn a slot. For a
    // batch this counts as an immediately-resolved page.
    if (!forceRerun && store->hasResults(page)) {
        return SubmitResult::Cached;
    }
    if (forceRerun) {
        store->invalidate(page);
    }

    // Don't double-enqueue. If a task for the same key is already in
    // flight at a non-higher priority, cancel it so the new (likely
    // higher) priority replaces it. If the existing one is already
    // at the right priority, skip.
    auto existing = m_pending.find(key);
    if (existing != m_pending.end()) {
        m_app->mlScheduler().cancel(existing->second);
        m_pending.erase(existing);
    }

    // Render the page on the calling (UI) thread. PDF rendering via
    // QPdfDocument is not safe off the main thread; an image's
    // QImage::copy is cheap. Doing this up front means the worker
    // lambda owns nothing UI-thread-only.
    QImage source = doc->renderPageForOcr(page);
    if (source.isNull())
        return SubmitResult::Skipped;

    // Submit. The lambda captures by value to outlive the controller.
    QString label;
    if (priority == MlPriority::UserAction) {
        label = tr("Recognizing text…");
    } else if (priority == MlPriority::VisiblePage) {
        label = tr("Recognizing text (visible page)…");
    } else {
        label = tr("Recognizing text (prefetch)…");
    }
    // Capture the engine by shared_ptr so the worker lambda extends
    // the engine's lifetime past controller destruction — cancellation
    // is the right exit, but the worker may still be mid-inference
    // when the controller frees.
    std::shared_ptr<OcrEngine> engine = m_engine;
    // Test seam: a supplied recognizer replaces the ONNX pipeline (and
    // bypasses the model-ready gate). Null in production.
    RecognizeFn recognizer = m_recognizer;

    // We can't QPointer-track an IDocument directly because the
    // interface doesn't derive from QObject. Instead capture the
    // store pointer (QObject) and rely on setDocument() calling
    // cancelAll() before forgetting the old document — a truly
    // destroyed document has had its tokens flipped before we get
    // here, so the writer below short-circuits.
    QPointer<SelectableTextStore> storePtr(store);

    // Batch-tracked pages carry the controller + the shared cancel guard
    // into the apply step so completion can be counted and a cancelled
    // page's partial blocks discarded (ADR 0002 §2/§3). Ambient pages
    // carry neither — they never touch the progress widget.
    QPointer<OcrController> self(batchTracked ? this : nullptr);
    std::shared_ptr<std::atomic<bool>> cancelGuard = batchTracked ? m_batchCancelled : nullptr;

    auto handle = m_app->mlScheduler().submit(priority, label,
        [engine, recognizer, source, page, storePtr, self, cancelGuard](CancellationToken &token) {
            if (token.isCancelled())
                return;
            // OcrEngine::recognize cooperates with the same token
            // shape — it polls between detection and per-box rec. If the
            // engine has no model on disk, recognize() returns empty; we
            // deliberately do NOT cache an empty result so the next
            // enqueue can retry once the model lands. A test recognizer
            // bypasses this defensive gate.
            if (!recognizer && !engine->isModelReady()) {
                // Still resolve the batch page so the count can complete.
                if (self || cancelGuard) {
                    QMetaObject::invokeMethod(
                        storePtr, [self]() { if (self) self->onBatchPageResolved(); },
                        Qt::QueuedConnection);
                }
                return;
            }
            QVector<OcrEngine::TextBlock> blocks =
                recognizer ? recognizer(source, &token) : engine->recognize(source, &token);
            const bool cancelled = token.isCancelled();
            const std::uint64_t contentHash = hashImageContent(source);
            // Hand the result back to the UI thread. The store is a
            // QObject parented to the document on the UI thread, so
            // queue the write through its thread context.
            std::vector<OcrEngine::TextBlock> out(blocks.constBegin(), blocks.constEnd());
            QMetaObject::invokeMethod(
                storePtr,
                [storePtr, self, cancelGuard, page, contentHash, cancelled,
                 out = std::move(out)]() mutable {
                    // No-partial-write guard (ADR 0002 §2): if the batch
                    // was cancelled (or this page's token flipped mid-
                    // flight), discard the blocks rather than persist a
                    // half-recognised page. Runs on the GUI thread, same
                    // as cancelActiveBatch(), so the flag read is race-
                    // free.
                    const bool discard =
                        cancelled || (cancelGuard && cancelGuard->load());
                    if (!discard && storePtr)
                        storePtr->put(page, contentHash, std::move(out));
                    // Count the page against the batch regardless of
                    // whether we stored it — a discarded page still
                    // resolves the slot. (cancelActiveBatch() has already
                    // deactivated the batch, so onBatchPageResolved() is a
                    // no-op in the cancelled case.)
                    if (self)
                        self->onBatchPageResolved();
                },
                Qt::QueuedConnection);
        });

    m_pending[key] = handle.id;
    return SubmitResult::Submitted;
}

} // namespace trailer
