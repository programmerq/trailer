#include "PdfAdapter.h"

#include "ui/AnnotationOverlay.h"
#include "ui/FormOverlay.h"
#include "ui/SelectableTextLayer.h"
#include "ui/TwoPageView.h"
#include "util/DocumentSurroundColor.h"
#include "util/TempPath.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QEventLoop>
#include <QFutureWatcher>
#include <QInputDialog>
#include <QLineEdit>
#include <QObject>
#include <QResizeEvent>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfDocument>
#include <QPdfDocumentRenderOptions>
#include <QPdfPageNavigator>
#include <QIdentityProxyModel>
#include <QPdfBookmarkModel>
#include <QPdfLink>
#include <QPdfSearchModel>
#include <QPdfSelection>
#include <QPdfView>
#include <QPrintDialog>
#include <QPrinter>
#include <QScrollBar>
#include <QSizeF>
#include <QThread>
#include <QTimer>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>

namespace trailer {

namespace {
// ~25% per zoom-in / zoom-out tap. Kept in sync with ImageAdapter's
// kZoomStep so image and PDF zooming feel identical; raised from 1.1
// (10%) as the finer step felt too sluggish. See ImageAdapter.cpp for
// the fuller rationale.
constexpr double kZoomStep = 1.25;
constexpr double kZoomMin = 0.10;
constexpr double kZoomMax = 16.0;
// DPI at which pages are rasterised for OCR (renderPageForOcr). A 144-DPI
// raster of a US-letter page is ~1224×1584 — above PP-OCRv3's stride
// threshold, below the memory cost of 300 DPI on long PDFs. Shared with
// ocrSourceToDocScale() so the OCR→point scale always tracks it (range
// tried: 96 too coarse for 8pt scans, 300 blows memory on long docs).
constexpr double kOcrRenderDpi = 144.0;

// Bridge proxy for QPdfBookmarkModel: a vanilla QTreeView fetches
// row text via Qt::DisplayRole, but QPdfBookmarkModel exposes its
// title under the model's `Title` role (numerically Qt::UserRole).
// Without this remap the tree shows a column of empty rows.
class OutlineProxyModel : public QIdentityProxyModel {
  public:
    explicit OutlineProxyModel(QObject *parent = nullptr) : QIdentityProxyModel(parent) {}

    QVariant data(const QModelIndex &proxyIndex, int role) const override {
        if (role == Qt::DisplayRole) {
            return QIdentityProxyModel::data(proxyIndex,
                                             static_cast<int>(QPdfBookmarkModel::Role::Title));
        }
        return QIdentityProxyModel::data(proxyIndex, role);
    }
};

class NavigablePdfView : public QPdfView {
  public:
    explicit NavigablePdfView(QWidget *parent) : QPdfView(parent) {}

  protected:
    void keyPressEvent(QKeyEvent *e) override {
        if (pageMode() == QPdfView::PageMode::SinglePage) {
            const int key = e->key();
            QScrollBar *vbar = verticalScrollBar();
            const bool atBottom = vbar->value() >= vbar->maximum();
            const bool atTop = vbar->value() <= vbar->minimum();
            auto *nav = pageNavigator();
            const int current = nav->currentPage();
            const int last = document() ? document()->pageCount() - 1 : 0;
            // In fit modes the entire page is meant to fit the viewport,
            // so Down/Space should step to the next page outright. The
            // "scroll until you hit the bottom, then step" behaviour is
            // correct for Custom zoom (the user might be reading a
            // zoomed-in page) but wrong for fit modes — with slightly
            // varying page sizes the user otherwise sees a small scroll
            // before the step.
            const bool inFitMode = zoomMode() == QPdfView::ZoomMode::FitInView ||
                                   zoomMode() == QPdfView::ZoomMode::FitToWidth;
            const bool stepDownReady = inFitMode || atBottom;
            const bool stepUpReady = inFitMode || atTop;
            if ((key == Qt::Key_Down || key == Qt::Key_PageDown || key == Qt::Key_Space) &&
                stepDownReady && current < last) {
                // Capture the active fit mode before jumping so we can
                // re-apply it after the page change — passing the
                // current zoomFactor instead would freeze the view at
                // whatever scale the previous page chose, which is
                // wrong when page sizes vary.
                const QPdfView::ZoomMode mode = zoomMode();
                nav->jump(current + 1, QPointF{}, zoomFactor());
                if (inFitMode) {
                    setZoomMode(mode);
                }
                verticalScrollBar()->setValue(verticalScrollBar()->minimum());
                e->accept();
                return;
            }
            if ((key == Qt::Key_Up || key == Qt::Key_PageUp) && stepUpReady && current > 0) {
                const QPdfView::ZoomMode mode = zoomMode();
                nav->jump(current - 1, QPointF{}, zoomFactor());
                if (inFitMode) {
                    setZoomMode(mode);
                }
                verticalScrollBar()->setValue(verticalScrollBar()->maximum());
                e->accept();
                return;
            }
        } else {
            // Continuous (MultiPage) mode: QPdfView hands arrow keys to
            // QAbstractScrollArea, whose Up/Down move by a small
            // line-step — so reaching the next page on a long document
            // by keyboard takes dozens to hundreds of presses. Step by
            // roughly a screenful instead (matching Preview / Acrobat).
            // Space follows Down for a consistent "advance" key.
            // PageDown/PageUp are deliberately NOT handled here: they
            // stay bound to MainWindow's Next/Previous Page shortcuts, so
            // we let them fall through to QPdfView. QScrollBar::setValue
            // clamps to [minimum, maximum], so no manual bounds check.
            const int key = e->key();
            QScrollBar *vbar = verticalScrollBar();
            const int step = vbar->pageStep(); // ~ one viewport height
            if (key == Qt::Key_Down || key == Qt::Key_Space) {
                vbar->setValue(vbar->value() + step);
                e->accept();
                return;
            }
            if (key == Qt::Key_Up) {
                vbar->setValue(vbar->value() - step);
                e->accept();
                return;
            }
        }
        QPdfView::keyPressEvent(e);
    }
};
} // namespace

// Test-only device factory for the background document open (see the header).
// Read on the GUI thread at startDocOpen() and copied by value into the worker
// lambda, so the worker never touches this static concurrently with a setter.
PdfDocument::LoadDeviceFactory PdfDocument::s_loadDeviceFactory;

// Test instrumentation (see PdfAdapter.h). Bumped only where a call actually
// reaches pdfium — i.e. on a page-metrics cache MISS, once per page per load.
std::atomic<qint64> PdfDocument::s_pagePointSizeEngineCalls{0};

const PdfDocument::PageMetrics *PdfDocument::pageMetrics() const {
    if (!m_valid || !m_doc)
        return nullptr;
    if (m_pageMetrics)
        return &*m_pageMetrics;
    const int total = m_doc->pageCount();
    PageMetrics pm;
    pm.sizes.reserve(static_cast<size_t>(std::max(0, total)));
    pm.yOffsets.reserve(static_cast<size_t>(std::max(0, total) + 1));
    double running = 0.0;
    pm.yOffsets.push_back(0.0);
    for (int i = 0; i < total; ++i) {
        // The one and only place pdfium is asked for a page size: once per
        // page per loaded page graph.
        s_pagePointSizeEngineCalls.fetch_add(1, std::memory_order_relaxed);
        const QSizeF sz = m_doc->pagePointSize(i);
        pm.sizes.push_back(sz);
        pm.maxWidth = std::max(pm.maxWidth, sz.width());
        running += sz.height();
        pm.yOffsets.push_back(running);
    }
    m_pageMetrics = std::move(pm);
    return &*m_pageMetrics;
}

QSizeF PdfDocument::pagePoints(int page) const {
    const PageMetrics *pm = pageMetrics();
    if (!pm || page < 0 || static_cast<size_t>(page) >= pm->sizes.size())
        return {};
    return pm->sizes[static_cast<size_t>(page)];
}

void PdfDocument::attachDocSignals() {
    if (!m_doc)
        return;
    // Central invalidation: every QPdfDocument::load()/close() transitions
    // status (Ready -> Loading -> Ready, or -> Null), so hooking statusChanged
    // covers the deferred open, unlock(), recoverFrom(), and every
    // reloadViewerFromEditor() after a page-graph mutation — including any
    // future load site — without each of them having to remember. Connected
    // once, when m_doc is first adopted.
    QObject::connect(m_doc.get(), &QPdfDocument::statusChanged, m_doc.get(),
                     [this](QPdfDocument::Status) { invalidatePageMetrics(); });
}

void PdfDocument::setLoadDeviceFactoryForTesting(LoadDeviceFactory factory) {
    s_loadDeviceFactory = std::move(factory);
}

PdfDocument::PdfDocument(QString path)
    : m_path(std::move(path)), m_editor(std::make_shared<PdfEditor>()) {
    // The residual synchronous open cost — QPdfDocument::load — is the last
    // whole-file read that used to run on the GUI thread at open (backlog
    // 2026-07-15-offthread-pdf-open-placeholder; deferred (b) of DR 0006). It
    // now runs on a worker kicked here. The ctor does NOT block: m_doc stays
    // null and m_valid/m_needsPassword stay unknown until a consumer forces the
    // adopt via ensureDocLoaded() (pageCount / needsPassword / createView), at
    // which point the file reads have already happened off the GUI thread.
    //
    // The qpdf processFile parse (m_editor->load) and the all-pages annotation
    // sweep (readAnnotations) also do NOT run here — they run on the separate
    // startBackgroundLoad() worker (P0 startup-hang fix, #63); the sync
    // edit/save paths fall back to an inline parse via ensureEditorLoaded().
    startDocOpen();
    // Record the on-disk identity we opened so the save-time conflict guard
    // and the ExternalChangeMonitor can distinguish a later external write
    // from our own (ADR 2026-07-19). Independent of the QPdfDocument load.
    captureFileBaseline();
}

void PdfDocument::startDocOpen() {
    if (m_docOpenStarted)
        return;
    m_docOpenStarted = true;
    // Value copies for the worker — it must share NOTHING with this object.
    const QString path = m_path;
    LoadDeviceFactory factory = s_loadDeviceFactory; // usually null
    m_docOpenWatcher = std::make_unique<QFutureWatcher<DocOpenResult>>();
    // Context = qApp (a GUI-thread QObject that outlives the run); the finished
    // slot runs on the GUI thread and adopts the result. If this document is
    // destroyed first, ~PdfDocument resets the watcher, disconnecting the
    // pending signal before the captured `this` can dangle.
    QObject::connect(m_docOpenWatcher.get(), &QFutureWatcherBase::finished,
                     QCoreApplication::instance(), [this]() { onDocOpenFinished(); });
    m_docOpenWatcher->setFuture(QtConcurrent::run([path, factory]() -> DocOpenResult {
        DocOpenResult r;
        // Constructed on the worker thread → worker affinity, so the file
        // reads QPdfDocument::load performs run OFF the GUI thread. This is the
        // whole point of the item: the residual open IO no longer lands on the
        // GUI thread.
        auto *doc = new QPdfDocument();
        QPdfDocument::Error error = QPdfDocument::Error::Unknown;
        if (factory) {
            // Test seam: load from an injected, instrumented QIODevice so the
            // perf harness can observe the read thread. The device overload is
            // event-loop-driven, so pump the worker's own event queue until the
            // load settles. Parent the device to the document so it lives as
            // long as QPdfDocument reads from it and moves with it below.
            QIODevice *dev = factory(path);
            dev->setParent(doc);
            doc->load(dev);
            while (doc->status() == QPdfDocument::Status::Loading)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            error = (doc->status() == QPdfDocument::Status::Ready)
                        ? QPdfDocument::Error::None
                        : QPdfDocument::Error::Unknown;
        } else {
            // Production path: the QString overload is a synchronous bounded
            // progressive read (yields pageCount + a page-0 render), performed
            // here on the worker.
            error = doc->load(path);
        }
        // Hand the loaded document back to the GUI thread. moveToThread from the
        // object's current (worker) thread sets its affinity + that of its
        // children (the internal device); the target thread need not be pumping.
        doc->moveToThread(QCoreApplication::instance()->thread());
        r.doc = doc;
        r.ok = (error == QPdfDocument::Error::None);
        // Password-gated PDFs are a recoverable load failure: PdfAdapter::open
        // prompts and calls unlock(). Everything else stays permanently invalid.
        r.needsPassword = (error == QPdfDocument::Error::IncorrectPassword);
        return r;
    }));
}

void PdfDocument::onDocOpenFinished() { adoptDocOpenResult(); }

void PdfDocument::adoptDocOpenResult() {
    if (!m_docOpenWatcher)
        return; // already drained/adopted (or never started)
    // Detach + delete via the event loop so we never free the watcher from
    // inside its own finished emission, and so a later finished signal (after a
    // sync ensureDocLoaded already drained here) is a no-op via this guard.
    QFutureWatcher<DocOpenResult> *watcher = m_docOpenWatcher.release();
    watcher->deleteLater();
    const DocOpenResult r = watcher->future().result();

    // Adopt the worker's QPdfDocument (already moved to this, the GUI, thread).
    // m_doc is a unique_ptr; take ownership of the raw pointer directly.
    m_doc.reset(r.doc);
    m_valid = r.ok;
    m_needsPassword = r.needsPassword;
    m_docLoaded = true;
    // The worker's load already happened, so no statusChanged will fire for it
    // on this thread: drop any stale metrics explicitly, then hook the signal
    // so every SUBSEQUENT load/close invalidates centrally.
    invalidatePageMetrics();
    attachDocSignals();

    // If createView already returned a "Loading…" placeholder container, swap in
    // the real view now that m_doc exists. In the common interactive path
    // pageCount()/needsPassword() force this adopt before createView runs, so no
    // placeholder was created and this is a no-op.
    if (m_viewContainer && !m_view) {
        QLayout *layout = m_viewContainer->layout();
        if (m_placeholder) {
            layout->removeWidget(m_placeholder);
            m_placeholder->deleteLater();
            m_placeholder = nullptr;
        }
        layout->addWidget(buildRealView(m_viewContainer));
    }
}

void PdfDocument::ensureDocLoaded() {
    if (m_docLoaded)
        return;
    if (!m_docOpenStarted)
        startDocOpen();
    if (m_docOpenWatcher) {
        // waitForFinished() blocks the caller but does NOT spin the event loop,
        // so it cannot re-enter this object or fire the queued finished slot
        // mid-wait; the reads still happened on the worker. Mirrors
        // ensureEditorLoaded()/ensureAnnotationsLoadedSync().
        m_docOpenWatcher->future().waitForFinished();
        adoptDocOpenResult();
    }
}

void PdfDocument::ensureEditorLoaded() const {
    if (m_editorLoaded || !m_valid)
        return;
    // If the background load is in flight, BLOCK for the worker and adopt the
    // editor it parsed rather than parsing a second one on the GUI thread.
    // This is the mid-load edit/save path — brief and rare (in the shipping
    // app the load is kicked at view-attach and has usually long since
    // completed). Safe: the worker is isolated and waitForFinished() does not
    // spin the event loop, so it cannot re-enter or deadlock; the adopt is
    // idempotent (released-watcher guard), so it never double-adopts. Called
    // through const_cast because this const probe drives the lazy work.
    if (m_backgroundLoadStarted && m_backgroundWatcher) {
        m_backgroundWatcher->future().waitForFinished();
        const_cast<PdfDocument *>(this)->adoptBackgroundLoadResult();
        if (m_editorLoaded)
            return;
        // Fell through: the worker produced no valid editor (e.g. a load
        // failure). Drop to the inline parse below as a last resort.
    }
    // Mark loaded up front so a failed/again call doesn't re-run the
    // expensive parse; a locked doc (handled by the !m_valid guard above)
    // stays un-flagged so a later unlock() can still load it.
    m_editorLoaded = true;
    m_editor->load(m_path);
    // Re-apply the unlock the user already performed on the viewer side
    // so editing / annotation round-tripping work on encrypted docs.
    if (m_editor->isEncrypted() && !m_password.isEmpty()) {
        m_editor->unlock(m_password);
        // The GUI editor is now unlocked. If the background load has already
        // captured its own copy of the password, drop the remembered plaintext
        // rather than retain it for the doc lifetime (whichever of these two
        // consumers runs second clears it).
        if (m_backgroundLoadStarted)
            m_password.clear();
    }
}

void PdfDocument::ensureAnnotationHooksWired() {
    if (m_annotationHooksWired || !m_valid)
        return;
    m_annotationHooksWired = true;
    // Wire the store's modified / history mirrors SYNCHRONOUSLY the instant
    // the store is first handed out, so any user annotation edit — including
    // one made while the background sweep is still in flight — is tracked.
    // The bulk background populate is committed via AnnotationStore::addBatch
    // (which pushes no undo frame) under the m_suppressUndoLog guard, so it
    // never sets the dirty flag or logs an undo step. This preserves the
    // pre-async invariant (initial populate is never logged as a user edit)
    // without depending on the populate completing before the first edit.
    QObject::connect(&m_annotations, &AnnotationStore::changed, m_doc.get(), [this]() {
        if (!m_suppressUndoLog)
            m_annotationsModified = true;
    });
    // Pre-edit hook: BEFORE the store records the snapshot for the FIRST user
    // edit, force the deferred off-thread sweep to commit so the loaded file
    // annotations are the baseline that undo reverts to. Without this, an edit
    // made during the async load window snapshots the still-empty pre-load
    // state, and a later undo would wipe every file annotation once addBatch
    // has appended them (BLOCKER B1). ensureAnnotationsLoadedSync() is
    // idempotent (no-ops once loaded) and safe to call here: it is never
    // reached from within the load's own finished slot, and the baseline
    // commit uses addBatch (no pushHistory), so it cannot re-enter this hook.
    m_annotations.setPreEditHook([this]() { ensureAnnotationsLoadedSync(); });
    connectAnnotationHistory();
}

void PdfDocument::startBackgroundLoad() {
    ensureAnnotationHooksWired();
    if (m_annotationsLoaded || m_backgroundLoadStarted || !m_valid)
        return;
    m_backgroundLoadStarted = true;
    // Capture value copies for the worker; it must share NOTHING with this
    // object or the GUI-thread m_editor (qpdf's QPDF is not safe for
    // concurrent access — even reads mutate its lazy object cache).
    const QString path = m_path;
    const QString password = m_password;
    m_backgroundWatcher = std::make_unique<QFutureWatcher<BackgroundLoadResult>>();
    // Context = m_doc.get() (a GUI-thread QObject); the finished slot runs
    // on the GUI thread and adopts the result.
    QObject::connect(m_backgroundWatcher.get(), &QFutureWatcherBase::finished, m_doc.get(),
                     [this]() { onBackgroundLoadFinished(); });
    m_backgroundWatcher->setFuture(QtConcurrent::run([path, password]() -> BackgroundLoadResult {
        // Option B (DR 0006): keep the annotation sweep on a THROWAWAY qpdf
        // instance that is freed before we return, so the fully-resolved
        // ~GB annotation graph never sticks around in steady state — and
        // parse a SEPARATE, parse-only editor that we adopt as m_editor. Both
        // qpdf instances are fully isolated from this object and the GUI
        // editor. The PdfEditor instrumentation counters (parse / page-visit
        // / sweep-thread / parse-thread) tick on these worker instances,
        // which is exactly what the perf test observes.
        BackgroundLoadResult result;
        {
            // (1) Annotation sweep on a throwaway instance. Scoped so its
            // heavy object graph is released before we parse the adopt editor
            // — only one large qpdf is ever resident on the worker at a time.
            PdfEditor sweep;
            sweep.load(path);
            if (sweep.isEncrypted() && !password.isEmpty())
                sweep.unlock(password);
            result.annotations = sweep.readAnnotations();
        }
        // (2) Parse-only editor to adopt as m_editor, plus AcroForm presence.
        // Parse only — deliberately NOT swept for annotations, so the adopted
        // editor stays modest RSS (the sweep's graph was on the throwaway).
        auto editor = std::make_shared<PdfEditor>();
        editor->load(path);
        if (editor->isEncrypted() && !password.isEmpty())
            editor->unlock(password);
        result.hasFormFields = editor->isValid() && editor->hasFormFields();
        result.editor = std::move(editor);
        return result;
    }));
    // Secondary hardening: if the GUI editor is already loaded/unlocked, the
    // worker now holds the only copy of the password it needs, so drop the
    // remembered plaintext (whichever consumer runs second clears it).
    if (m_editorLoaded)
        m_password.clear();
}

void PdfDocument::onBackgroundLoadFinished() {
    adoptBackgroundLoadResult();
}

void PdfDocument::adoptBackgroundLoadResult() {
    if (!m_backgroundWatcher)
        return; // already drained/adopted (or never started)
    // Detach the watcher and delete it via the event loop so we never free it
    // from inside its own finished emission, and so a later finished signal
    // (after a sync-ensure already drained here) is a no-op via this guard.
    QFutureWatcher<BackgroundLoadResult> *watcher = m_backgroundWatcher.release();
    watcher->deleteLater();
    // takeResult() moves the result out (the ~GB annotation vector is never
    // copied). Called exactly once — the released-watcher guard above ensures
    // no second drain reaches here.
    BackgroundLoadResult result = watcher->future().takeResult();

    // Adopt the parse-only editor as m_editor — unless a sync ensureEditorLoaded
    // already parsed one on the GUI thread (mid-load edit). The guard is what
    // makes a mid-load edit correct: we keep the editor that already carries
    // the user's pending edit rather than clobbering it with the worker's.
    if (!m_editorLoaded && result.editor && result.editor->isValid()) {
        m_editor = std::move(result.editor);
        m_editorLoaded = true;
        m_hasFormFieldsCache = result.hasFormFields;
        // The worker held its own password copy; the adopted editor is already
        // unlocked, so drop the remembered plaintext.
        m_password.clear();
    }

    // Commit the annotation set unless a sync ensure (save/export/reduce)
    // already committed it while the worker was finishing.
    if (!m_annotationsLoaded)
        commitAnnotations(std::move(result.annotations));

    // Capabilities (forms) are now known: let MainWindow re-run its
    // forms-toolbar setup. Fires exactly once (this method drains once).
    m_capabilityNotifier.notifyChanged();
}

void PdfDocument::commitAnnotations(std::vector<Annotation> loaded) {
    m_annotationsLoaded = true;
    ensureAnnotationHooksWired();
    // Single batched populate: append the whole loaded set and emit exactly
    // ONE AnnotationStore::changed (coalesced overlay/sidebar/inspector
    // refresh). The suppress guard keeps that changed() from tripping the
    // dirty flag; addBatch pushes no undo frame, so the unified undo log is
    // untouched. If the user already made edits during the load window,
    // those frames/dirty state are preserved (this only appends).
    m_suppressUndoLog = true;
    m_annotations.addBatch(std::move(loaded));
    m_suppressUndoLog = false;
}

void PdfDocument::restoreAnnotationsFromDraft(const QList<Annotation> &annotations, bool dirty) {
    if (!m_valid)
        return;
    ensureAnnotationHooksWired();
    // The draft carried the COMPLETE in-memory annotation set (on-disk +
    // unsaved) captured at ⌥⌘Q. Mark the sweep as already satisfied so a
    // later annotations()/view-attach does NOT run the background sweep and
    // re-append the on-disk subset on top (which would duplicate every
    // saved annotation). Both flags are the "already loaded" guards
    // startBackgroundLoad()/ensureAnnotationsLoadedSync() consult.
    m_annotationsLoaded = true;
    m_backgroundLoadStarted = true;
    // Batch-populate under the suppress guard so the populate itself neither
    // trips the dirty flag nor logs an undo frame (same contract as the
    // deferred load's commit). We then set the modified flag explicitly so
    // the document returns exactly as dirty as it was at quit.
    std::vector<Annotation> items(annotations.begin(), annotations.end());
    m_suppressUndoLog = true;
    m_annotations.addBatch(std::move(items));
    m_suppressUndoLog = false;
    m_annotationsModified = dirty;
}

void PdfDocument::ensureAnnotationsLoadedSync() {
    if (m_annotationsLoaded || !m_valid)
        return;
    ensureAnnotationHooksWired();
    if (m_backgroundLoadStarted && m_backgroundWatcher) {
        // A background load is in flight — block for the worker, then adopt
        // its full result here (commits the annotation set AND adopts the
        // editor). Reached only by the synchronous write paths, which must
        // see the COMPLETE set. In the shipping app the load is kicked at
        // view-attach and has long since committed by the time any save runs,
        // so this wait is a safety net, not a hot path. It is never called
        // from within the watcher's own finished slot, so it cannot deadlock
        // (waitForFinished() does not spin the event loop), and adopt is
        // idempotent.
        m_backgroundWatcher->future().waitForFinished();
        adoptBackgroundLoadResult();
        if (m_annotationsLoaded)
            return;
        // Fell through (no valid commit) — drop to the inline path below.
    }
    // Never kicked (e.g. a document saved without ever being viewed): read
    // synchronously through the GUI-thread editor — the old inline path.
    m_backgroundLoadStarted = true;
    ensureEditorLoaded();
    commitAnnotations(m_editor->readAnnotations());
}

void PdfDocument::connectAnnotationHistory() {
    QObject::connect(&m_annotations, &AnnotationStore::historyPushed, m_doc.get(),
                     [this]() { onAnnotationHistoryPushed(); });
    QObject::connect(&m_annotations, &AnnotationStore::historyEvicted, m_doc.get(),
                     [this]() { onAnnotationHistoryEvicted(); });
}

void PdfDocument::onAnnotationHistoryPushed() {
    if (m_suppressUndoLog)
        return;
    // A new annotation edit (one frame, compound-coalesced):
    // record it in the unified log and invalidate all redo.
    m_undoLog.push_back(UndoSource::Annotation);
    m_redoLog.clear();
    m_pdfRedoStack.clear();
}

void PdfDocument::onAnnotationHistoryEvicted() {
    if (m_suppressUndoLog)
        return;
    // The store dropped its oldest frame to stay within its depth cap.
    // Drop the oldest Annotation entry from the chronological log so
    // the log's annotation count matches what the store can actually
    // undo — without this, undo-all past the cap dispatches to an
    // empty store (silent no-op) and pushes phantom redo entries.
    const auto it = std::find(m_undoLog.begin(), m_undoLog.end(), UndoSource::Annotation);
    if (it != m_undoLog.end()) {
        m_undoLog.erase(it);
    } else {
        qWarning("PdfDocument: AnnotationStore evicted an undo frame but the "
                 "chronological log holds no Annotation entry — log/store desync");
    }
}

bool PdfDocument::needsPassword() const {
    // The password-needed answer comes from the initial load; force it to
    // settle so PdfAdapter::open's prompt loop sees a definitive result.
    const_cast<PdfDocument *>(this)->ensureDocLoaded();
    return m_needsPassword;
}

bool PdfDocument::unlock(const QString &password) {
    ensureDocLoaded(); // m_doc + m_needsPassword must be definitive first
    if (m_valid)
        return true;
    if (!m_needsPassword)
        return false;

    m_doc->setPassword(password);
    const QPdfDocument::Error error = m_doc->load(m_path);
    if (error != QPdfDocument::Error::None) {
        // Wrong password or some other problem. Keep m_needsPassword
        // true only if it's still a password issue so the caller can
        // re-prompt; anything else becomes a hard failure.
        m_needsPassword = (error == QPdfDocument::Error::IncorrectPassword);
        return false;
    }

    m_valid = true;
    m_needsPassword = false;

    // Remember the password so the deferred editor load (and the background
    // worker's isolated editors) can re-unlock the qpdf side. Do NOT eagerly
    // load the editor or sweep annotations here — that synchronous
    // whole-document work is exactly the P0 hang this fix avoids (it now runs
    // lazily via ensureEditorLoaded / on a worker via startBackgroundLoad).
    m_password = password;
    return true;
}

PdfDocument::~PdfDocument() {
    // Detach any in-flight background load so its finished slot cannot fire on
    // a half-destroyed this. Dropping the watcher disconnects the pending
    // finished signal; the worker lambda captures only value copies and its own
    // local editors (the parsed editor is handed back only through the QFuture
    // result, which is discarded with the watcher), so it stays self-contained
    // as it winds down. We deliberately do NOT wait for the worker — closing a
    // tab mid-load must never re-freeze the GUI.
    m_backgroundWatcher.reset();

    // Same for the initial doc-open worker. We must not block teardown on it,
    // but we also must not leak the QPdfDocument it heap-allocated. We delete
    // that result directly rather than routing through adoptDocOpenResult (whose
    // view-building side effect is unwanted during teardown):
    //   * already finished, not yet adopted → delete its QPdfDocument now, here
    //     on the GUI thread (correct affinity — the worker moved it here); or
    //   * still running → schedule that delete on the GUI thread once it
    //     finishes (the continuation captures only the result by value, not
    //     this), so a mid-open tab close neither blocks nor leaks.
    if (m_docOpenWatcher) {
        if (m_docOpenWatcher->future().isFinished()) {
            if (!m_docLoaded)
                delete m_docOpenWatcher->future().result().doc;
        } else {
            m_docOpenWatcher->future().then(QCoreApplication::instance(),
                                            [](DocOpenResult r) { delete r.doc; });
        }
        m_docOpenWatcher.reset();
    }

    // QPdfDocument::close() (reached from ~QPdfDocument) synchronously
    // emits currentPageChanged, which createView() wires to a lambda that
    // calls ingestNativeTextLayer() → m_selectableText.put(). The view and
    // its overlays are owned by the enclosing DocumentView, not by us, so
    // they are still alive at teardown and the connection is still live.
    // Because m_selectableText is declared after m_doc, member-wise
    // destruction would free it *before* m_doc, so that teardown-time
    // signal would touch an already-destroyed store (use-after-free).
    // Flip m_valid first so ingestNativeTextLayer()'s guard short-circuits,
    // then release m_doc here while every member is still alive.
    m_valid = false;
    m_doc.reset();
}

QString PdfDocument::displayName() const {
    // A recovery-untitled doc (markUntitledForRecovery) has no on-disk home;
    // show a clean "Untitled" rather than an empty basename until the user
    // Saves it to a real path (mirrors ImageDocument::displayName).
    if (m_untitled)
        return QObject::tr("Untitled");
    return QFileInfo(m_path).fileName();
}

QString PdfDocument::filePath() const {
    return m_path;
}

bool PdfDocument::isValid() const {
    const_cast<PdfDocument *>(this)->ensureDocLoaded();
    return m_valid;
}

int PdfDocument::pageCount() const {
    // Force the deferred off-thread open to settle so the page count is
    // definitive (the reads already ran on the worker; this only waits).
    const_cast<PdfDocument *>(this)->ensureDocLoaded();
    return m_valid ? m_doc->pageCount() : 0;
}

QWidget *PdfDocument::createView(QWidget *parent) {
    if (m_docLoaded) {
        // Common path: the deferred off-thread open has already settled
        // (pageCount()/needsPassword() forced the adopt before the view is
        // built). Return the real QPdfView directly — no wrapper — so the
        // long-standing "createView returns the QPdfView" contract holds.
        return buildRealView(parent);
    }
    // Async path: the initial QPdfDocument open is genuinely still in flight
    // (e.g. createView called without a prior pageCount()). Return a container
    // showing an honest, self-describing "Loading…" placeholder — not a blank
    // pane, not fake content (G3: it offers no control that cannot act) — and
    // swap in the real view when the worker finishes (adoptDocOpenResult →
    // buildRealView). We deliberately do NOT force ensureDocLoaded() here: that
    // would re-block the very read the worker just took off the GUI thread.
    auto *container = new QWidget(parent);
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    m_viewContainer = container;
    auto *label = new QLabel(QObject::tr("Loading…"), container);
    label->setAlignment(Qt::AlignCenter);
    label->setObjectName(QStringLiteral("pdfLoadingPlaceholder"));
    layout->addWidget(label);
    m_placeholder = label;
    return container;
}

QWidget *PdfDocument::buildRealView(QWidget *parent) {
    if (!m_valid) {
        auto *label = new QLabel(QObject::tr("Could not open PDF:\n%1").arg(m_path), parent);
        label->setAlignment(Qt::AlignCenter);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        return label;
    }

    auto *view = new NavigablePdfView(parent);
    view->setDocument(m_doc.get());
    view->setZoomMode(QPdfView::ZoomMode::Custom);
    view->setZoomFactor(1.0);
    // Fit-to-content on first show. Defer to the event loop so the
    // viewport has its real size after the tab insert + layout pass.
    // Re-checks zero size and bails — a later resize will re-trigger
    // this via the standard QPdfView FitInView path if it stuck. The
    // small-doc upscale guard is the spec: docs that already fit at
    // 100% stay at 100% rather than blowing up to fill the window.
    QTimer::singleShot(0, view, [this, view]() { applyInitialFitZoom(view); });
    applyViewPalette(view);
    m_view = view;
    if (m_searchModel) {
        view->setSearchModel(m_searchModel.get());
        if (m_currentResult >= 0) {
            view->setCurrentSearchResultIndex(m_currentResult);
        }
    }
    applyViewMode();

    auto *overlay = new AnnotationOverlay(view->viewport());
    overlay->setStore(&m_annotations);
    overlay->setPage(view->pageNavigator()->currentPage());
    // Origin of `page` in viewport coordinates. This runs on EVERY
    // document->view coordinate conversion: per polygon point of every
    // selectable-text block, per annotation handle, per mouse move. It used to
    // re-derive the whole-document aggregates (widest page, running Y offset,
    // total content height) by walking every page and calling
    // QPdfDocument::pagePointSize() on each — a mutex-guarded pdfium lookup —
    // which made each conversion O(pageCount) and the first paint of a large
    // document O(points x pages). The aggregates now come from the
    // PageMetrics cache (points, zoom-independent, one pdfium pass per loaded
    // page graph), so this is O(1) arithmetic. Keep it that way: any new
    // whole-document term belongs in PageMetrics, not in a loop here.
    auto pageOriginInView = [this](int page) -> QPointF {
        if (!m_view || !m_doc || page < 0)
            return {};
        const PageMetrics *pm = pageMetrics();
        // Past-the-end page index (a stale annotation whose page the document
        // no longer has). Answered with the same default-QPointF this lambda
        // already returns for the other invalid input, page < 0, above —
        // rather than replicating what the removed loops happened to produce
        // for a nonexistent page, which was QPdfDocument::pagePointSize()'s
        // out-of-range (-1, -1) fed through the sums.
        if (!pm || static_cast<size_t>(page) >= pm->sizes.size())
            return {};
        const double z = m_view->zoomFactor();
        const QMargins m = m_view->documentMargins();
        const int spacing = m_view->pageSpacing();
        const QSize vp = m_view->viewport()->size();

        const int total = static_cast<int>(pm->sizes.size());
        const double maxW = pm->maxWidth * z;
        const QSizeF pageSz = pm->sizes[static_cast<size_t>(page)];
        const double pw = pageSz.width() * z;

        if (m_view->pageMode() == QPdfView::PageMode::SinglePage) {
            const int cur = m_view->pageNavigator()->currentPage();
            if (page != cur)
                return QPointF(-1e9, -1e9);
            const double contentW = maxW + m.left() + m.right();
            const double contentH = pageSz.height() * z + m.top() + m.bottom();
            const double extraX = std::max(0.0, (vp.width() - contentW) / 2.0);
            const double extraY = std::max(0.0, (vp.height() - contentH) / 2.0);
            return QPointF(extraX + m.left() + (maxW - pw) / 2.0 -
                               m_view->horizontalScrollBar()->value(),
                           extraY + m.top() - m_view->verticalScrollBar()->value());
        }

        // Continuous: y is the stacked height of the pages above `page` plus
        // one inter-page gap for each of them; contentH is the whole stack
        // plus (total - 1) gaps. Both are the closed forms of the loops this
        // replaced, so the geometry is bit-for-bit the same modulo
        // floating-point summation order.
        const double y = m.top() + pm->yOffsets[static_cast<size_t>(page)] * z +
                         static_cast<double>(spacing) * page;
        const double contentH = m.top() + m.bottom() + pm->yOffsets.back() * z +
                                (total > 0 ? static_cast<double>(spacing) * (total - 1) : 0.0);
        const double contentW = maxW + m.left() + m.right();
        const double extraX = std::max(0.0, (vp.width() - contentW) / 2.0);
        const double extraY = std::max(0.0, (vp.height() - contentH) / 2.0);
        return QPointF(extraX + m.left() + (maxW - pw) / 2.0 -
                           m_view->horizontalScrollBar()->value(),
                       extraY + y - m_view->verticalScrollBar()->value());
    };
    overlay->setDocumentToView([this, pageOriginInView](QPointF p, int page) {
        if (!m_view)
            return p;
        const double z = m_view->zoomFactor();
        const QPointF origin = pageOriginInView(page);
        return QPointF(origin.x() + p.x() * z, origin.y() + p.y() * z);
    });
    overlay->setViewToDocument([this, pageOriginInView](QPointF p, int page) {
        if (!m_view)
            return p;
        const double z = m_view->zoomFactor();
        if (z <= 0.0)
            return p;
        const QPointF origin = pageOriginInView(page);
        return QPointF((p.x() - origin.x()) / z, (p.y() - origin.y()) / z);
    });
    overlay->setPageAtViewPoint([this, pageOriginInView](QPointF viewPt) -> int {
        if (!m_view || !m_doc)
            return -1;
        const double z = m_view->zoomFactor();
        const int total = m_doc->pageCount();
        for (int i = 0; i < total; ++i) {
            const QPointF origin = pageOriginInView(i);
            const QSizeF pt = pagePoints(i);
            const QRectF rect(origin.x(), origin.y(), pt.width() * z, pt.height() * z);
            if (rect.contains(viewPt))
                return i;
        }
        return m_view->pageNavigator()->currentPage();
    });
    overlay->setSourceSampler([this](QRectF docRect, QSize outPx, int page) -> QImage {
        if (!m_doc || page < 0 || docRect.isEmpty())
            return {};
        const QSizeF pagePts = pagePoints(page);
        if (pagePts.isEmpty())
            return {};
        const double sx = outPx.width() / docRect.width();
        const double sy = outPx.height() / docRect.height();
        const QSize fullPx(std::max(1, static_cast<int>(pagePts.width() * sx)),
                           std::max(1, static_cast<int>(pagePts.height() * sy)));
        QPdfDocumentRenderOptions opts;
        opts.setScaledSize(fullPx);
        opts.setScaledClipRect(QRect(static_cast<int>(docRect.x() * sx),
                                     static_cast<int>(docRect.y() * sy), outPx.width(),
                                     outPx.height()));
        return m_doc->render(page, outPx, opts);
    });
    overlay->setTextSelectionProvider(
        [this](QPointF startDoc, QPointF endDoc, int page) -> std::vector<QRectF> {
            if (!m_doc || page < 0)
                return {};
            const QPdfSelection sel = m_doc->getSelection(page, startDoc, endDoc);
            if (!sel.isValid())
                return {};
            std::vector<QRectF> out;
            for (const QPolygonF &poly : sel.bounds()) {
                out.push_back(poly.boundingRect());
            }
            return out;
        });
    // Copy-able text for the same span, backing Select-tool Ctrl+C / Cmd+C
    // (Tool-precedence rule, AnnotationOverlay.h). getSelection().text() is
    // render-mode-agnostic — it reads the same Tj operators whether the
    // glyphs paint visibly or not, so this covers invisible-text-layer
    // (scanned + OCR'd) PDFs the same way it covers born-digital ones;
    // verified against a synthetic Tr-3 (invisible render mode) fixture in
    // pdfDocumentInvisibleRenderModeTextIsIngestedAndSelectable
    // (tests/test_adapters.cpp).
    overlay->setTextSelectionTextProvider(
        [this](QPointF startDoc, QPointF endDoc, int page) -> QString {
            if (!m_doc || page < 0)
                return {};
            return m_doc->getSelection(page, startDoc, endDoc).text();
        });
    // Point-over-text hover test for the Select-tool I-beam cursor (Tool-
    // precedence rule, case 2 — AnnotationOverlay.h). Reuses the same
    // per-page line-level blocks ingestNativeTextLayer() feeds
    // SelectableTextStore for the None-tool path, rather than
    // SelectableTextLayer::isPointOverText(): that helper tracks a single
    // "current page", which would misreport for a second page partially
    // visible during Continuous-mode scroll. Page-parameterised here
    // (fed by the same per-point pageAt() the overlay already resolves on
    // every mouse move) stays correct for every visible page, not just
    // the "current" one. ingestNativeTextLayer() is idempotent
    // (hasResults() short-circuits), so calling it on every hover is a
    // cheap no-op after the first for that page.
    //
    // Frugality note: this rebuilds the page's view-space polygons on
    // EVERY hover call rather than caching them the way SelectableText
    // Layer::rebuildViewBlocks() does. Deliberate for now — a page's
    // line count is tens, not thousands, so the per-hover cost is a
    // handful of QPolygonF allocations, not a hot loop over the whole
    // document. Revisit with a real (page, zoom)-keyed cache if profiling
    // ever shows this on a dense multi-column page (PHILOSOPHY "frugal by
    // construction": named trade, not an accidental one).
    overlay->setPointOverTextProvider([this, pageOriginInView](QPointF viewPt, int page) -> bool {
        if (!m_view || !m_doc || page < 0)
            return false;
        ingestNativeTextLayer(page);
        const auto &blocks = m_selectableText.blocks(page);
        if (blocks.empty())
            return false;
        const double z = m_view->zoomFactor();
        const QPointF origin = pageOriginInView(page);
        for (const auto &b : blocks) {
            QPolygonF view;
            view.reserve(b.polygon.size());
            for (const QPoint &pt : b.polygon) {
                view << QPointF(origin.x() + pt.x() * z, origin.y() + pt.y() * z);
            }
            if (view.isEmpty())
                continue;
            if (view.boundingRect().contains(viewPt) &&
                view.containsPoint(viewPt, Qt::OddEvenFill)) {
                return true;
            }
        }
        return false;
    });
    overlay->setGeometry(view->viewport()->rect());
    overlay->show();
    m_overlay = overlay;

    QObject::connect(view->pageNavigator(), &QPdfPageNavigator::currentPageChanged, overlay,
                     [overlay](int page) {
                         if (overlay)
                             overlay->setPage(page);
                     });
    // Announce the page change to non-QObject IDocument consumers (Sidebar
    // page-sync, MainWindow auto-OCR / missing-model hint). Routed through the
    // notifier because IDocument is not a QObject; this is the same navigator
    // signal the overlay/text layer already follow, so it fires on keyboard
    // paging, thumbnail jumps, AND continuous-scroll page crossings.
    QObject::connect(view->pageNavigator(), &QPdfPageNavigator::currentPageChanged,
                     &m_pageChangeNotifier, &PageChangeNotifier::notifyPageChanged,
                     Qt::UniqueConnection);
    QObject::connect(view->verticalScrollBar(), &QScrollBar::valueChanged, overlay,
                     QOverload<>::of(&QWidget::update));
    QObject::connect(view->horizontalScrollBar(), &QScrollBar::valueChanged, overlay,
                     QOverload<>::of(&QWidget::update));
    QObject::connect(view, &QPdfView::zoomFactorChanged, overlay,
                     QOverload<>::of(&QWidget::update));
    view->viewport()->installEventFilter(overlay);

    // --- Selectable-text layer (Phase 6F / Workstream F) ---
    // Sits beneath the annotation overlay so user-drawn shapes paint
    // on top of any highlighted selection. Initially empty (no OCR
    // results); MainWindow's auto-OCR pump or the Recognize Text
    // dialog populates the store and the layer wakes up.
    auto *textLayer = new SelectableTextLayer(view->viewport());
    textLayer->setStore(&m_selectableText);
    textLayer->setDocToView([this, pageOriginInView](QPointF p, int page) {
        if (!m_view)
            return p;
        const double z = m_view->zoomFactor();
        const QPointF origin = pageOriginInView(page);
        return QPointF(origin.x() + p.x() * z, origin.y() + p.y() * z);
    });
    textLayer->setPageAtView([this, pageOriginInView](QPointF viewPt) -> int {
        if (!m_view || !m_doc)
            return -1;
        const double z = m_view->zoomFactor();
        const int total = m_doc->pageCount();
        for (int i = 0; i < total; ++i) {
            const QPointF origin = pageOriginInView(i);
            const QSizeF pt = pagePoints(i);
            const QRectF rect(origin.x(), origin.y(), pt.width() * z, pt.height() * z);
            if (rect.contains(viewPt))
                return i;
        }
        return m_view->pageNavigator()->currentPage();
    });
    // Feed the native text layer into the store for the initial page so
    // selection is live on born-digital docs immediately (find already
    // works via QPdfSearchModel; this closes the selection gap). Lazy,
    // per page — see ingestNativeTextLayer().
    ingestNativeTextLayer(view->pageNavigator()->currentPage());
    textLayer->setCurrentPage(view->pageNavigator()->currentPage());
    textLayer->setGeometry(view->viewport()->rect());
    textLayer->lower(); // sit below annotation overlay in the z-order
    textLayer->show();
    m_textLayer = textLayer;

    QObject::connect(view->pageNavigator(), &QPdfPageNavigator::currentPageChanged, textLayer,
                     [this, textLayer](int page) {
                         // Ingest native text for the page the user just
                         // scrolled to before the layer refreshes its
                         // hit-test cache, so selection is ready on arrival.
                         ingestNativeTextLayer(page);
                         if (textLayer)
                             textLayer->setCurrentPage(page);
                     });
    QObject::connect(view->verticalScrollBar(), &QScrollBar::valueChanged, textLayer,
                     QOverload<>::of(&QWidget::update));
    QObject::connect(view->horizontalScrollBar(), &QScrollBar::valueChanged, textLayer,
                     QOverload<>::of(&QWidget::update));
    QObject::connect(view, &QPdfView::zoomFactorChanged, textLayer,
                     QOverload<>::of(&QWidget::update));

    // --- Form overlay (Phase 5) ---
    auto *formOverlay = new FormOverlay(view->viewport());
    formOverlay->setDocumentToView([this, pageOriginInView](QPointF p, int page) {
        if (!m_view)
            return p;
        const double z = m_view->zoomFactor();
        const QPointF origin = pageOriginInView(page);
        return QPointF(origin.x() + p.x() * z, origin.y() + p.y() * z);
    });
    formOverlay->setPageSize([this](int page) -> QSizeF {
        if (!m_doc || page < 0)
            return {};
        return pagePoints(page);
    });
    // The form overlay is populated lazily: at createView time the qpdf
    // editor is usually not yet loaded (deferred), so this seeds fields only
    // if it happens to be live already. The real population happens on demand
    // in setFormFillingActive()/refreshFormView(), which force the editor
    // load — so the initial empty overlay is expected and correct.
    if (m_editor && m_editor->isValid()) {
        formOverlay->setFields(m_editor->readFormFields());
    }
    formOverlay->setGeometry(view->viewport()->rect());
    formOverlay->hide(); // shown by MainWindow when form-filling is toggled on
    m_formOverlay = formOverlay;

    // Relayout form widgets on scroll / zoom / resize.
    QObject::connect(view->verticalScrollBar(), &QScrollBar::valueChanged, formOverlay,
                     &FormOverlay::relayout);
    QObject::connect(view->horizontalScrollBar(), &QScrollBar::valueChanged, formOverlay,
                     &FormOverlay::relayout);
    QObject::connect(view, &QPdfView::zoomFactorChanged, formOverlay, &FormOverlay::relayout);
    // When the user edits a widget, write the value back to the editor.
    QObject::connect(formOverlay, &FormOverlay::fieldValueChanged, view,
                     [this](int id, const QString &value) { setFormFieldValue(id, value); });

    // AUGMENT (decision record 2026-07-21-two-page-layout, D1-A): host the
    // QPdfView surface and a custom TwoPageView in a QStackedWidget. Single and
    // Continuous keep driving the QPdfView (index 0), unchanged; Two-Pages mode
    // shows the TwoPageView (index 1). The zoom factor is SHARED — TwoPageView
    // follows QPdfView::zoomFactorChanged — so the zoom-% readout stays truthful
    // across all three modes (record clause 3).
    auto *stack = new QStackedWidget(parent);
    stack->addWidget(view); // index 0: Single / Continuous

    auto *twoPageView = new TwoPageView(stack);
    twoPageView->setDocument(m_doc.get());
    twoPageView->setZoomFactor(view->zoomFactor());
    stack->addWidget(twoPageView); // index 1: Two-Pages
    // A literal Custom/Actual zoom (a % the user chose) is pushed into
    // twoPageView EXPLICITLY by applyZoomFactor() below, not mirrored via
    // QPdfView::zoomFactorChanged — that signal only fires when the
    // property's NUMERIC VALUE changes, and it does not when switching
    // FROM a fit mode whose last-computed value happens to already equal
    // the new literal factor (e.g. Actual Size right after Fit Page had
    // already settled at exactly 1.0 because the page fully fit the
    // viewport): the mode's MEANING changed (dynamic fit -> a value that
    // must now stay fixed and mode-stable) even though the number didn't,
    // and a property-changed signal can't see that. FitInView/FitToWidth
    // zoom is never mirrored here at all — see applyViewMode()'s TwoPages
    // case, which computes the spread's own fit instead of reusing a
    // number computed for a single page.
    // Re-lay-out the spreads whenever the document's page graph changes: an
    // in-place reload after a page op (rotate / delete / insert / move / crop,
    // revert, recover) changes pageCount / page sizes, and a deferred/async open
    // reaches Ready after createView. Without these the cached spreads would keep
    // drawing the pre-change layout while the user sits in Two-Pages mode.
    QObject::connect(m_doc.get(), &QPdfDocument::pageCountChanged, twoPageView,
                     [twoPageView]() {
                         if (twoPageView)
                             twoPageView->relayout();
                     });
    QObject::connect(m_doc.get(), &QPdfDocument::statusChanged, twoPageView,
                     [twoPageView](QPdfDocument::Status) {
                         if (twoPageView)
                             twoPageView->relayout();
                     });
    // Track the visible spread as the user free-scrolls the TwoPageView so the
    // current-page indicator (sidebar highlight, driven by currentPage()) stays
    // live in Two-Pages mode instead of freezing on the first spread. We only
    // record the value — we must NOT scroll the view back or re-navigate, which
    // would create a feedback loop.
    QObject::connect(twoPageView, &TwoPageView::currentPageChanged, m_doc.get(),
                     [this](int leadingPage) { m_twoPageCurrentPage = leadingPage; });
    m_viewStack = stack;
    m_twoPageView = twoPageView;

    // Apply the current mode now that both surfaces exist (Continuous by
    // default → shows the QPdfView).
    applyViewMode();

    return stack;
}

// static
void PdfDocument::applyViewPalette(QPdfView *view) {
    if (!view)
        return;
    QPalette pal = view->palette();
    // QPdfView paints the canvas surrounding a page that doesn't fill the
    // viewport using QPalette::Dark. Pin it to documentSurroundColor()
    // (util/DocumentSurroundColor.h) — the shared rule TwoPageView also
    // uses (TwoPageView.cpp) — so this can never independently drift from
    // it, and so the reported "grey that's too light in dark mode" (::Dark
    // resolving lighter than ::Base in Trailer's synthesized dark palette)
    // self-heals to match ImageDocument's QPalette::Base surround
    // (ImageAdapter.cpp) exactly, while the light-mode canvas — already
    // correct, and relied on by uat_vwr_079_zoomReadoutMatchesRenderScale's
    // page-vs-canvas contrast measurement — is untouched. See
    // DR 2026-07-31-document-surround-colour-follows-base and that
    // header's comment for why ImageDocument itself is NOT switched to
    // this helper. Recomputed here (not just at construction, from
    // buildRealView) because setPalette() pins the role: Qt's
    // QEvent::PaletteChange cascade on a live theme flip (PR #105) skips
    // any role a widget explicitly set, so a stale pin would survive a
    // theme change unless refreshViewPalette() (below) re-derives it and
    // calls back in here.
    //
    // Read from QApplication::palette(), NOT view->palette() — a widget's
    // OWN resolved palette updates only once Qt delivers the (POSTED, not
    // sent) QEvent::PaletteChange for a QApplication::setPalette /
    // QStyleHints::setColorScheme change, so reading view->palette() here
    // (called synchronously from Application::applyTheme, in the same call
    // stack as the scheme change, before any event-loop turn) can observe a
    // STALE palette. QApplication::palette() itself updates synchronously,
    // so it is the reliable, race-free source — verified empirically (a
    // standalone probe showed QApplication::palette() reflects a new
    // setPalette() immediately while a widget's own .palette() lags until
    // processEvents()).
    pal.setColor(QPalette::Dark, documentSurroundColor(QApplication::palette()));
    // QPdfView paints search matches using the palette's Highlight role.
    // Override to a translucent yellow so matches look like a marker-pen
    // highlighter instead of a system selection. (Qt versions that ignore
    // the role for PDF render fall back gracefully — the change is
    // harmless.) Theme-independent by design (a highlighter colour, not a
    // surround colour), so re-setting it on every refresh is harmless too.
    pal.setColor(QPalette::Highlight, QColor(255, 235, 50, 160));
    pal.setColor(QPalette::HighlightedText, Qt::black);
    view->setPalette(pal);
}

void PdfDocument::refreshViewPalette() {
    // TwoPageView needs no call here — it reads QPalette::Base straight off
    // its viewport on every paint (TwoPageView.cpp), so Qt's own palette-
    // change cascade already keeps it correct with no pinned role to go
    // stale.
    if (m_view)
        applyViewPalette(m_view);
}

void PdfDocument::setAnnotationTool(AnnotationTool tool) {
    if (m_overlay)
        m_overlay->setActiveTool(tool);
}

void PdfDocument::setAnnotationStyle(const AnnotationStyle &style) {
    if (m_overlay)
        m_overlay->setStyle(style);
}

void PdfDocument::setPendingAnnotationText(const QString &text) {
    if (m_overlay)
        m_overlay->setPendingTextPreset(text);
}

void PdfDocument::setPendingSignaturePath(const QString &path) {
    if (m_overlay)
        m_overlay->setPendingSignaturePath(path);
}

void PdfDocument::applyViewMode() {
    if (!m_view) {
        return;
    }
    switch (m_viewMode) {
    case ViewMode::SinglePage:
        m_view->setPageMode(QPdfView::PageMode::SinglePage);
        if (m_viewStack && m_view)
            m_viewStack->setCurrentWidget(m_view);
        break;
    case ViewMode::TwoPages:
        // Two-up (facing) layout has no QPdfView::PageMode, so it renders
        // through the custom TwoPageView (decision record
        // 2026-07-21-two-page-layout, D1-A AUGMENT). Swap the stack to it.
        //
        // Zoom: a literal Custom/Actual zoom (a % the user chose, or "Actual
        // Size") must mean the same physical page size in every mode (record
        // clause 3), so it carries over unchanged. A FIT zoom mode is
        // different: QPdfView's FitInView/FitToWidth factor is computed for a
        // SINGLE page, and blindly applying that same number to a two-page
        // spread overflows the viewport by roughly a whole page — the real
        // dogfooding bug (entering Two-Pages from Single Page + Fit Page
        // spilled page 1 half off-screen and populated a scrollbar the user
        // never asked for). A fit mode instead recomputes its OWN fit for the
        // spread here, exactly like zoomFitPage()/zoomFitWidth() already do
        // when invoked directly while already in this mode — "fit" means "fit
        // what's actually on screen," not "reuse a number computed for a
        // different layout." If the stack isn't built yet (createView not
        // run) there is nothing to switch.
        //
        // Order matters here: QStackedWidget only lays out (resizes) the
        // page that is actually CURRENT — a hidden page keeps whatever
        // stale/default geometry it had (e.g. its never-shown construction-
        // time viewport size), so computing fitPageZoom()/fitWidthZoom()
        // BEFORE the swap would fit against a bogus tiny viewport and
        // produce a wildly wrong zoom (the near-zero-zoom variant of the
        // same spillage-class bug — everything shrinks to fit an 84x14
        // viewport instead of the real one). setCurrentWidget() first makes
        // TwoPageView's real geometry current synchronously, so the fit
        // below measures the viewport the user is actually looking at.
        if (m_viewStack && m_twoPageView)
            m_viewStack->setCurrentWidget(m_twoPageView);
        if (m_twoPageView && m_view) {
            switch (zoomMode()) {
            case ZoomMode::FitInView:
                m_twoPageView->setZoomFactor(m_twoPageView->fitPageZoom());
                break;
            case ZoomMode::FitToWidth:
                m_twoPageView->setZoomFactor(m_twoPageView->fitWidthZoom());
                break;
            case ZoomMode::Custom:
            case ZoomMode::Actual:
                m_twoPageView->setZoomFactor(m_view->zoomFactor());
                break;
            }
        }
        break;
    case ViewMode::Continuous:
        m_view->setPageMode(QPdfView::PageMode::MultiPage);
        if (m_viewStack && m_view)
            m_viewStack->setCurrentWidget(m_view);
        break;
    }
}

void PdfDocument::setViewMode(ViewMode mode) {
    if (mode == m_viewMode) {
        return; // nothing moved; re-navigating would be a redundant jump.
    }
    // Real dogfooding bug: switching modes (Cmd-1/2/3) used to leave the
    // NEW surface sitting at its own default scroll position (page 1 for a
    // freshly-shown QPdfView / TwoPageView) while the document model — and
    // the sidebar reading currentPage() off it — kept reporting the page the
    // user was actually on. The model was right and the view was wrong,
    // which is its own lying-UI bug independent of the jump itself. Capture
    // the page BEFORE swapping surfaces and explicitly re-navigate the new
    // one to it, through the same goToPage() every other page-change uses,
    // so the model and the view can never disagree after a mode switch.
    const int fromPage = currentPage();
    m_viewMode = mode;
    applyViewMode();
    goToPage(fromPage);
}

void PdfDocument::applyZoomFactor(double factor) {
    if (!m_view) {
        return;
    }
    const double clamped = std::clamp(factor, kZoomMin, kZoomMax);
    m_view->setZoomMode(QPdfView::ZoomMode::Custom);
    m_view->setZoomFactor(clamped);
    QScrollBar *hbar = m_view->horizontalScrollBar();
    hbar->setValue((hbar->minimum() + hbar->maximum()) / 2);
    // Push the literal zoom into the two-page surface explicitly (see the
    // rationale at the zoomFactorChanged-removal note in createView()) —
    // this is the canonical Custom/Actual zoom path shared by zoomIn/
    // zoomOut/zoomActual/applyZoomState, so it is the single place that
    // needs to know about TwoPageView. Only while Two-Pages is the active
    // mode: applyViewMode() already does its own fresh Custom/Actual push
    // on every entry into that mode, so syncing here while some OTHER mode
    // is active would just be a wasted relayout() on a hidden widget for
    // every zoom action in Single/Continuous mode.
    if (m_twoPageView && m_viewMode == ViewMode::TwoPages)
        m_twoPageView->setZoomFactor(clamped);
}

void PdfDocument::zoomIn() {
    if (!m_view)
        return;
    applyZoomFactor(m_view->zoomFactor() * kZoomStep);
}

void PdfDocument::zoomOut() {
    if (!m_view)
        return;
    applyZoomFactor(m_view->zoomFactor() / kZoomStep);
}

void PdfDocument::zoomActual() {
    applyZoomFactor(1.0);
}

void PdfDocument::zoomFitWidth() {
    if (!m_view)
        return;
    // Two-Pages mode renders through the custom TwoPageView, whose fit must
    // account for a full facing spread (page1 + gutter + page2) — QPdfView's
    // per-page FitToWidth would overflow the viewport by a whole page. Route to
    // the spread-aware fit and apply it through the shared zoom path so the
    // zoom-% readout stays truthful; QPdfView keeps its own fit for the other
    // two modes (record clause 3, G3: the visible surface actually fits).
    if (m_viewMode == ViewMode::TwoPages && m_twoPageView) {
        applyZoomFactor(m_twoPageView->fitWidthZoom());
        return;
    }
    m_view->setZoomMode(QPdfView::ZoomMode::FitToWidth);
}

void PdfDocument::zoomFitPage() {
    if (!m_view)
        return;
    if (m_viewMode == ViewMode::TwoPages && m_twoPageView) {
        applyZoomFactor(m_twoPageView->fitPageZoom());
        return;
    }
    m_view->setZoomMode(QPdfView::ZoomMode::FitInView);
}

QSize PdfDocument::contentSizeHint() const {
    // Used for the initial window sizing; force the deferred open to settle so
    // the page-0 size is available (the reads already ran on the worker).
    const_cast<PdfDocument *>(this)->ensureDocLoaded();
    if (!m_valid || !m_doc || m_doc->pageCount() <= 0)
        return {};
    const QSizeF pts = pagePoints(0);
    if (pts.isEmpty())
        return {};
    // QPdfView maps 1 PDF point to 1 logical pixel at zoom 1.0, so
    // the natural display size in CSS pixels is just the point size.
    return QSize(static_cast<int>(std::ceil(pts.width())),
                 static_cast<int>(std::ceil(pts.height())));
}

void PdfDocument::applyInitialFitZoom(QPdfView *view) {
    if (!view || !m_doc || m_doc->pageCount() <= 0)
        return;
    if (m_initialZoomApplied)
        return;
    const QSizeF pagePts = pagePoints(0);
    if (pagePts.isEmpty())
        return;
    const QSize vp = view->viewport()->size();
    if (vp.width() <= 0 || vp.height() <= 0) {
        // Layout hasn't settled — retry on the next tick. The retry
        // chain stops as soon as the viewport reports a real size or
        // the view is destroyed.
        QTimer::singleShot(0, view, [this, view]() { applyInitialFitZoom(view); });
        return;
    }
    m_initialZoomApplied = true;
    const QMargins m = view->documentMargins();
    const double availW = std::max(1, vp.width() - m.left() - m.right());
    const double availH = std::max(1, vp.height() - m.top() - m.bottom());
    const double scaleW = availW / pagePts.width();
    const double scaleH = availH / pagePts.height();
    const double fit = std::min(scaleW, scaleH);
    if (fit >= 1.0) {
        // Doc already fits at 100% — leave it at actual size rather
        // than upscaling. zoomFactor was already set to 1.0 above.
        return;
    }
    // Use FitInView so a later window resize re-fits without the user
    // having to hit ⌘0 again. zoomFitPage() picks the same mode.
    view->setZoomMode(QPdfView::ZoomMode::FitInView);
}

ZoomMode PdfDocument::zoomMode() const {
    if (!m_view)
        return ZoomMode::Custom;
    switch (m_view->zoomMode()) {
    case QPdfView::ZoomMode::FitInView:
        return ZoomMode::FitInView;
    case QPdfView::ZoomMode::FitToWidth:
        return ZoomMode::FitToWidth;
    case QPdfView::ZoomMode::Custom:
        break;
    }
    // QPdfView treats "actual size" as a custom zoom of 1.0. We report
    // it separately so the persistence layer can preserve the user's
    // intent (⌘0 vs an exact 100% custom factor) — they're identical
    // mechanically but the user thinks of them differently.
    if (qFuzzyCompare(m_view->zoomFactor(), 1.0))
        return ZoomMode::Actual;
    return ZoomMode::Custom;
}

double PdfDocument::zoomFactor() const {
    // In Two-Pages mode the visible surface is the custom TwoPageView, which
    // — for a FIT zoom mode — runs an independently-computed spread fit that
    // can diverge from QPdfView's single-page fit (see applyViewMode()).
    // Read the readout from whichever surface is actually on screen so the
    // number can never lie about the painted scale (mirrors currentPage()'s
    // existing per-mode branch just below).
    if (m_viewMode == ViewMode::TwoPages && m_twoPageView)
        return m_twoPageView->zoomFactor();
    return m_view ? m_view->zoomFactor() : 1.0;
}

void PdfDocument::applyZoomState(ZoomMode mode, double factor) {
    if (!m_view)
        return;
    switch (mode) {
    case ZoomMode::FitInView:
        m_view->setZoomMode(QPdfView::ZoomMode::FitInView);
        return;
    case ZoomMode::FitToWidth:
        m_view->setZoomMode(QPdfView::ZoomMode::FitToWidth);
        return;
    case ZoomMode::Actual:
        applyZoomFactor(1.0);
        return;
    case ZoomMode::Custom:
        if (factor > 0.0)
            applyZoomFactor(factor);
        return;
    }
}

int PdfDocument::scrollY() const {
    if (!m_view)
        return 0;
    return m_view->verticalScrollBar()->value();
}

void PdfDocument::applyScrollY(int y) {
    if (!m_view)
        return;
    auto *bar = m_view->verticalScrollBar();
    if (!bar)
        return;
    // Clamp to the bar's range — a saved scroll position from a doc
    // that has since been edited (pages removed, zoom changed) may
    // exceed the new maximum. Falling back to the closest valid
    // value is friendlier than landing at 0.
    const int clamped = std::clamp(y, bar->minimum(), bar->maximum());
    bar->setValue(clamped);
}

QImage PdfDocument::renderPageForOcr(int pageIndex) const {
    if (!m_valid || !m_doc || pageIndex < 0 || pageIndex >= m_doc->pageCount()) {
        return {};
    }
    // PP-OCRv3 caps the long side at 960 px internally, but we want a
    // little extra so smaller scans render legible glyphs. See
    // kOcrRenderDpi for the DPI rationale.
    const QSizeF pagePts = pagePoints(pageIndex);
    if (pagePts.isEmpty())
        return {};
    const int w = std::max(1, static_cast<int>(pagePts.width() / 72.0 * kOcrRenderDpi));
    const int h = std::max(1, static_cast<int>(pagePts.height() / 72.0 * kOcrRenderDpi));
    QImage rendered = m_doc->render(pageIndex, QSize(w, h));
    if (rendered.isNull())
        return rendered;
    // Background-flatten so the OCR detector sees a white-paper
    // colour rather than transparent pixels (which the detector reads
    // as "outside the document").
    QImage canvas(rendered.size(), QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::white);
    QPainter painter(&canvas);
    painter.drawImage(0, 0, rendered);
    painter.end();
    return canvas;
}

QSizeF PdfDocument::pageSizeHint(int pageIndex) const {
    // Cheap page-geometry probe used by the sidebar to size each
    // thumbnail row by aspect. Mirrors the validity/bounds guards in
    // renderThumbnail but does no rendering — QPdfDocument::pagePointSize
    // returns the /CropBox (falling back to /MediaBox) dimensions.
    if (!m_valid || !m_doc || pageIndex < 0 || pageIndex >= m_doc->pageCount()) {
        return {};
    }
    return pagePoints(pageIndex);
}

double PdfDocument::ocrSourceToDocScale(int pageIndex) const {
    if (!m_valid || !m_doc || pageIndex < 0 || pageIndex >= m_doc->pageCount())
        return 1.0;
    const QSizeF pagePts = pagePoints(pageIndex);
    if (pagePts.isEmpty())
        return 1.0;
    // renderPageForOcr rasterises at kOcrRenderDpi; recognized block
    // geometry therefore comes back in that pixel space, but docToView
    // (and native ingestion) work in PDF points. Derive the points-per-
    // pixel scale from the identical width computation so integer
    // truncation is accounted for exactly.
    const int renderedW = std::max(1, static_cast<int>(pagePts.width() / 72.0 * kOcrRenderDpi));
    return pagePts.width() / static_cast<double>(renderedW);
}

bool PdfDocument::pageHasText(int page) const {
    if (!m_valid || !m_doc || page < 0 || page >= m_doc->pageCount())
        return false;
    // Real per-page probe (not the coarse hasTextLayer() stub): a born-
    // digital page yields a non-empty extraction, an image-only scan
    // yields an empty string. m_doc is non-const through the unique_ptr
    // even in a const method, mirroring renderPageForOcr().
    return !m_doc->getAllText(page).text().trimmed().isEmpty();
}

void PdfDocument::ingestNativeTextLayer(int page) {
    if (!m_valid || !m_doc || page < 0 || page >= m_doc->pageCount())
        return;
    // Never clobber real OCR output (or a prior native ingest) — the
    // store is the shared sink for both pipelines, and hasResults() is
    // the "already populated" guard.
    if (m_selectableText.hasResults(page))
        return;
    const QPdfSelection all = m_doc->getAllText(page);
    if (!all.isValid())
        return;
    const QString pageText = all.text();
    if (pageText.trimmed().isEmpty())
        return;

    // Build line-level TextBlocks. Qt PDF exposes glyph-level bounds via
    // getAllText().bounds(), but the SelectableTextLayer selects whole
    // blocks and joins them with '\n', so line granularity matches its
    // UX (a drag snaps to lines, exactly like the OCR path's regions).
    // We walk the page string, split it into lines on CR/LF, and re-
    // query getSelectionAtIndex() for each line's clean text + point-
    // space bounding rectangle. Point space is what the layer's
    // docToView() callback expects (it multiplies by the zoom factor;
    // see the setDocToView() wiring in createView()).
    std::vector<OcrEngine::TextBlock> blocks;
    const int n = static_cast<int>(pageText.size());
    int i = 0;
    while (i < n) {
        const QChar c = pageText.at(i);
        if (c == QLatin1Char('\r') || c == QLatin1Char('\n')) {
            ++i;
            continue;
        }
        const int start = i;
        while (i < n && pageText.at(i) != QLatin1Char('\r') &&
               pageText.at(i) != QLatin1Char('\n')) {
            ++i;
        }
        const int len = i - start;
        if (len <= 0)
            continue;
        const QPdfSelection line = m_doc->getSelectionAtIndex(page, start, len);
        if (!line.isValid())
            continue;
        const QString lineText = line.text();
        if (lineText.trimmed().isEmpty())
            continue;
        const QRectF r = line.boundingRectangle();
        if (r.isEmpty())
            continue;
        OcrEngine::TextBlock b;
        b.text = lineText;
        b.polygon = QPolygon({QPoint(qRound(r.left()), qRound(r.top())),
                              QPoint(qRound(r.right()), qRound(r.top())),
                              QPoint(qRound(r.right()), qRound(r.bottom())),
                              QPoint(qRound(r.left()), qRound(r.bottom()))});
        // Native layer is exact, not a probabilistic OCR read.
        b.confidence = 1.0f;
        blocks.push_back(std::move(b));
    }
    if (blocks.empty())
        return;
    // A stable non-zero content hash keyed off the page text, so the
    // store's 0 == "no entry" sentinel is never produced. The auto-OCR
    // scheduler skips text-layer docs anyway, so the exact value only
    // needs to be deterministic and non-zero.
    const std::uint64_t hash =
        static_cast<std::uint64_t>(qHash(pageText)) | 0x1ULL;
    m_selectableText.put(page, hash, std::move(blocks));
}

QImage PdfDocument::renderThumbnail(int pageIndex, QSize targetSize) {
    if (!m_valid || pageIndex < 0 || pageIndex >= m_doc->pageCount()) {
        return {};
    }
    const QSizeF pageSize = pagePoints(pageIndex);
    if (pageSize.isEmpty() || !targetSize.isValid() || targetSize.isEmpty()) {
        return {};
    }
    const double aspect = pageSize.width() / pageSize.height();
    int w = targetSize.width();
    int h = static_cast<int>(w / aspect);
    if (h > targetSize.height()) {
        h = targetSize.height();
        w = static_cast<int>(h * aspect);
    }
    QImage rendered = m_doc->render(pageIndex, QSize(w, h));
    if (rendered.isNull())
        return rendered;
    // Many PDFs draw their content (text, vector ink) with no
    // explicit page background, leaving the rendered QImage with
    // transparent regions where paper would be. In dark mode the
    // sidebar's dock background shows through and the page reads
    // as floating black text on dark grey — unrecognisable. Force
    // an opaque white backdrop. PDFs that DO paint a background
    // colour just paint over it, no harm done.
    QImage canvas(rendered.size(), QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::white);
    QPainter painter(&canvas);
    painter.drawImage(0, 0, rendered);
    painter.end();
    return canvas;
}

int PdfDocument::currentPage() const {
    if (!m_view)
        return 0;
    // In Two-Pages mode the QPdfView is hidden and its navigator only moves on
    // explicit goToPage(); free-scrolling the custom TwoPageView doesn't touch
    // it. Return the leading page the TwoPageView reports for the top-most
    // visible spread so the current-page indicator tracks the scroll position.
    if (m_viewMode == ViewMode::TwoPages && m_twoPageView)
        return m_twoPageCurrentPage;
    return m_view->pageNavigator()->currentPage();
}

void PdfDocument::goToPage(int pageIndex) {
    if (!m_view || pageIndex < 0 || pageIndex >= pageCount()) {
        return;
    }
    m_view->pageNavigator()->jump(pageIndex, QPointF{}, m_view->zoomFactor());
    // In Two-Pages mode the QPdfView is hidden, so also scroll the visible
    // TwoPageView to the spread holding this page — otherwise Previous/Next Page
    // and thumbnail-click navigation would silently move only the hidden view
    // (an inert control, G3). The scroll fires TwoPageView::currentPageChanged,
    // which updates m_twoPageCurrentPage — the value currentPage() reports in
    // this mode — so the current-page indicator stays in sync. (The
    // pageNavigator jump above keeps the hidden QPdfView consistent for when the
    // user switches back to Single/Continuous.)
    if (m_viewMode == ViewMode::TwoPages && m_twoPageView) {
        m_twoPageView->scrollToPage(pageIndex);
    }
}

int PdfDocument::nextPageIndex() const {
    // In Two-Pages mode Next Page must advance by a whole SPREAD relative to the
    // currently-visible one, not by one page: currentPage()+1 lands on the right
    // page of the same spread, which scrollToPage() maps straight back to that
    // spread, so per-page stepping would stick. Ask the layout for the next
    // spread's leading page instead. Single/Continuous keep single-page steps.
    if (m_viewMode == ViewMode::TwoPages && m_twoPageView)
        return m_twoPageView->leadingPageOfNextSpread(currentPage());
    return currentPage() + 1;
}

int PdfDocument::previousPageIndex() const {
    if (m_viewMode == ViewMode::TwoPages && m_twoPageView)
        return m_twoPageView->leadingPageOfPrevSpread(currentPage());
    return currentPage() - 1;
}

void PdfDocument::setSearchQuery(const QString &query) {
    if (!m_valid) {
        return;
    }
    if (!m_searchModel) {
        m_searchModel = std::make_unique<QPdfSearchModel>();
        m_searchModel->setDocument(m_doc.get());
        // setSearchString dispatches the actual search to a worker
        // thread — rowCount() is still 0 when this function returns.
        // Without this hook, the synchronous
        // setCurrentSearchResultIndex call below runs before any
        // matches exist, so the view never highlights the first hit
        // even when the model eventually populates. That's the
        // "Find found nothing" bug on OCR'd PDFs.
        //
        // Using the search model as the context so the lambda is
        // torn down automatically with it. PdfDocument itself isn't a
        // QObject so we can't bind to a member slot directly.
        QObject::connect(m_searchModel.get(), &QAbstractItemModel::rowsInserted,
                         m_searchModel.get(),
                         [this](const QModelIndex &, int, int) { onSearchResultsPopulated(); });
    }
    m_searchModel->setSearchString(query);
    if (query.isEmpty()) {
        m_currentResult = -1;
        m_seedPending = false;
        m_provisionalSeedIndex = -1;
    } else {
        // Capture where the reader is now; the position-aware seed is the
        // first match at/after this page (ADR 0006 — Option B). The actual
        // index is computed once the model has rows, either synchronously
        // just below (cached results) or from onSearchResultsPopulated as
        // the async search streams matches in.
        m_seedFromPage = currentPage();
        m_seedPending = true;
        m_provisionalSeedIndex = -1;
        m_currentResult = 0; // placeholder until the seed is computed
    }
    if (m_view) {
        m_view->setSearchModel(m_searchModel.get());
        // Clear the view's current index so a late rowsInserted from
        // the *previous* query can't be mistaken for in-flight user
        // navigation by the onSearchResultsPopulated guard.
        m_view->setCurrentSearchResultIndex(-1);
        // Best-effort synchronous seed for the cached-results case. The
        // async rowsInserted signal handles the common "search still
        // running" path. Either way the seed is position-aware, not a
        // stale index 0.
        if (m_seedPending && m_searchModel->rowCount({}) > 0) {
            const int seed = firstResultIndexAtOrAfter(m_seedFromPage);
            m_currentResult = seed;
            m_provisionalSeedIndex = seed;
            applySearchResultIndex(seed);
        }
    }
    // Push the (possibly empty) match list to the overlay so an
    // empty / cleared query removes stale yellow highlights from a
    // previous search.
    refreshSearchHighlights();
}

void PdfDocument::onSearchResultsPopulated() {
    if (!m_view || !m_searchModel)
        return;
    if (m_searchModel->rowCount({}) <= 0)
        return;
    // No seed to settle (query cleared, or the seed already froze on user
    // navigation): just keep the overlay highlights in sync with the
    // growing model.
    if (!m_seedPending) {
        refreshSearchHighlights();
        return;
    }
    // Distinguish a provisional seed WE pushed from genuine user
    // navigation. The raw ">= 0" test can't tell them apart: this handler
    // fires on every rowsInserted, and computing the seed against a
    // *partially* populated model can wrap to index 0 before the
    // current-page rows arrive — the old guard then froze that provisional
    // index for the rest of the populate (ADR 0006 R1). If the view's
    // index differs from our last provisional push, the user drove
    // findNext/findPrevious mid-populate: freeze and respect it.
    const int viewIdx = m_view->currentSearchResultIndex();
    if (viewIdx >= 0 && viewIdx != m_provisionalSeedIndex) {
        m_seedPending = false;
        refreshSearchHighlights();
        return;
    }
    // Re-seed against the now-larger model. As rows stream in page order,
    // firstResultIndexAtOrAfter converges on the true at/after-page match
    // once the current-page (or later) rows land, overwriting any earlier
    // provisional wrap-to-0.
    const int seed = firstResultIndexAtOrAfter(m_seedFromPage);
    m_currentResult = seed;
    m_provisionalSeedIndex = seed;
    applySearchResultIndex(seed);
    refreshSearchHighlights();
}

int PdfDocument::firstResultIndexAtOrAfter(int page) const {
    if (!m_searchModel)
        return 0;
    const int total = m_searchModel->rowCount({});
    if (total <= 0)
        return 0;
    for (int i = 0; i < total; ++i) {
        // Results come back ordered by page (same Page role
        // pagesWithSearchMatches walks), so the first row with page >=
        // the target is both the first at/after page and the first
        // reading-order match on that page.
        const QModelIndex idx = m_searchModel->index(i, 0);
        const int p = idx.data(static_cast<int>(QPdfSearchModel::Role::Page)).toInt();
        if (p >= page)
            return i;
    }
    // Nothing at/after the page — wrap to the first match in the document.
    return 0;
}

QAbstractItemModel *PdfDocument::outlineModel() {
    if (!m_valid || !m_doc)
        return nullptr;
    if (!m_bookmarkModel) {
        m_bookmarkModel = std::make_unique<QPdfBookmarkModel>();
        m_bookmarkModel->setDocument(m_doc.get());
    }
    if (!m_outlineProxy) {
        m_outlineProxy = std::make_unique<OutlineProxyModel>();
        m_outlineProxy->setSourceModel(m_bookmarkModel.get());
    }
    return m_outlineProxy.get();
}

bool PdfDocument::hasOutline() const {
    if (!m_valid || !m_doc)
        return false;
    // Construct the model lazily on the pre-check too so MainWindow
    // can drive the Sidebar picker's enabled-state without forcing a
    // separate tree walk.
    if (!m_bookmarkModel) {
        m_bookmarkModel = std::make_unique<QPdfBookmarkModel>();
        m_bookmarkModel->setDocument(m_doc.get());
    }
    return m_bookmarkModel->rowCount({}) > 0;
}

void PdfDocument::goToOutlineEntry(const QModelIndex &index) {
    if (!m_valid || !m_view || !index.isValid())
        return;
    if (!m_bookmarkModel)
        return;
    // The index may be either a proxy index (from the Sidebar's
    // QTreeView attached to outlineModel() ) or a source index. Read
    // the page role through the index itself — QIdentityProxyModel
    // passes non-DisplayRole queries straight through, so either
    // works.
    const QVariant pageVar = index.data(static_cast<int>(QPdfBookmarkModel::Role::Page));
    bool ok = false;
    const int page = pageVar.toInt(&ok);
    if (!ok || page < 0 || page >= pageCount())
        return;
    goToPage(page);
}

void PdfDocument::refreshSearchHighlights() {
    if (!m_overlay)
        return;
    std::vector<AnnotationOverlay::SearchHighlight> highlights;
    if (m_searchModel) {
        const int n = m_searchModel->rowCount({});
        for (int i = 0; i < n; ++i) {
            const QPdfLink link = m_searchModel->resultAtIndex(i);
            if (!link.isValid())
                continue;
            const int page = link.page();
            const bool isCurrent = (i == m_currentResult);
            for (const QRectF &r : link.rectangles()) {
                highlights.push_back({page, r, isCurrent});
            }
        }
    }
    m_overlay->setSearchHighlights(std::move(highlights));
}

void PdfDocument::applySearchResultIndex(int index) {
    if (!m_view)
        return;
    m_view->setCurrentSearchResultIndex(index);
    if (!m_searchModel || index < 0 || index >= m_searchModel->rowCount({}))
        return;
    const QPdfLink link = m_searchModel->resultAtIndex(index);
    if (link.isValid())
        m_view->pageNavigator()->jump(link);
}

void PdfDocument::findNext() {
    if (!m_view || !m_searchModel)
        return;
    const int count = m_searchModel->rowCount({});
    if (count <= 0)
        return;
    m_currentResult = (m_currentResult + 1) % count;
    applySearchResultIndex(m_currentResult);
    refreshSearchHighlights();
}

void PdfDocument::findPrevious() {
    if (!m_view || !m_searchModel)
        return;
    const int count = m_searchModel->rowCount({});
    if (count <= 0)
        return;
    m_currentResult = (m_currentResult - 1 + count) % count;
    applySearchResultIndex(m_currentResult);
    refreshSearchHighlights();
}

void PdfDocument::clearSearch() {
    if (m_searchModel) {
        m_searchModel->setSearchString(QString());
    }
    m_currentResult = -1;
    // Reset the async-seed guard for symmetry with the empty-query branch of
    // search() (PdfAdapter.cpp ~797). clearSearch() bypasses search(), so
    // without this a stale m_seedPending / m_provisionalSeedIndex from the
    // previous query would linger until the next non-empty query overwrote
    // them.
    m_seedPending = false;
    m_provisionalSeedIndex = -1;
    if (m_view) {
        m_view->setCurrentSearchResultIndex(-1);
    }
    refreshSearchHighlights();
}

int PdfDocument::searchMatchCount() const {
    if (!m_searchModel)
        return 0;
    return m_searchModel->rowCount({});
}

int PdfDocument::currentSearchMatchIndex() const {
    if (!m_searchModel)
        return -1;
    if (m_currentResult < 0)
        return -1;
    if (m_currentResult >= m_searchModel->rowCount({}))
        return -1;
    // 1-based for display; the convention every "X of Y" UI uses.
    return m_currentResult + 1;
}

std::vector<int> PdfDocument::pagesWithSearchMatches() const {
    std::vector<int> out;
    if (!m_searchModel)
        return out;
    const int total = m_searchModel->rowCount({});
    for (int i = 0; i < total; ++i) {
        // QPdfSearchModel exposes the page index via the
        // PageIndexRole (Qt::UserRole + 1). We dedupe inline by
        // remembering the last page seen — search results come
        // back ordered by page so we don't need a set.
        const QModelIndex idx = m_searchModel->index(i, 0);
        const int page = idx.data(static_cast<int>(QPdfSearchModel::Role::Page)).toInt();
        if (out.empty() || out.back() != page) {
            out.push_back(page);
        }
    }
    return out;
}

void PdfDocument::print(QWidget *dialogParent) {
    if (!m_valid) {
        return;
    }

    QPrinter printer(QPrinter::HighResolution);
    printer.setDocName(displayName());
    printer.setFromTo(1, m_doc->pageCount());
    QPrintDialog dialog(&printer, dialogParent);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const int first = printer.fromPage() > 0 ? printer.fromPage() - 1 : 0;
    const int last = printer.toPage() > 0 ? printer.toPage() - 1 : m_doc->pageCount() - 1;
    if (first > last) {
        return;
    }

    QPainter painter;
    if (!painter.begin(&printer)) {
        return;
    }

    const QRect target = printer.pageLayout().paintRectPixels(printer.resolution());
    for (int page = first; page <= last; ++page) {
        const QSizeF pagePts = pagePoints(page);
        if (pagePts.isEmpty())
            continue;

        const double aspect = pagePts.width() / pagePts.height();
        int w = target.width();
        int h = static_cast<int>(w / aspect);
        if (h > target.height()) {
            h = target.height();
            w = static_cast<int>(h * aspect);
        }
        const QImage img = m_doc->render(page, QSize(w, h));
        const int x = target.x() + (target.width() - w) / 2;
        const int y = target.y() + (target.height() - h) / 2;
        painter.drawImage(QPoint(x, y), img);

        if (page < last) {
            printer.newPage();
        }
    }
    painter.end();
}

bool PdfDocument::reloadViewerFromEditor() {
    // Any edit-driven reload derefs m_doc; make sure the deferred open has been
    // adopted first (idempotent — a no-op once loaded).
    ensureDocLoaded();
    if (!m_editor || !m_editor->isValid()) {
        return false;
    }
    // A page-graph mutation may add or remove AcroForm fields, so the
    // cached form-detection result is now stale.
    m_hasFormFieldsCache.reset();
    auto preview =
        std::make_unique<ScopedTempFile>(QStringLiteral("trailer-preview-XXXXXX.pdf"));
    if (!preview->isValid()) {
        return false;
    }
    const QString previewPath = preview->path();
    if (!m_editor->save(previewPath)) {
        return false;
    }

    const int savedPage = currentPage();
    const double savedZoom = m_view ? m_view->zoomFactor() : 1.0;

    m_doc->close();
    const QPdfDocument::Error error = m_doc->load(previewPath);
    if (error != QPdfDocument::Error::None) {
        return false;
    }

    m_previewFile = std::move(preview);

    if (m_searchModel) {
        m_searchModel->setDocument(m_doc.get());
    }
    if (m_view) {
        m_view->setDocument(m_doc.get());
        if (savedPage >= 0 && savedPage < pageCount()) {
            m_view->pageNavigator()->jump(savedPage, QPointF{}, savedZoom);
        }
    }
    // Any reload is the result of a page-level mutation (rotate,
    // delete, move, crop, insert). The OCR cache is keyed on the raw
    // page raster — clear it wholesale rather than try to be clever
    // about which pages survived. The auto-OCR pump will re-enqueue
    // work for the visible page after the reload settles.
    m_selectableText.clear();
    return true;
}

void PdfDocument::rotatePage(int pageIndex, int degreesClockwise) {
    ensureEditorLoaded();
    if (!m_valid || !m_editor || !m_editor->isValid()) {
        return;
    }
    if (pageIndex < 0 || pageIndex >= pageCount()) {
        return;
    }
    auto cmd = std::make_unique<RotatePageCommand>(pageIndex, degreesClockwise);
    if (!cmd->apply(*m_editor))
        return;
    m_pdfUndoStack.push_back(std::move(cmd));
    recordPdfCommandApplied();
    if (reloadViewerFromEditor()) {
        m_dirty = true;
    }
}

bool PdfDocument::canUndo() const { return !m_undoLog.empty(); }

bool PdfDocument::canRedo() const { return !m_redoLog.empty(); }

bool PdfDocument::undo() {
    // Pop the single chronological log so the most recent op is undone
    // first, regardless of which stack it came from.
    if (m_undoLog.empty())
        return false;
    const UndoSource src = m_undoLog.back();
    if (src == UndoSource::Annotation) {
        if (!m_annotations.canUndo()) {
            // Should be unreachable: historyEvicted keeps the log's
            // annotation count in lockstep with the store. Degrade to
            // a warning + no-op rather than claim an undo happened;
            // drop the orphaned entry so older (valid) log entries
            // stay reachable.
            qWarning("PdfDocument::undo: log expects an annotation frame but the "
                     "AnnotationStore history is empty; dropping the orphaned entry");
            m_undoLog.pop_back();
            return false;
        }
        m_undoLog.pop_back();
        m_annotations.undo();
    } else {
        if (m_pdfUndoStack.empty()) {
            // Runtime guard, not an assert: a log/stack desync here
            // would otherwise call .back() on an empty vector — UB in
            // release builds where Q_ASSERT compiles out. Warn, drop
            // the orphaned entry, and refuse.
            qWarning("PdfDocument::undo: log expects a PdfCommand but the command "
                     "stack is empty; dropping the orphaned entry");
            m_undoLog.pop_back();
            return false;
        }
        m_undoLog.pop_back();
        auto cmd = std::move(m_pdfUndoStack.back());
        m_pdfUndoStack.pop_back();
        cmd->revert(*m_editor);
        m_pdfRedoStack.push_back(std::move(cmd));
        reloadViewerFromEditor();
        m_dirty = true;
    }
    m_redoLog.push_back(src);
    return true;
}

bool PdfDocument::redo() {
    // Inverse of undo(): pop the redo log and re-apply on the stack the
    // entry came from, in the order the ops were originally undone.
    if (m_redoLog.empty())
        return false;
    const UndoSource src = m_redoLog.back();
    if (src == UndoSource::Annotation) {
        if (!m_annotations.canRedo()) {
            qWarning("PdfDocument::redo: log expects an annotation frame but the "
                     "AnnotationStore redo history is empty; dropping the orphaned entry");
            m_redoLog.pop_back();
            return false;
        }
        m_redoLog.pop_back();
        m_annotations.redo();
    } else {
        if (m_pdfRedoStack.empty()) {
            qWarning("PdfDocument::redo: log expects a PdfCommand but the command "
                     "stack is empty; dropping the orphaned entry");
            m_redoLog.pop_back();
            return false;
        }
        m_redoLog.pop_back();
        auto cmd = std::move(m_pdfRedoStack.back());
        m_pdfRedoStack.pop_back();
        cmd->apply(*m_editor);
        m_pdfUndoStack.push_back(std::move(cmd));
        reloadViewerFromEditor();
        m_dirty = true;
    }
    m_undoLog.push_back(src);
    return true;
}

void PdfDocument::recordPdfCommandApplied() {
    // A new qpdf-level edit invalidates all redo — the two redo stacks
    // and the unified redo log — then appends to the chronological undo
    // log.
    m_pdfRedoStack.clear();
    m_annotations.clearRedo();
    m_redoLog.clear();
    m_undoLog.push_back(UndoSource::PdfCommand);
}

void PdfDocument::deletePages(const std::vector<int> &pageIndices) {
    ensureEditorLoaded();
    if (!m_valid || !m_editor || !m_editor->isValid() || pageIndices.empty()) {
        return;
    }
    const int before = m_editor->pageCount();
    if (static_cast<int>(pageIndices.size()) >= before) {
        return; // refuse to delete every page
    }
    auto cmd = std::make_unique<DeletePagesCommand>(pageIndices);
    if (!cmd->apply(*m_editor))
        return;
    m_pdfUndoStack.push_back(std::move(cmd));
    recordPdfCommandApplied();
    if (reloadViewerFromEditor()) {
        m_dirty = true;
    }
}

bool PdfDocument::extractPages(const std::vector<int> &pageIndices, const QString &destPath) const {
    ensureEditorLoaded();
    if (!m_valid || !m_editor || !m_editor->isValid())
        return false;
    return m_editor->extractPages(pageIndices, destPath);
}

bool PdfDocument::cropPage(int pageIndex, double leftPts, double topPts, double rightPts,
                           double bottomPts) {
    ensureEditorLoaded();
    if (!m_valid || !m_editor || !m_editor->isValid())
        return false;
    auto cmd = std::make_unique<CropPageCommand>(std::vector<int>{pageIndex}, leftPts, topPts,
                                                 rightPts, bottomPts);
    if (!cmd->apply(*m_editor))
        return false;
    m_pdfUndoStack.push_back(std::move(cmd));
    recordPdfCommandApplied();
    if (reloadViewerFromEditor()) {
        m_dirty = true;
        return true;
    }
    return false;
}

bool PdfDocument::cropPages(const std::vector<int> &pageIndices, double leftPts, double topPts,
                            double rightPts, double bottomPts) {
    ensureEditorLoaded();
    if (!m_valid || !m_editor || !m_editor->isValid() || pageIndices.empty()) {
        return false;
    }
    // A single user gesture cropping N pages produces ONE command so
    // Ctrl-Z reverts the whole batch atomically (CropPageCommand
    // captures each affected page's original /CropBox internally).
    auto cmd = std::make_unique<CropPageCommand>(pageIndices, leftPts, topPts, rightPts, bottomPts);
    if (!cmd->apply(*m_editor))
        return false;
    m_pdfUndoStack.push_back(std::move(cmd));
    recordPdfCommandApplied();
    if (reloadViewerFromEditor()) {
        m_dirty = true;
        return true;
    }
    return false;
}

bool PdfDocument::insertPagesFrom(const QString &sourcePath, int insertAtIndex) {
    ensureEditorLoaded();
    if (!m_valid || !m_editor || !m_editor->isValid()) {
        return false;
    }
    auto cmd = std::make_unique<InsertPagesCommand>(sourcePath, insertAtIndex);
    if (!cmd->apply(*m_editor)) {
        // apply() returns false on any failure (bad source, no pages,
        // qpdf throw). Don't push a failed command — mirrors the
        // rotatePage pattern.
        return false;
    }
    m_pdfUndoStack.push_back(std::move(cmd));
    recordPdfCommandApplied();
    if (reloadViewerFromEditor()) {
        m_dirty = true;
        return true;
    }
    return false;
}

void PdfDocument::movePage(int from, int to) {
    ensureEditorLoaded();
    if (!m_valid || !m_editor || !m_editor->isValid()) {
        return;
    }
    const int total = m_editor->pageCount();
    if (from < 0 || from >= total || to < 0 || to >= total || from == to) {
        return;
    }
    auto cmd = std::make_unique<MovePageCommand>(from, to);
    if (!cmd->apply(*m_editor))
        return;
    m_pdfUndoStack.push_back(std::move(cmd));
    recordPdfCommandApplied();
    if (reloadViewerFromEditor()) {
        m_dirty = true;
    }
}

bool PdfDocument::save(const QString &newPath) {
    // Synchronous entry point (tests + save-model where blocking is fine): adopt
    // the deferred open on THIS (GUI/calling) thread so m_valid is definitive
    // before the qpdf begin-phase guards on it. The async two-phase save
    // (saveBeginQpdfPhase on a worker) deliberately does NOT call ensureDocLoaded
    // — it must not touch m_doc/widgets off the GUI thread — and relies on the
    // invariant that a document is always adopted (viewed) before an async save.
    ensureDocLoaded();
    auto ctx = saveBeginQpdfPhase(newPath);
    if (!ctx)
        return false;
    return saveCommitOnUi(*ctx);
}

bool PdfDocument::hasPendingDestructiveAnnotation() {
    // See the header comment: force the complete annotation set to be present
    // (the deferred sweep may still be in flight) before scanning, so this
    // safety gate can never MISS a disk-resident redaction/signature and let it
    // be silently burned in by a keep snapshot.
    ensureAnnotationsLoadedSync();
    for (const Annotation &a : m_annotations.annotations()) {
        if (a.type == AnnotationType::Redaction || a.type == AnnotationType::Signature)
            return true;
    }
    return false;
}

bool PdfDocument::writeRecoverySnapshot(const QString &sidecarPath) {
    // Auto-save calls this instead of save(): it must NEVER write the backing
    // file and must NOT clear the dirty flag. We snapshot the CURRENT editor
    // graph (structural page edits included) to a throwaway temp, reload it
    // into a scratch editor, and bake the in-memory annotations into THAT —
    // never our live m_editor, since writeAnnotations() appends and
    // applyRedactions()/flattenSignatures() are destructive. The live document
    // (m_editor / m_path / m_doc / dirty flags) is left completely untouched;
    // only the sidecar is written.
    if (m_forceRecoverySnapshotFailureForTesting)
        return false; // test seam: exercise the ⌥⌘Q snapshot-preflight fallback
    ensureDocLoaded(); // definitive m_valid before the guards below
    ensureEditorLoaded();
    ensureAnnotationsLoadedSync();
    if (!m_valid || !m_editor || !m_editor->isValid() || sidecarPath.isEmpty())
        return false;

    const QString scratch = makeUniqueTempPath(QStringLiteral("trailer-recovery-XXXXXX.pdf"));
    if (scratch.isEmpty())
        return false;
    struct ScratchGuard {
        QString path;
        ~ScratchGuard() {
            if (!path.isEmpty())
                QFile::remove(path);
        }
    } guard{scratch};

    // m_editor->save() serializes the live editor graph via QPDFWriter::write()
    // — a read of the QPDF, not a structural mutation — so the live editor is
    // left intact and usable; all destructive annotation baking happens on the
    // separate `snap` editor below. (This relies on save() not reordering /
    // rewriting the live graph in place; if that ever changes, snapshot from a
    // reloaded copy instead.)
    if (!m_editor->save(scratch))
        return false;
    PdfEditor snap;
    if (!snap.load(scratch) || !snap.isValid())
        return false;

    const std::vector<Annotation> anns = m_annotations.annotations();
    // Strip any managed markup annotations the scratch carried over from the
    // current graph (forms/links/popups are preserved), so the in-memory
    // store is the single source of truth and nothing is duplicated.
    if (!snap.clearManagedAnnotations())
        return false;
    if (!snap.applyRedactions(anns))
        return false;
    if (!snap.flattenSignatures(anns))
        return false;
    if (!snap.writeAnnotations(anns))
        return false;
    return snap.save(sidecarPath);
}

bool PdfDocument::recoverFrom(const QString &sidecarPath) {
    if (sidecarPath.isEmpty() || !QFileInfo::exists(sidecarPath))
        return false;
    ensureDocLoaded(); // definitive m_valid + non-null m_doc before use
    ensureEditorLoaded();
    if (!m_valid || !m_doc)
        return false;

    // If the deferred annotation sweep is already in flight, drain and discard
    // it: we are about to replace the annotation store wholesale from the
    // recovery snapshot, and the sweep reads the BACKING file — letting it
    // commit afterwards would append the backing file's annotations on top of
    // the recovered set. (At the normal open-time call site the sweep has not
    // been kicked yet; this is a defensive drain.)
    if (m_backgroundLoadStarted && m_backgroundWatcher) {
        m_backgroundWatcher->future().waitForFinished();
        m_backgroundWatcher.reset();
    }

    // Keep pointing Save at the user's real file; load content from the
    // sidecar. The backing file is not touched here.
    const QString backing = m_path;

    // Load the live editor/viewer from a PRIVATE copy of the sidecar, never the
    // sidecar path itself. The sidecar path is deterministic for this backing
    // file, so it is exactly what the next auto-save tick overwrites — if the
    // live m_editor/m_doc held that path open, the tick would truncate the file
    // out from under them (blocker: corrupt recovered doc / unsaveable work).
    // The private copy is owned for the document's lifetime.
    auto recoveryCopy =
        std::make_unique<ScopedTempFile>(QStringLiteral("trailer-recovered-XXXXXX.pdf"));
    if (!recoveryCopy->isValid())
        return false;
    // Manual truncating byte-copy rather than QFile::copy: makeUniqueTempPath
    // reserves-then-removes the destination, but on Windows a QTemporaryFile's
    // handle can outlive close() so the reserved file may still exist when we
    // get here — QFile::copy refuses to overwrite an existing destination and
    // fails. Opening the destination WriteOnly|Truncate overwrites regardless,
    // on every platform, and touches no non-Qt writer / rename / delete-of-open
    // path.
    {
        QFile src(sidecarPath);
        QFile dst(recoveryCopy->path());
        if (!src.open(QIODevice::ReadOnly) ||
            !dst.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        constexpr qint64 kChunk = 1 << 20; // 1 MiB
        while (!src.atEnd()) {
            const QByteArray chunk = src.read(kChunk);
            if (chunk.isEmpty())
                break;
            if (dst.write(chunk) != chunk.size())
                return false;
        }
        dst.close();
        src.close();
    }
    const QString content = recoveryCopy->path();

    m_doc->close();
    if (m_doc->load(content) != QPdfDocument::Error::None) {
        m_doc->load(backing); // fall back to the untouched real file
        return false;
    }

    m_editor = std::make_shared<PdfEditor>();
    if (!m_editor->load(content) || !m_editor->isValid()) {
        m_doc->close();
        m_doc->load(backing);
        m_editor = std::make_shared<PdfEditor>();
        m_editor->load(backing);
        return false;
    }
    m_recoveryBackingFile = std::move(recoveryCopy);
    m_editorLoaded = true;
    m_path = backing;
    m_hasFormFieldsCache.reset();

    // Repopulate annotations from the recovered content so they are live,
    // editable objects again. Mirror the save-reload churn guard so this is
    // not logged as a user edit.
    m_suppressUndoLog = true;
    m_annotations.clear();
    for (Annotation &a : m_editor->readAnnotations()) {
        m_annotations.add(std::move(a));
    }
    m_annotations.clearHistory();
    m_suppressUndoLog = false;

    // The store is now the single source of truth for markup annotations.
    // Strip the managed markup annotations from the editor graph (forms/links
    // survive) so a later explicit Save — which appends the store's set to the
    // editor's /Annots — does not DUPLICATE the recovered annotations. Without
    // this, recover + Save writes each recovered stroke twice.
    m_editor->clearManagedAnnotations();

    // The store is fully populated from the recovery snapshot: mark the
    // deferred annotation load DONE so the view-attach background sweep
    // (startBackgroundLoad, kicked from annotations()) short-circuits instead
    // of reading the backing file and appending its annotations over the
    // recovered set. This is the blocker guard — without it a previously-
    // annotated backing PDF would have its saved annotations duplicated on
    // recovery.
    m_annotationsLoaded = true;
    m_backgroundLoadStarted = true;

    if (m_searchModel) {
        m_searchModel->setDocument(m_doc.get());
    }
    if (m_view) {
        m_view->setDocument(m_doc.get());
    }

    // The recovered edits are unsaved: mark dirty so they are visible and the
    // close/Save flow treats them as pending. The source stays byte-identical
    // until the user explicitly Saves.
    m_annotationsModified = true;
    m_dirty = true;
    return true;
}

std::optional<PdfDocument::SaveContext> PdfDocument::saveBeginQpdfPhase(const QString &newPath) {
    // Saving flushes pending annotations into the qpdf graph, so both the
    // editor and the (possibly deferred) annotation store must be live.
    ensureEditorLoaded();
    ensureAnnotationsLoadedSync();
    if (!m_valid || !m_editor || !m_editor->isValid()) {
        return std::nullopt;
    }
    const QString targetPath = newPath.isEmpty() ? m_path : newPath;
    if (targetPath.isEmpty()) {
        return std::nullopt;
    }

    // Save-time conflict guard (ADR 2026-07-19): refuse to overwrite the
    // baselined original if it changed under us and this isn't a deliberate
    // "Keep mine" clobber. Runs before any bytes are written so no on-disk
    // data is touched; the caller surfaces the conflict banner.
    // Capture the force state BEFORE the guard consumes it so the commit
    // phase can honour a deliberate clobber during its own re-stat (F1).
    const bool forced = m_forceSaveOverExternalChange;
    if (saveWouldClobberExternalChange(targetPath)) {
        return std::nullopt;
    }

    // Order matters: apply redactions first so their rasterised page
    // image replaces the old content stream before anything else runs.
    // Then flatten signatures so they survive as page content when the
    // file is re-read (readAnnotations does not reconstruct image
    // stamps). Finally, write every other annotation as /Annot.
    if (!m_editor->applyRedactions(m_annotations.annotations())) {
        return std::nullopt;
    }
    if (!m_editor->flattenSignatures(m_annotations.annotations())) {
        return std::nullopt;
    }
    if (!m_editor->writeAnnotations(m_annotations.annotations())) {
        return std::nullopt;
    }

    SaveContext ctx;
    ctx.targetPath = targetPath;
    ctx.forced = forced;
    ctx.sameFile = !m_path.isEmpty() && QFileInfo(targetPath).canonicalFilePath() ==
                                            QFileInfo(m_path).canonicalFilePath();

    if (ctx.sameFile) {
        // Stage to a temp file so a partial write doesn't clobber the
        // original. The UI-phase rename is atomic. makeUniqueTempPath
        // (not QTemporaryFile) so qpdf can open the path for writing
        // on Windows — see util/TempPath.h for the rationale.
        ctx.writePath = makeUniqueTempPath(QStringLiteral("trailer-save-XXXXXX.pdf"));
        if (ctx.writePath.isEmpty()) {
            return std::nullopt;
        }
        if (!m_editor->save(ctx.writePath)) {
            QFile::remove(ctx.writePath);
            return std::nullopt;
        }
    } else {
        ctx.writePath = targetPath;
        if (!m_editor->save(ctx.writePath)) {
            return std::nullopt;
        }
    }
    return ctx;
}

bool PdfDocument::saveCommitOnUi(const SaveContext &ctx) {
    // The commit half reloads m_doc from the written bytes; make sure the
    // initial open has been adopted first (idempotent once loaded).
    ensureDocLoaded();
    if (ctx.sameFile) {
        // Commit-time re-stat guard (F1). The begin phase's guard ran on a
        // worker thread and the destructive remove+rename below can land
        // several seconds later; an external writer may have replaced the
        // file in that window. Re-stat against the baseline right before we
        // touch the on-disk file. If an uncaused external change is now
        // present and this isn't a deliberate "Keep mine" clobber, abort
        // WITHOUT removing/renaming — the original on-disk bytes survive and
        // the staged temp is dropped. The caller re-checks externalChangeState
        // and routes this to the conflict banner (F6). The baseline is left
        // untouched so that re-check still sees the conflict.
        if (!ctx.forced) {
            const ExternalChangeState st = externalChangeState();
            if (st == ExternalChangeState::CleanExternalChange ||
                st == ExternalChangeState::DirtyConflict) {
                QFile::remove(ctx.writePath);
                return false;
            }
        }
        // Tear down our QPdfDocument's open handle so we can rename
        // over the file on Windows (Linux/macOS don't strictly need
        // this but it matches behaviour).
        m_doc->close();
        // Same story for the qpdf editor: QPDF::processFile leaves
        // m_path open for lazy stream reads, and Windows refuses
        // DeleteFile on a handle opened without FILE_SHARE_DELETE.
        // We rebuild a fresh editor from the post-rename file at the
        // end of this method, so dropping the old one now costs
        // nothing. (Linux/macOS would tolerate the open handle; this
        // is purely a Windows shield.)
        m_editor.reset();
        auto restoreOnFailure = [this]() {
            m_editor = std::make_shared<PdfEditor>();
            m_editor->load(m_path);
            m_doc->load(m_path);
        };
        if (QFile::exists(ctx.targetPath) && !QFile::remove(ctx.targetPath)) {
            // Restore the original handle and bail; the staged temp
            // is leaked on disk but the user's file is untouched.
            restoreOnFailure();
            return false;
        }
        if (!QFile::rename(ctx.writePath, ctx.targetPath)) {
            restoreOnFailure();
            return false;
        }
    }

    m_path = ctx.targetPath;
    m_editor = std::make_shared<PdfEditor>();
    m_editor->load(m_path);
    // The editor is freshly loaded from the saved bytes; keep the lazy
    // gate consistent and drop the stale form-detection cache.
    m_editorLoaded = true;
    m_hasFormFieldsCache.reset();

    const int savedPage = currentPage();
    const double savedZoom = m_view ? m_view->zoomFactor() : 1.0;

    m_doc->close();
    const QPdfDocument::Error error = m_doc->load(m_path);
    if (error != QPdfDocument::Error::None) {
        return false;
    }
    m_previewFile.reset();
    // A successful Save repoints m_editor/m_doc at the backing file above, so a
    // private recovery-backing copy (if this doc was restored from a sidecar)
    // is no longer referenced — release it.
    m_recoveryBackingFile.reset();

    if (m_searchModel) {
        m_searchModel->setDocument(m_doc.get());
    }
    if (m_view) {
        m_view->setDocument(m_doc.get());
        if (savedPage >= 0 && savedPage < pageCount()) {
            m_view->pageNavigator()->jump(savedPage, QPointF{}, savedZoom);
        }
    }
    m_dirty = false;
    // Reload annotations from the saved editor without disturbing the
    // unified undo log — this churn is not user edits.
    m_suppressUndoLog = true;
    m_annotations.clear();
    for (Annotation &a : m_editor->readAnnotations()) {
        m_annotations.add(std::move(a));
    }
    m_annotations.clearHistory();
    m_suppressUndoLog = false;
    m_annotationsModified = false;
    // Annotation history was just cleared; the qpdf page-command stacks
    // are retained, so rebuild the unified log to match them.
    m_undoLog.assign(m_pdfUndoStack.size(), UndoSource::PdfCommand);
    m_redoLog.assign(m_pdfRedoStack.size(), UndoSource::PdfCommand);
    // Refresh the external-change baseline to the bytes we just wrote so our
    // own save never trips the conflict guard / monitor (ADR 2026-07-19).
    captureFileBaseline();
    return true;
}

bool PdfDocument::reloadFromDisk() {
    if (m_path.isEmpty() || !QFileInfo::exists(m_path))
        return false;
    // Compose with the file watcher (#89): a reload arriving while the initial
    // off-thread open is still in flight must SUPERSEDE it, not race it. Drain
    // and adopt the in-flight open first, then the synchronous reload below
    // replaces m_doc/m_editor with the fresh on-disk bytes. The retarget then
    // fires against the final, swapped-in view.
    ensureDocLoaded();
    ensureEditorLoaded();

    const int savedPage = currentPage();
    const double savedZoom = m_view ? m_view->zoomFactor() : 1.0;

    // Rebuild the qpdf editor and the QPdfDocument from the on-disk bytes,
    // mirroring saveCommitOnUi's reload half. A clean-doc reload discards our
    // (empty) edit state; the banner's explicit Reload discards edits the user
    // chose to drop, so there is nothing to preserve either way.
    //
    // Stage the new editor BEFORE disturbing the live one so a failed qpdf
    // parse leaves the current document fully intact (nothing swapped, nothing
    // closed).
    auto newEditor = std::make_shared<PdfEditor>();
    if (!newEditor->load(m_path)) {
        // The current editor + QPdfDocument are untouched; the view still
        // shows the previous content.
        return false;
    }

    // The editor parsed. Now reload the QPdfDocument, which must close the old
    // handle first. If that fails, restore a live document from the current
    // path and keep the existing editor (don't swap in the staged one) so the
    // view isn't left blank — this mirrors the editor-load-failure restore and
    // closes the F9 partial-failure gap.
    m_doc->close();
    if (m_doc->load(m_path) != QPdfDocument::Error::None) {
        m_doc->load(m_path);
        return false;
    }
    m_editor = newEditor;
    m_editorLoaded = true;
    m_hasFormFieldsCache.reset();
    m_previewFile.reset();
    m_valid = true;

    if (m_searchModel)
        m_searchModel->setDocument(m_doc.get());
    if (m_view) {
        m_view->setDocument(m_doc.get());
        if (savedPage >= 0 && savedPage < pageCount())
            m_view->pageNavigator()->jump(savedPage, QPointF{}, savedZoom);
    }

    // Drop every edit stack — a reload replaces the document wholesale.
    m_pdfUndoStack.clear();
    m_pdfRedoStack.clear();
    m_undoLog.clear();
    m_redoLog.clear();
    m_suppressUndoLog = true;
    m_annotations.clear();
    for (Annotation &a : m_editor->readAnnotations())
        m_annotations.add(std::move(a));
    m_annotations.clearHistory();
    m_suppressUndoLog = false;
    m_annotationsModified = false;
    m_dirty = false;

    captureFileBaseline();
    return true;
}

std::vector<FormField> PdfDocument::formFields() const {
    const_cast<PdfDocument *>(this)->ensureDocLoaded(); // definitive m_valid
    ensureEditorLoaded();
    if (!m_valid || !m_editor || !m_editor->isValid())
        return {};
    return m_editor->readFormFields();
}

bool PdfDocument::setFormFieldValue(int id, const QString &value) {
    ensureEditorLoaded();
    if (!m_valid || !m_editor || !m_editor->isValid())
        return false;
    const bool ok = m_editor->setFormFieldValue(id, value);
    if (ok)
        m_dirty = true;
    return ok;
}

void PdfDocument::setFormFillingActive(bool active) {
    if (active) {
        // Turning form-filling on is a genuine editor need.
        ensureEditorLoaded();
    }
    if (m_formOverlay) {
        if (active) {
            // Refresh fields in case the document changed since
            // the overlay was last populated.
            if (m_editor && m_editor->isValid()) {
                m_formOverlay->setFields(m_editor->readFormFields());
            }
            m_formOverlay->setGeometry(m_view ? m_view->viewport()->rect() : QRect{});
            m_formOverlay->show();
        } else {
            m_formOverlay->hide();
        }
    }
}

void PdfDocument::refreshFormView() {
    // Re-push field values into whichever widgets the overlay has
    // already built. Called after bulk writes (AutoFill) so the user
    // sees the new values immediately. Does not change the overlay's
    // visibility — if form-filling is off the refresh is a no-op until
    // the user toggles it on.
    ensureEditorLoaded();
    if (!m_formOverlay || !m_editor || !m_editor->isValid())
        return;
    m_formOverlay->setFields(m_editor->readFormFields());
}

bool PdfDocument::exportWithPassword(const QString &destPath, const QString &password) {
    ensureDocLoaded(); // definitive m_valid before the guards below
    ensureEditorLoaded();
    ensureAnnotationsLoadedSync();
    if (!m_valid || !m_editor || !m_editor->isValid())
        return false;
    if (destPath.isEmpty())
        return false;
    // Write annotations into the editor's QPDF graph (same as save),
    // then serialize to `destPath` with AES-256 encryption. We write to
    // a separate destination only — never overwrite the source file —
    // so the in-memory state remains unencrypted and further edits keep
    // working normally.
    if (!m_editor->applyRedactions(m_annotations.annotations()))
        return false;
    if (!m_editor->flattenSignatures(m_annotations.annotations()))
        return false;
    if (!m_editor->writeAnnotations(m_annotations.annotations()))
        return false;
    EncryptionOptions enc;
    enc.userPassword = password;
    return m_editor->save(destPath, enc);
}

bool PdfDocument::reduceFileSize(const QString &destPath) {
    ensureDocLoaded(); // definitive m_valid before the guards below
    ensureEditorLoaded();
    ensureAnnotationsLoadedSync();
    if (!m_valid || !m_editor || !m_editor->isValid())
        return false;
    if (destPath.isEmpty())
        return false;
    // Flush pending annotations first so the reduced output reflects
    // everything the user sees on screen. Linearization + object-
    // stream regeneration then re-packs the document.
    if (!m_editor->applyRedactions(m_annotations.annotations()))
        return false;
    if (!m_editor->flattenSignatures(m_annotations.annotations()))
        return false;
    if (!m_editor->writeAnnotations(m_annotations.annotations()))
        return false;
    return m_editor->saveReduced(destPath);
}

QStringList PdfAdapter::mimeTypes() const {
    return {QStringLiteral("application/pdf")};
}

QStringList PdfAdapter::extensions() const {
    return {QStringLiteral("pdf")};
}

namespace {

// Default prompt. Pops a modal QInputDialog on the active window with
// the password echo hidden. Returns nullopt if the user cancels or if
// there's no window to parent to (e.g. offscreen UAT without an
// installed test shim — we refuse to spin a dialog into the void).
std::optional<QString> defaultPasswordPrompt(const QString &path, int attempt) {
    QWidget *parent = QApplication::activeWindow();
    if (!parent)
        return std::nullopt;
    const int maxAttempts = 3;
    const QString title =
        attempt == 0
            ? QObject::tr("Password required")
            : QObject::tr("Password required (%1 attempts left)").arg(maxAttempts - attempt);
    const QString prompt = QObject::tr("“%1” is password-protected. Enter the password to open it.")
                               .arg(QFileInfo(path).fileName());
    bool ok = false;
    const QString pw =
        QInputDialog::getText(parent, title, prompt, QLineEdit::Password, QString(), &ok);
    if (!ok)
        return std::nullopt;
    return pw;
}

PdfAdapter::PasswordPrompt &activePasswordPrompt() {
    static PdfAdapter::PasswordPrompt prompt = defaultPasswordPrompt;
    return prompt;
}

} // namespace

void PdfAdapter::setPasswordPrompt(PasswordPrompt prompt) {
    activePasswordPrompt() = prompt ? std::move(prompt) : defaultPasswordPrompt;
}

PdfAdapter::PasswordPrompt PdfAdapter::passwordPrompt() {
    return activePasswordPrompt();
}

std::unique_ptr<IDocument> PdfAdapter::open(const QString &path) {
    auto doc = std::make_unique<PdfDocument>(path);

    // Password-gated PDF: prompt up to three times. Each iteration
    // asks the currently-installed PasswordPrompt hook; a nullopt
    // response ends the loop and leaves the document in its locked
    // state (createView falls back to a "Could not open" label).
    const int maxAttempts = 3;
    auto &prompt = activePasswordPrompt();
    for (int attempt = 0; attempt < maxAttempts && doc->needsPassword(); ++attempt) {
        std::optional<QString> pw = prompt(path, attempt);
        if (!pw)
            break;
        doc->unlock(*pw);
    }
    return doc;
}

} // namespace trailer
