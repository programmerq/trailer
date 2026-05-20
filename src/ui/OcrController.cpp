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

#include <algorithm>

namespace trailer {

OcrController::OcrController(Application *app, QObject *parent)
    : QObject(parent), m_app(app),
      m_engine(std::make_shared<OcrEngine>(app ? &app->modelRegistry() : nullptr)) {}

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
    // as Prefetch.
    submitPage(doc, page, MlPriority::VisiblePage, /*forceRerun=*/false);
    if (page - 1 >= 0)
        submitPage(doc, page - 1, MlPriority::Prefetch, /*forceRerun=*/false);
    if (page + 1 < doc->pageCount())
        submitPage(doc, page + 1, MlPriority::Prefetch, /*forceRerun=*/false);
}

void OcrController::submitUserPages(IDocument *doc, std::vector<int> pages, bool forceRerun) {
    if (!doc || !m_app)
        return;
    if (!doc->supportsSelectableText())
        return;
    // Update m_doc so the engine instance follows. The
    // setDocument() call clears unrelated state; if the doc is
    // already current it's a no-op.
    if (m_doc != doc)
        setDocument(doc);
    for (int page : pages) {
        if (page < 0 || page >= doc->pageCount())
            continue;
        submitPage(doc, page, MlPriority::UserAction, forceRerun);
    }
}

void OcrController::cancelAll() {
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

void OcrController::submitPage(IDocument *doc, int page, MlPriority priority, bool forceRerun) {
    if (!doc || !m_app)
        return;
    auto *store = doc->selectableText();
    if (!store)
        return;
    PendingKey key{doc, page};

    // If this page is already cached and the caller doesn't request a
    // force-rerun, the work is done — bail before we burn a slot.
    if (!forceRerun && store->hasResults(page)) {
        return;
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
        return;

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

    // We can't QPointer-track an IDocument directly because the
    // interface doesn't derive from QObject. Instead capture the
    // store pointer (QObject) and rely on setDocument() calling
    // cancelAll() before forgetting the old document — a truly
    // destroyed document has had its tokens flipped before we get
    // here, so the writer below short-circuits.
    QPointer<SelectableTextStore> storePtr(store);

    auto handle = m_app->mlScheduler().submit(priority, label,
        [engine, source, page, storePtr](CancellationToken &token) {
            if (token.isCancelled())
                return;
            // OcrEngine::recognize cooperates with the same token
            // shape — it polls between detection and per-box rec.
            // If the engine has no model on disk, recognize() returns
            // empty; we deliberately do NOT cache an empty result in
            // that case so the next visible-page enqueue can retry
            // once the model lands.
            if (!engine->isModelReady())
                return;
            QVector<OcrEngine::TextBlock> blocks = engine->recognize(source, &token);
            if (token.isCancelled())
                return;
            const std::uint64_t contentHash = hashImageContent(source);
            // Hand the result back to the UI thread. The store is a
            // QObject parented to the document on the UI thread, so
            // queue the write through its thread context.
            std::vector<OcrEngine::TextBlock> out(blocks.constBegin(), blocks.constEnd());
            QMetaObject::invokeMethod(
                storePtr,
                [storePtr, page, contentHash, out = std::move(out)]() mutable {
                    if (!storePtr)
                        return;
                    storePtr->put(page, contentHash, std::move(out));
                },
                Qt::QueuedConnection);
        });

    m_pending[key] = handle.id;

    // OcrEngine::ensureModelsAvailable is async. To keep this PR
    // small, we treat "no model on disk" as a no-op for the auto-
    // submission path: recognize() returns an empty blocks vector
    // when models aren't loaded, and the store stays empty. The
    // RecognizeTextDialog's UserAction path drives model download
    // through ensureOcrModelsReady (the existing helper), so the
    // explicit user click still works end-to-end. Future: route
    // download-progress signals through the scheduler so background
    // OCR can transparently kick off a first-time download too.
}

} // namespace trailer
