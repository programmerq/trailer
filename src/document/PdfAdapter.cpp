#include "PdfAdapter.h"

#include "ui/AnnotationOverlay.h"
#include "ui/FormOverlay.h"
#include "ui/SelectableTextLayer.h"
#include "util/TempPath.h"

#include <QApplication>
#include <QDir>
#include <QEvent>
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

PdfDocument::PdfDocument(QString path)
    : m_path(std::move(path)), m_doc(std::make_unique<QPdfDocument>()),
      m_editor(std::make_shared<PdfEditor>()) {
    const QPdfDocument::Error error = m_doc->load(m_path);
    m_valid = (error == QPdfDocument::Error::None);
    // Password-gated PDFs are a special kind of load failure: the
    // caller (PdfAdapter::open) can recover by prompting for a
    // password and calling unlock(). Everything else (corrupt,
    // missing, unsupported scheme) stays permanently invalid.
    m_needsPassword = (error == QPdfDocument::Error::IncorrectPassword);
    // Deliberately NOT done here (P0 startup-hang fix; closed by #63,
    // residual tracked in
    // docs/backlog/2026-07-15-offthread-pdf-open-placeholder.md): the qpdf
    // processFile parse (m_editor->load) and the all-pages annotation
    // sweep (readAnnotations) both used to run synchronously in this
    // ctor, freezing the GUI thread for minutes on large PDFs. Both now run on
    // a BACKGROUND worker (startBackgroundLoad()) — the parse + AcroForm
    // detection AND the sweep — so neither blocks the GUI thread at open
    // (owner feedback on PR #63); the sync edit/save paths fall back to an
    // inline parse via ensureEditorLoaded() when they need a live editor
    // before the worker has finished. The kept QPdfDocument::load above is the
    // bounded progressive read that already yields pageCount + page-0.
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

bool PdfDocument::unlock(const QString &password) {
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
    return QFileInfo(m_path).fileName();
}

QString PdfDocument::filePath() const {
    return m_path;
}

int PdfDocument::pageCount() const {
    return m_valid ? m_doc->pageCount() : 0;
}

QWidget *PdfDocument::createView(QWidget *parent) {
    if (!m_valid) {
        auto *container = new QWidget(parent);
        auto *layout = new QVBoxLayout(container);
        auto *label = new QLabel(QObject::tr("Could not open PDF:\n%1").arg(m_path), container);
        label->setAlignment(Qt::AlignCenter);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(label);
        return container;
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
    // QPdfView paints search matches using the palette's Highlight
    // role. Override to a translucent yellow so matches look like
    // a marker-pen highlighter instead of a system selection.
    // (Qt versions that ignore the role for PDF render fall back
    // gracefully — the change is harmless.)
    QPalette pdfPalette = view->palette();
    pdfPalette.setColor(QPalette::Highlight, QColor(255, 235, 50, 160));
    pdfPalette.setColor(QPalette::HighlightedText, Qt::black);
    view->setPalette(pdfPalette);
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
    auto pageOriginInView = [this](int page) -> QPointF {
        if (!m_view || !m_doc || page < 0)
            return {};
        const double z = m_view->zoomFactor();
        const QMargins m = m_view->documentMargins();
        const int spacing = m_view->pageSpacing();
        const QSize vp = m_view->viewport()->size();

        double maxW = 0.0;
        const int total = m_doc->pageCount();
        for (int i = 0; i < total; ++i) {
            maxW = std::max(maxW, m_doc->pagePointSize(i).width() * z);
        }
        const double pw = m_doc->pagePointSize(page).width() * z;

        if (m_view->pageMode() == QPdfView::PageMode::SinglePage) {
            const int cur = m_view->pageNavigator()->currentPage();
            if (page != cur)
                return QPointF(-1e9, -1e9);
            const double contentW = maxW + m.left() + m.right();
            const double contentH = m_doc->pagePointSize(page).height() * z + m.top() + m.bottom();
            const double extraX = std::max(0.0, (vp.width() - contentW) / 2.0);
            const double extraY = std::max(0.0, (vp.height() - contentH) / 2.0);
            return QPointF(extraX + m.left() + (maxW - pw) / 2.0 -
                               m_view->horizontalScrollBar()->value(),
                           extraY + m.top() - m_view->verticalScrollBar()->value());
        }

        double y = m.top();
        for (int i = 0; i < page; ++i) {
            y += m_doc->pagePointSize(i).height() * z + spacing;
        }
        double contentH = m.top() + m.bottom();
        for (int i = 0; i < total; ++i) {
            contentH += m_doc->pagePointSize(i).height() * z;
            if (i > 0)
                contentH += spacing;
        }
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
            const QSizeF pt = m_doc->pagePointSize(i);
            const QRectF rect(origin.x(), origin.y(), pt.width() * z, pt.height() * z);
            if (rect.contains(viewPt))
                return i;
        }
        return m_view->pageNavigator()->currentPage();
    });
    overlay->setSourceSampler([this](QRectF docRect, QSize outPx, int page) -> QImage {
        if (!m_doc || page < 0 || docRect.isEmpty())
            return {};
        const QSizeF pagePts = m_doc->pagePointSize(page);
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
    overlay->setGeometry(view->viewport()->rect());
    overlay->show();
    m_overlay = overlay;

    QObject::connect(view->pageNavigator(), &QPdfPageNavigator::currentPageChanged, overlay,
                     [overlay](int page) {
                         if (overlay)
                             overlay->setPage(page);
                     });
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
            const QSizeF pt = m_doc->pagePointSize(i);
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
        return m_doc->pagePointSize(page);
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

    return view;
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
        break;
    case ViewMode::TwoPages:
        // Two-page (facing) layout is not supported: QPdfView::PageMode only
        // offers SinglePage and MultiPage, neither of which is a real two-up
        // layout. Deliberately do NOT alias Continuous here — silently showing
        // a different layout than the label promises is forbidden by policy.
        // The View > Two Pages action (m_twoPagesAction) is kept disabled with
        // an explanatory tooltip so this case is unreachable from the UI; this
        // guard prevents any future code path from regressing into a silent
        // alias. Leave the current page mode untouched.
        qWarning("PdfDocument::applyViewMode: ViewMode::TwoPages is unsupported "
                 "(no facing layout in QPdfView); leaving page mode unchanged");
        return;
    case ViewMode::Continuous:
        m_view->setPageMode(QPdfView::PageMode::MultiPage);
        break;
    }
}

void PdfDocument::setViewMode(ViewMode mode) {
    m_viewMode = mode;
    applyViewMode();
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
    m_view->setZoomMode(QPdfView::ZoomMode::FitToWidth);
}

void PdfDocument::zoomFitPage() {
    if (!m_view)
        return;
    m_view->setZoomMode(QPdfView::ZoomMode::FitInView);
}

QSize PdfDocument::contentSizeHint() const {
    if (!m_valid || !m_doc || m_doc->pageCount() <= 0)
        return {};
    const QSizeF pts = m_doc->pagePointSize(0);
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
    const QSizeF pagePts = m_doc->pagePointSize(0);
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
    const QSizeF pagePts = m_doc->pagePointSize(pageIndex);
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
    return m_doc->pagePointSize(pageIndex);
}

double PdfDocument::ocrSourceToDocScale(int pageIndex) const {
    if (!m_valid || !m_doc || pageIndex < 0 || pageIndex >= m_doc->pageCount())
        return 1.0;
    const QSizeF pagePts = m_doc->pagePointSize(pageIndex);
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
    const QSizeF pageSize = m_doc->pagePointSize(pageIndex);
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
    return m_view->pageNavigator()->currentPage();
}

void PdfDocument::goToPage(int pageIndex) {
    if (!m_view || pageIndex < 0 || pageIndex >= pageCount()) {
        return;
    }
    m_view->pageNavigator()->jump(pageIndex, QPointF{}, m_view->zoomFactor());
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
            m_view->setCurrentSearchResultIndex(seed);
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
    m_view->setCurrentSearchResultIndex(seed);
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

void PdfDocument::findNext() {
    if (!m_view || !m_searchModel)
        return;
    const int count = m_searchModel->rowCount({});
    if (count <= 0)
        return;
    m_currentResult = (m_currentResult + 1) % count;
    m_view->setCurrentSearchResultIndex(m_currentResult);
    refreshSearchHighlights();
}

void PdfDocument::findPrevious() {
    if (!m_view || !m_searchModel)
        return;
    const int count = m_searchModel->rowCount({});
    if (count <= 0)
        return;
    m_currentResult = (m_currentResult - 1 + count) % count;
    m_view->setCurrentSearchResultIndex(m_currentResult);
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
        const QSizeF pagePts = m_doc->pagePointSize(page);
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
    auto ctx = saveBeginQpdfPhase(newPath);
    if (!ctx)
        return false;
    return saveCommitOnUi(*ctx);
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
    if (ctx.sameFile) {
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
    return true;
}

std::vector<FormField> PdfDocument::formFields() const {
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
