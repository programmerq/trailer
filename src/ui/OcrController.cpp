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
    // Flag the teardown so cancelAll() suppresses ocrBatchAborted(): our
    // signal targets (MainWindow's status-bar widgets) are children of the
    // same parent and may already be destroyed.
    m_destroying = true;
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

    // A fresh explicit batch supersedes any still-active one. Tear the old
    // batch down SILENTLY — cancelActiveBatch() would surface a misleading
    // "cancelled — no changes saved" terminal message that the new reveal
    // would immediately overwrite (ADR 0002 review item 7). ocrBatchAborted
    // drives the widget straight to idle; the new batch's reveal takes over.
    if (m_batchActive) {
        deactivateBatch();
        cancelBatchTrackedHandles();
        emit ocrBatchAborted();
    }

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

    // New batch identity: workers scheduled below capture this epoch and
    // hand it back on completion so a superseded batch's stragglers are
    // ignored (ADR 0002 review item 1).
    ++m_batchEpoch;
    m_batchActive = true;
    m_batchTotal = static_cast<int>(valid.size());
    m_batchCompleted = 0;
    m_batchPages = valid;
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
            onBatchPageResolved(m_batchEpoch);
    }
}

void OcrController::onBatchPageResolved(int epoch) {
    // Ignore stragglers from a batch that has since been superseded or
    // torn down: their epoch no longer matches the live batch, so counting
    // them would inflate the current batch's progress (ADR 0002 review
    // item 1).
    if (!m_batchActive || epoch != m_batchEpoch)
        return;
    ++m_batchCompleted;
    emit ocrBatchProgress(m_batchCompleted, m_batchTotal);
    if (m_batchCompleted >= m_batchTotal) {
        m_batchActive = false;
        m_revealTimer->stop();
        // Honest completion count: total OCR blocks the batch's pages hold
        // in the store now (Item C). Zero → the caller reports "No text
        // found" rather than a false "complete".
        int blockCount = 0;
        if (m_doc) {
            if (auto *store = m_doc->selectableText()) {
                for (int page : m_batchPages)
                    blockCount += static_cast<int>(store->blocks(page).size());
            }
        }
        emit ocrBatchFinished(/*cancelled=*/false, blockCount);
    }
}

void OcrController::deactivateBatch() {
    m_batchActive = false;
    // Orphan any in-flight stragglers scheduled under the old epoch so a
    // late resolve can't touch a subsequent batch's counter.
    ++m_batchEpoch;
    if (m_revealTimer)
        m_revealTimer->stop();
    // Flip the shared apply guard so any GUI-thread apply steps still to
    // come discard their (possibly partial) page rather than persisting a
    // half-recognised page (ADR 0002 §2, per-page granularity).
    if (m_batchCancelled)
        m_batchCancelled->store(true);
}

void OcrController::cancelBatchTrackedHandles() {
    // Cancel and forget only batch-tracked scheduler tasks so not-yet-
    // started batch pages never run and in-flight recognition bails at its
    // next checkpoint. Ambient (visible-page ±1) submissions are left
    // running (ADR 0002 review item 3).
    MlScheduler *sched = m_app ? &m_app->mlScheduler() : nullptr;
    for (auto it = m_pending.begin(); it != m_pending.end();) {
        if (it->second.batchTracked) {
            if (sched)
                sched->cancel(it->second.id);
            it = m_pending.erase(it);
        } else {
            ++it;
        }
    }
}

void OcrController::cancelActiveBatch() {
    if (!m_batchActive)
        return;
    deactivateBatch();
    cancelBatchTrackedHandles();
    emit ocrBatchFinished(/*cancelled=*/true, /*blockCount=*/0);
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
    // Emit only on a real change so a stale hint clears exactly once and
    // steady-state page scrolling doesn't re-fire the signal (ADR 0002
    // review item 9).
    if (m_lastModelMissing.has_value() && *m_lastModelMissing == missing)
        return;
    m_lastModelMissing = missing;
    emit autoOcrModelMissing(missing);
}

void OcrController::refreshModelHint() {
    // Handles doc==null / non-OCR docs: evaluateAutoOcrModel() derives
    // wouldAutoOcr=false for them and emits autoOcrModelMissing(false),
    // hiding any hint left over from the previous document.
    const int page = m_doc ? m_doc->currentPage() : -1;
    evaluateAutoOcrModel(m_doc, page);
}

void OcrController::cancelAll() {
    // Tear down any active batch (doc replace / window close): deactivate,
    // flip the apply guard, and orphan stragglers. Unlike cancelActiveBatch
    // there is no user "cancelled" message; instead we emit ocrBatchAborted
    // so a REVEALED progress widget returns to idle and the scoped cancel
    // action disables (ADR 0002 review item 2). Suppressed during
    // destruction, when the signal targets may already be gone.
    const bool hadBatch = m_batchActive;
    if (m_batchActive)
        deactivateBatch();
    if (m_app) {
        MlScheduler &sched = m_app->mlScheduler();
        for (const auto &kv : m_pending)
            sched.cancel(kv.second.id);
    }
    m_pending.clear();
    if (hadBatch && !m_destroying)
        emit ocrBatchAborted();
}

void OcrController::cancelKey(const PendingKey &key) {
    auto it = m_pending.find(key);
    if (it == m_pending.end())
        return;
    if (m_app) {
        m_app->mlScheduler().cancel(it->second.id);
    }
    m_pending.erase(it);
}

void OcrController::cancelPagesNotMatching(IDocument *doc, const std::vector<int> &keep) {
    if (!m_app)
        return;
    MlScheduler &sched = m_app->mlScheduler();
    for (auto it = m_pending.begin(); it != m_pending.end();) {
        if (it->first.doc != doc) {
            sched.cancel(it->second.id);
            it = m_pending.erase(it);
            continue;
        }
        if (std::find(keep.begin(), keep.end(), it->first.page) == keep.end()) {
            sched.cancel(it->second.id);
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
        m_app->mlScheduler().cancel(existing->second.id);
        m_pending.erase(existing);
    }

    // Render the page on the calling (UI) thread. PDF rendering via
    // QPdfDocument is not safe off the main thread; an image's
    // QImage::copy is cheap. Doing this up front means the worker
    // lambda owns nothing UI-thread-only.
    QImage source = doc->renderPageForOcr(page);
    if (source.isNull())
        return SubmitResult::Skipped;

    // Recognized geometry comes back in renderPageForOcr's source-pixel
    // space. For PDFs that raster is at a fixed DPI while the selection
    // layer's docToView works in PDF points, so blocks must be scaled
    // into point space before they are stored or selection lands ~2× off
    // (image documents render OCR in native pixels and return 1.0). The
    // document owns the conversion; we capture it on the UI thread here.
    const double ocrScale = doc->ocrSourceToDocScale(page);

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
    // Identity of the batch this page belongs to (batch-tracked only).
    // Handed back to onBatchPageResolved() so a straggler from a
    // superseded batch can't advance the current batch's counter.
    const int epoch = batchTracked ? m_batchEpoch : -1;

    auto handle = m_app->mlScheduler().submit(priority, label,
        [engine, recognizer, source, page, storePtr, self, cancelGuard, epoch, ocrScale](CancellationToken &token) {
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
                // Use `self` (the GUI-thread controller) as the invoke
                // context, not storePtr — storePtr may be null here, which
                // would silently drop the queued call and stall the batch
                // count (ADR 0002 review item 8).
                if (self) {
                    QMetaObject::invokeMethod(
                        self, [self, epoch]() { if (self) self->onBatchPageResolved(epoch); },
                        Qt::QueuedConnection);
                }
                return;
            }
            QVector<OcrEngine::TextBlock> blocks =
                recognizer ? recognizer(source, &token) : engine->recognize(source, &token);
            const bool cancelled = token.isCancelled();
            const std::uint64_t contentHash = hashImageContent(source);
            // Scale block geometry from OCR source pixels into the
            // document's coordinate space (points for PDF, pixels for
            // images → 1.0 no-op). Rounds to the polygon's integer grid;
            // block-level selection tolerates sub-pixel rounding.
            std::vector<OcrEngine::TextBlock> out(blocks.constBegin(), blocks.constEnd());
            if (ocrScale != 1.0) {
                for (auto &b : out) {
                    QPolygon scaled;
                    scaled.reserve(b.polygon.size());
                    for (const QPoint &p : b.polygon) {
                        scaled << QPoint(qRound(p.x() * ocrScale), qRound(p.y() * ocrScale));
                    }
                    b.polygon = std::move(scaled);
                }
            }
            // Hand the result back to the UI thread. The store is a
            // QObject parented to the document on the UI thread, so
            // queue the write through its thread context.
            QMetaObject::invokeMethod(
                storePtr,
                [storePtr, self, cancelGuard, page, contentHash, cancelled, epoch,
                 out = std::move(out)]() mutable {
                    // No-partial-write guard (ADR 0002 §2): if the batch
                    // was cancelled (or this page's token flipped mid-
                    // flight), discard the blocks rather than persist a
                    // half-recognised page. Runs on the GUI thread, same
                    // as cancelActiveBatch(), so the flag read is race-
                    // free.
                    const bool discard =
                        cancelled || (cancelGuard && cancelGuard->load());
                    // Silent-discard of an empty layer (Item C): a page that
                    // OCR'd to zero blocks must not leave a "hasResults" entry
                    // claiming a text layer that has no text. Skip the put()
                    // so hasResults(page) stays false and the honest "No text
                    // found" status is truthful. (The models-not-ready path
                    // above already avoids caching empties for a different
                    // reason — retry once the model lands.)
                    if (!discard && storePtr && !out.empty())
                        storePtr->put(page, contentHash, std::move(out));
                    // Count the page against the batch regardless of
                    // whether we stored it — a discarded page still
                    // resolves the slot. (cancelActiveBatch() has already
                    // deactivated the batch, so onBatchPageResolved() is a
                    // no-op in the cancelled case.)
                    if (self)
                        self->onBatchPageResolved(epoch);
                },
                Qt::QueuedConnection);
        });

    m_pending[key] = PendingEntry{handle.id, batchTracked};
    return SubmitResult::Submitted;
}

} // namespace trailer
