#pragma once

#include "CapabilityNotifier.h"
#include "IDocument.h"
#include "PageChangeNotifier.h"
#include "IFormatAdapter.h"
#include "PdfCommands.h"
#include "PdfEditor.h"
#include "SelectableTextStore.h"
#include "annotation/AnnotationStore.h"
#include "util/TempPath.h"

#include <QPointer>
#include <QString>
#include <QStringList>
#include <functional>
#include <memory>
#include <optional>

class QIODevice;
class QPdfDocument;
class QPdfSearchModel;
class QPdfBookmarkModel;
class QIdentityProxyModel;
class QPdfView;
class QStackedWidget;
template <typename T> class QFutureWatcher;
// tests/test_adapters.cpp — befriended so the desync test seam below
// stays private instead of shipping as callable production API.
class TestAdapters;

namespace trailer {

class AnnotationOverlay;
class FormOverlay;
class SelectableTextLayer;
class TwoPageView;

class PdfDocument : public IDocument {
  public:
    explicit PdfDocument(QString path);
    ~PdfDocument() override;

    QString displayName() const override;
    QString filePath() const override;
    QWidget *createView(QWidget *parent) override;
    void refreshViewPalette() override;

    DocumentType documentType() const override { return DocumentType::Pdf; }

    bool supportsZoom() const override { return true; }
    void zoomIn() override;
    void zoomOut() override;
    void zoomActual() override;
    void zoomFitWidth() override;
    void zoomFitPage() override;
    QSize contentSizeHint() const override;

    ZoomMode zoomMode() const override;
    double zoomFactor() const override;
    void applyZoomState(ZoomMode mode, double factor) override;
    int scrollY() const override;
    void applyScrollY(int y) override;

    bool supportsViewModes() const override { return true; }
    ViewMode viewMode() const override { return m_viewMode; }
    void setViewMode(ViewMode mode) override;

    bool supportsThumbnails() const override { return true; }
    int pageCount() const override;
    QImage renderThumbnail(int pageIndex, QSize targetSize) override;
    QSizeF pageSizeHint(int pageIndex) const override;
    int currentPage() const override;
    void goToPage(int pageIndex) override;
    // Fires on every navigator page change (keyboard paging, thumbnail jumps,
    // continuous-scroll page crossings), so the Sidebar page-sync and the
    // auto-OCR / missing-model hint re-derivation react to a real signal
    // instead of polling currentPage() on a timer.
    PageChangeNotifier *pageChangeNotifier() override { return &m_pageChangeNotifier; }
    int nextPageIndex() const override;
    int previousPageIndex() const override;

    bool supportsSearch() const override { return true; }
    void setSearchQuery(const QString &query) override;
    void findNext() override;
    void findPrevious() override;
    void clearSearch() override;
    int searchMatchCount() const override;
    int currentSearchMatchIndex() const override;
    std::vector<int> pagesWithSearchMatches() const override;

    bool supportsPrint() const override { return m_valid; }
    void print(QWidget *dialogParent) override;

    bool supportsEditing() const override { return m_valid; }
    // Any valid PDF can be re-encrypted / re-linearized on export; the
    // qpdf editor is loaded lazily by the actual export action, so these
    // capability probes must NOT force the parse (P0 startup-hang fix,
    // closed by #63; residual in
    // docs/backlog/2026-07-15-offthread-pdf-open-placeholder.md).
    bool supportsPasswordExport() const override { return m_valid; }
    bool exportWithPassword(const QString &destPath, const QString &password) override;

    bool supportsFileSizeReduction() const override { return m_valid; }
    bool reduceFileSize(const QString &destPath) override;

    // Form-field detection genuinely needs the qpdf editor (there is no
    // cheaper way to know a PDF carries an AcroForm). The ~0.55s qpdf
    // processFile parse this used to force synchronously must NOT block the
    // GUI thread at open (owner feedback on PR #63): so this probe no longer
    // forces a synchronous ensureEditorLoaded(). It answers definitively
    // from the editor once the background load has adopted it, and returns a
    // provisional "not ready yet" (false) meanwhile — kicking that
    // background load so the answer resolves. The forms toolbar is disabled
    // during the window and enables when capabilitiesChanged() fires (G3
    // disabled-not-lying). The result is cached so repeated toolbar
    // refreshes don't re-scan; an edit that rebuilds the page graph clears
    // the cache (reloadViewerFromEditor) and the definitive branch recomputes.
    bool supportsFormFilling() const override {
        if (!m_valid)
            return false;
        if (m_editorLoaded && m_editor && m_editor->isValid()) {
            if (!m_hasFormFieldsCache)
                m_hasFormFieldsCache = m_editor->hasFormFields();
            return *m_hasFormFieldsCache;
        }
        // Editor not adopted yet: kick the background load (idempotent,
        // non-blocking) and report provisionally not-ready. const_cast: the
        // kick mutates lazy-load bookkeeping, mirroring how ensureEditorLoaded
        // mutates its mutable members from a const probe.
        const_cast<PdfDocument *>(this)->startBackgroundLoad();
        return false;
    }
    // PDFs always carry a text layer (even scan-only PDFs typically
    // expose an empty layer). Text-aware markup tools are offered.
    bool hasTextLayer() const override { return m_valid; }

    // Per-page native-text probe: true iff Qt PDF can extract a non-
    // empty text string for this page (a born-digital page, as opposed
    // to an image-only scan). Cheap enough to call on page change.
    bool pageHasText(int page) const override;

    // PDF outline (Table of Contents) — backed by QPdfBookmarkModel,
    // lazily constructed on first access. Empty for documents without
    // an /Outlines tree; hasOutline() pre-checks rowCount so the
    // Sidebar picker can grey-out the TOC mode entry.
    QAbstractItemModel* outlineModel() override;
    bool hasOutline() const override;
    void goToOutlineEntry(const QModelIndex& index) override;
    std::vector<FormField> formFields() const override;
    // Fires once the background load has parsed the editor + detected the
    // AcroForm, so MainWindow can re-run its forms-toolbar setup a moment
    // after open (owner feedback on PR #63).
    CapabilityNotifier *capabilityNotifier() override { return &m_capabilityNotifier; }
    bool setFormFieldValue(int id, const QString &value) override;
    void setFormFillingActive(bool active) override;
    void refreshFormView() override;
    bool isDirty() const override { return m_dirty || m_annotationsModified; }
    // True iff this document carries STRUCTURAL (qpdf page-graph) edits —
    // rotate / delete / move / insert / crop — as opposed to only unsaved
    // annotation edits. The kept-windows (⌥⌘Q) capture reconstructs
    // annotation-only dirtiness by reopening the file and re-applying the
    // annotations editable (restoreAnnotationsFromDraft), but it CANNOT
    // reconstruct structural edits from the annotation JSON, so a
    // structurally-dirty PDF falls back to the per-doc prompt (flagged
    // residual — see the decision record + docs/backlog).
    bool hasStructuralEdits() const { return m_dirty; }
    // True iff this document carries a still-PENDING (un-applied) redaction or
    // signature annotation. These are the only annotation kinds
    // writeRecoverySnapshot() bakes DESTRUCTIVELY into page content
    // (applyRedactions / flattenSignatures), so a ⌥⌘Q structural-keep blob of
    // such a doc would come back with them BURNED IN and no longer editable —
    // a silent irreversible commit. The kept-windows capture routes exactly
    // this combination to the per-doc prompt instead (Application::
    // canDraftForKeep; docs/backlog/2026-07-20-nondestructive-structural-
    // redaction-keep.md). Forces a synchronous annotation load first so the
    // check sees the COMPLETE set (never a racy, still-loading store) — hence
    // non-const (the sync load commits the deferred annotation result).
    bool hasPendingDestructiveAnnotation();
    // Rehydrate this document from a kept-windows draft on session restore:
    // re-apply `annotations` (the document's unsaved annotations, captured
    // at ⌥⌘Q) as individually editable objects and, when `dirty`, mark the
    // document modified so isDirty() reports true (it returned still-unsaved,
    // exactly as at quit). The file's own on-disk annotations are NOT
    // separately swept — `annotations` already carries the complete set
    // (saved + unsaved) captured in memory — so the background sweep is
    // short-circuited to avoid double-applying the on-disk subset. Undo
    // history is intentionally not restored.
    void restoreAnnotationsFromDraft(const QList<Annotation> &annotations, bool dirty);
    // PDF-level undo runs across two parallel stacks: the
    // AnnotationStore for in-memory shape edits, and a separate
    // PdfCommand stack for qpdf-level mutations (rotate / delete /
    // move / insert / crop).
    // Undo/redo pop a single chronological log (m_undoLog) recording
    // which stack each committed op went to, so the most recent action
    // is always undone first regardless of which stack it came from.
    bool canUndo() const override;
    bool canRedo() const override;
    bool undo() override;
    bool redo() override;
    void rotatePage(int pageIndex, int degreesClockwise) override;
    void deletePages(const std::vector<int> &pageIndices) override;
    void movePage(int from, int to) override;
    bool insertPagesFrom(const QString &sourcePath, int insertAtIndex) override;
    bool extractPages(const std::vector<int> &pageIndices, const QString &destPath) const override;
    bool cropPage(int pageIndex, double leftPts, double topPts, double rightPts,
                  double bottomPts) override;
    bool cropPages(const std::vector<int> &pageIndices, double leftPts, double topPts,
                   double rightPts, double bottomPts) override;
    bool save(const QString &newPath = {}) override;
    bool writeRecoverySnapshot(const QString &sidecarPath) override;
    bool recoverFrom(const QString &sidecarPath) override;
    bool reloadFromDisk() override;

    // True once markUntitledForRecovery() has run: the document holds recovered
    // (owned-copy-backed) content but has no on-disk home, so displayName shows
    // "Untitled" and MainWindow routes its first Save through Save-As.
    bool isUntitled() const override { return m_untitled; }
    // Convert a just-recovered document into an UNTITLED one. Used by the
    // kept-windows (⌥⌘Q) restore when a StructuralDraft's ORIGINAL file is gone:
    // the self-sufficient edited blob already loaded via recoverFrom() still
    // returns as unsaved, dirty work whose first Save prompts Save-As, rather
    // than the captured edits being silently dropped. Clears m_path (no home)
    // and marks the doc dirty; the recovered content is untouched.
    void markUntitledForRecovery() {
        m_untitled = true;
        m_path.clear();
        m_dirty = true;
    }

    // Test seam: force writeRecoverySnapshot() to fail, so a test can exercise
    // the ⌥⌘Q keep-flow's snapshot-preflight fallback — an unsnapshottable
    // structural PDF must be PROMPTED, never silently dropped (Application::
    // canDraftForKeep FIX 1). No production caller sets this.
    void setForceRecoverySnapshotFailureForTesting(bool fail) {
        m_forceRecoverySnapshotFailureForTesting = fail;
    }

    // Two-phase save for off-thread execution. The first phase
    // (saveBeginQpdfPhase) does only thread-safe qpdf work and may
    // be called from a worker thread. The second phase
    // (saveCommitOnUi) must run on the UI thread because it touches
    // QPdfDocument and QPdfView. MainWindow uses these via
    // QtConcurrent::run + QFutureWatcher to keep the UI responsive
    // during multi-second saves on large or heavily-redacted PDFs.
    // The synchronous save() above is kept for tests and for
    // documents where blocking on save is acceptable; both APIs are
    // exclusive (do not interleave calls).
    struct SaveContext {
        QString writePath;  // where the worker wrote the new bytes
        QString targetPath; // where they should end up (== writePath
                            // for non-overwrite, != for overwrite)
        bool sameFile = false;
        // True iff this save was a deliberate "Keep mine" clobber (the
        // one-shot force flag was armed at begin time). Carried into the
        // commit phase so the commit-time re-stat guard (F1) lets a forced
        // clobber through while still blocking an unforced one.
        bool forced = false;
    };
    // Computes the SaveContext (worker-safe), runs all qpdf
    // operations, and writes to writePath. Returns nullopt on
    // failure. Safe to call from any thread that is not interleaved
    // with another save on this document.
    std::optional<SaveContext> saveBeginQpdfPhase(const QString &newPath);
    // Publishes a successful saveBeginQpdfPhase result: rename the
    // temp file (for same-file overwrite), reload QPdfDocument and
    // re-attach the view / search model. UI-thread only. Returns
    // true on success.
    bool saveCommitOnUi(const SaveContext &ctx);

    AnnotationStore *annotations() override {
        // Obtaining the annotation store implies the document is being used, so
        // the deferred off-thread open must be adopted first: dirty-tracking
        // (ensureAnnotationHooksWired) needs m_valid + the adopted m_doc as its
        // connection context, and it is wired via startBackgroundLoad below. If
        // an edit lands on the store BEFORE adoption (e.g. an annotation added
        // before the doc is attached to a window — the ctor used to make m_valid
        // true synchronously), the hooks would be skipped and the edit would not
        // mark the document dirty. ensureDocLoaded() restores that invariant; it
        // drains only the doc-open worker (waitForFinished — reads already off
        // the GUI thread), NOT the annotation sweep, which stays asynchronous.
        ensureDocLoaded();
        // First genuine annotation access kicks the deferred unified load onto
        // a BACKGROUND worker (idempotent) and returns the store immediately —
        // empty at first, populated live when the worker commits. This is the
        // P0 fix: the ~12s sweep on a heavily-annotated document no longer runs
        // on the GUI thread at view-attach (and, since PR #63, neither does the
        // qpdf parse + AcroForm detection). The overlay/sidebar/inspector all
        // subscribe to AnnotationStore::changed, so the late populate
        // propagates automatically.
        startBackgroundLoad();
        return &m_annotations;
    }
    SelectableTextStore *selectableText() override { return &m_selectableText; }
    bool supportsSelectableText() const override { return m_valid; }
    QImage renderPageForOcr(int pageIndex) const override;
    double ocrSourceToDocScale(int pageIndex) const override;
    // Ingest the native PDF text layer for `page` into m_selectableText
    // as line-level TextBlocks (text + point-space geometry), so drag-
    // select + Ctrl+C work on born-digital pages without an OCR run. No-
    // op when the page has no native text or the store already holds
    // results for it (never clobbers real OCR output). Called lazily as
    // pages become current. Non-const: writes into the store.
    void ingestNativeTextLayer(int page);
    void setAnnotationTool(AnnotationTool tool) override;
    void setAnnotationStyle(const AnnotationStyle &style) override;
    void setPendingAnnotationText(const QString &text) override;
    void setPendingSignaturePath(const QString &path) override;

    // Forces the deferred off-thread open to settle so validity is definitive.
    bool isValid() const;

    // Phase 5: password-protected open flow. If load() hits
    // IncorrectPassword, needsPassword() returns true and unlock() can
    // be retried with a password from the user. Stays false for plain
    // PDFs. Forces the deferred off-thread open to settle (ensureDocLoaded)
    // so the answer is definitive for PdfAdapter::open's prompt loop.
    bool needsPassword() const;
    bool unlock(const QString &password);

    // Test seam (mirrors setPasswordPrompt): supply a QIODevice the background
    // document-open loads QPdfDocument from, so the structural perf harness can
    // observe which thread performs the open's file reads. The factory is
    // invoked ON THE WORKER THREAD and must return a fresh, unparented,
    // read-only QIODevice built from `path`; the worker takes ownership
    // (parents it to the loaded QPdfDocument). Unset (default) loads directly
    // from the path. Never used in production paths.
    using LoadDeviceFactory = std::function<QIODevice *(const QString &path)>;
    static void setLoadDeviceFactoryForTesting(LoadDeviceFactory factory);

  private:
    // Test seam only, private + friend-fenced so no production caller
    // can reach it: drop the qpdf command stacks while leaving the
    // chronological log untouched, simulating the log/stack desync the
    // runtime guards in undo()/redo() defend against. There is no
    // production path that produces this state.
    friend class ::TestAdapters;
    void corruptPdfCommandStacksForTesting() {
        m_pdfUndoStack.clear();
        m_pdfRedoStack.clear();
    }

    // --- Lazy open gates (P0 startup-hang fix + PR #63 off-thread parse) ---
    // The heavy whole-document passes that used to run in the ctor are
    // deferred off the synchronous open path. A single background worker now
    // does BOTH the all-pages annotation sweep AND the qpdf processFile parse
    // + AcroForm detection, so neither blocks the GUI thread at open:
    //   startBackgroundLoad()     — kicks that worker ONCE. On its own
    //                               isolated qpdf instances (see the .cpp) it
    //                               (1) sweeps annotations on a throwaway
    //                               instance freed before it returns — keeping
    //                               steady-state RSS low (Option B, DR 0006) —
    //                               and (2) parses a separate, parse-only
    //                               editor and reads AcroForm presence. The
    //                               GUI-thread finished slot ADOPTS that editor
    //                               as m_editor and commits the annotations.
    //                               Wires the annotation history hooks
    //                               synchronously up front so user edits made
    //                               during the (possibly multi-second) load
    //                               are tracked; the bulk populate itself is
    //                               committed via AnnotationStore::addBatch
    //                               (no undo frame) under m_suppressUndoLog,
    //                               so it is never mistaken for a user edit.
    //   ensureEditorLoaded()      — the sync consumers (edit/save paths) that
    //                               genuinely need a live editor NOW. If the
    //                               background load is in flight it BLOCKS for
    //                               the worker and adopts its editor; if the
    //                               load was never kicked it parses inline (the
    //                               old path). const because const accessors
    //                               gate on it; only mutates mutable state.
    // All are idempotent and no-op while the document is locked/invalid.
    //
    // The single worker result: the parse-only editor to adopt (shared_ptr so
    // the copyable QFuture result requirement is satisfied; moved out via
    // takeResult so the ~GB annotation vector is never copied), the swept
    // annotations, and the AcroForm-presence answer.
    struct BackgroundLoadResult {
        std::vector<Annotation> annotations;
        std::shared_ptr<PdfEditor> editor; // parse-only; adopted as m_editor
        bool hasFormFields = false;
    };
    void ensureEditorLoaded() const;
    void startBackgroundLoad();

    // --- Off-GUI-thread initial document open (backlog
    // 2026-07-15-offthread-pdf-open-placeholder; deferred (b) of DR 0006) ---
    // The residual synchronous open cost is QPdfDocument::load. It now runs on
    // a worker thread kicked from the ctor: the worker constructs a
    // QPdfDocument (worker affinity), loads it (its file reads therefore run
    // OFF the GUI thread), then moveToThread()s it back to the GUI thread and
    // hands the raw pointer back for adoption. The raw pointer keeps the QFuture
    // result trivially copyable and lets m_doc (a unique_ptr) adopt it directly.
    struct DocOpenResult {
        QPdfDocument *doc = nullptr; // heap-allocated on the worker, moved to
                                     // the GUI thread; adopted into m_doc
        // The QPdfDocument::Error is collapsed to two flags here so this header
        // needs only a forward declaration of QPdfDocument (the enum is nested
        // and cannot be named on a forward-declared type). The worker computes
        // them from the real error code (it includes <QPdfDocument>).
        bool ok = false;            // load succeeded (Error::None)
        bool needsPassword = false; // load failed with IncorrectPassword
    };
    // Kicked once from the ctor. Non-blocking: the reads happen on the worker.
    void startDocOpen();
    // Sync-fallback for consumers that need the load's result NOW (pageCount,
    // needsPassword, createView's real-view build, save/reload paths). Mirrors
    // ensureEditorLoaded: blocks on the worker via waitForFinished (which does
    // NOT spin the event loop, so it cannot re-enter/deadlock) and adopts the
    // result on the calling (GUI) thread. Idempotent. The file reads still
    // happened on the worker — this only *waits*, it does not read.
    void ensureDocLoaded();
    // GUI-thread adoption of the worker's loaded QPdfDocument: take ownership
    // into m_doc, set m_valid / m_needsPassword, and — if a placeholder view
    // container is waiting — swap in the real view. Idempotent (the released
    // watcher makes a second call a no-op).
    void adoptDocOpenResult();
    // QFutureWatcher::finished slot for the doc-open future.
    void onDocOpenFinished();
    // Builds the real QPdfView (or a "Could not open" label) + overlays as a
    // child of `parent` and returns it. Called two ways: directly from
    // createView when the open already settled — the widget is returned as-is,
    // preserving the long-standing "createView returns the QPdfView" contract —
    // and from adoptDocOpenResult, which mounts the returned widget into the
    // placeholder container after the worker finishes. Extracted from the former
    // createView body.
    QWidget *buildRealView(QWidget *parent);
    // Drain the in-flight background load (must already be started) and adopt
    // its result on the GUI thread: adopt the parse-only editor as m_editor
    // (unless a sync path already parsed one), cache the AcroForm answer,
    // commit the annotation set (unless a sync-ensure beat it), and fire
    // capabilitiesChanged(). Idempotent — the released-watcher guard makes it
    // a no-op after the first adoption. Never called from within the
    // watcher's own finished emission except via the finished slot below.
    void adoptBackgroundLoadResult();
    // Wire the AnnotationStore history/modified mirrors (idempotent). Split
    // out of the commit so it can also run synchronously the instant the
    // store is first handed out, keeping edits tracked during the load.
    void ensureAnnotationHooksWired();
    // Block for the deferred load and commit its annotation result NOW.
    // Used by the synchronous consumers that must see the COMPLETE set
    // (save / exportWithPassword / reduceFileSize). If a background load is
    // in flight it waits for the worker; if the load was never kicked it
    // reads synchronously through the GUI-thread editor (the old inline
    // path). Never invoked from within the load's own finished slot.
    void ensureAnnotationsLoadedSync();
    // GUI-thread commit of a loaded annotation set: batched populate that
    // emits a single AnnotationStore::changed and touches neither the dirty
    // flag nor the undo log.
    void commitAnnotations(std::vector<Annotation> loaded);
    // QFutureWatcher::finished slot — adopts the worker's result unless a
    // sync-ensure already beat it to it.
    void onBackgroundLoadFinished();

    void applyViewMode();
    void applyZoomFactor(double factor);
    // Sets the QPdfView's search-highlight colour AND pins its ::Dark
    // canvas-surround role to the current ::Base colour (see
    // refreshViewPalette()). Called at construction (buildRealView) and
    // again whenever the app theme changes, so the pin never goes stale.
    static void applyViewPalette(QPdfView *view);
    // Fit the freshly-opened doc into the viewport on first show.
    // Caps at 100% — small documents stay at actual size rather than
    // being upscaled. One-shot: subsequent currentDocumentChanged
    // events leave the user's zoom alone.
    void applyInitialFitZoom(QPdfView *view);

  public:
    // Test seam, mirroring ImageDocument::triggerInitialZoomForTest(): fire
    // the one-shot initial fit the way buildRealView()'s deferred QTimer
    // does, so a headless test can prove that a (Custom, 0.0) "not captured"
    // sentinel left that decision still REACHABLE rather than silently
    // consuming it. No-op without a view. Defined out-of-line because
    // QPdfView is only forward-declared here.
    void triggerInitialZoomForTest();

  private:
    bool reloadViewerFromEditor();
    // Make `index` the highlighted search result AND bring it on screen.
    // setCurrentSearchResultIndex alone only changes which rectangle the
    // overlay paints as "current" — it does not move the viewport, so a
    // match outside the current page/scroll position is selected but
    // invisible (the real dogfooding bug: "it selects the next match but
    // doesn't jump me to the next match"). QPdfSearchModel::resultAtIndex
    // returns a QPdfLink; QPdfPageNavigator::jump(QPdfLink) is the same
    // navigation primitive goToPage()/goToOutlineEntry() use, so advancing a
    // search match behaves exactly like any other page-change. No-op when
    // index is out of range (including -1, used to clear the selection).
    void applySearchResultIndex(int index);
    // Called from the search model's rowsInserted signal on the GUI
    // thread once the asynchronous search produces at least one hit.
    // Pushes m_currentResult into the view so the match is highlighted
    // and scrolled into view — this is where the "Find found nothing"
    // bug on OCR'd PDFs used to live.
    void onSearchResultsPopulated();
    // Position-aware seed (ADR 0006): the smallest populated result-row
    // index whose page is >= `page`. Returns 0 (wrap to the first match)
    // when nothing sits at/after the page, or the model is empty. Results
    // arrive in page order, so the first such row is also the first
    // reading-order match on the at/after page.
    int firstResultIndexAtOrAfter(int page) const;
    // Walk the search model and push every match rectangle into the
    // annotation overlay's search-highlight pass, flagging the
    // current-index match as `isCurrent`. Re-run whenever the model
    // populates, the current index changes, or the search clears.
    void refreshSearchHighlights();

    QString m_path;
    // Set only by markUntitledForRecovery(): a recovered structural-draft doc
    // whose original file was gone at ⌥⌘Q restore time. Backs isUntitled() so
    // its first Save routes through Save-As (see the header method comment).
    bool m_untitled = false;
    // Test-only: when set, writeRecoverySnapshot() returns false immediately.
    // See setForceRecoverySnapshotFailureForTesting().
    bool m_forceRecoverySnapshotFailureForTesting = false;
    std::unique_ptr<QPdfDocument> m_doc;
    std::unique_ptr<QPdfSearchModel> m_searchModel;
    // QPdfBookmarkModel is lazy: only created the first time
    // outlineModel() is called, so PDFs we never open a TOC view on
    // don't pay for the tree walk. The proxy remaps the model's
    // Title role onto Qt::DisplayRole so vanilla QTreeView shows
    // the bookmark titles without a custom delegate.
    mutable std::unique_ptr<QPdfBookmarkModel> m_bookmarkModel;
    mutable std::unique_ptr<QIdentityProxyModel> m_outlineProxy;
    // shared_ptr (not unique_ptr) so the background worker can hand the
    // parsed editor back through a copyable QFuture result and the GUI thread
    // can adopt it here by move (see startBackgroundLoad / adoptBackgroundLoadResult).
    std::shared_ptr<PdfEditor> m_editor;
    std::unique_ptr<ScopedTempFile> m_previewFile;
    // When the document was restored from a recovery sidecar (recoverFrom),
    // the live m_editor/m_doc are backed by a PRIVATE copy of the sidecar held
    // here — never the deterministic sidecar path itself. Otherwise the next
    // auto-save tick (writeRecoverySnapshot writes the deterministic sidecar
    // for this backing path) would truncate the very file the live editor/
    // viewer hold open, corrupting the recovered document. Removed on
    // destruction; reset on the next Save (which repoints m_editor at the
    // backing file).
    std::unique_ptr<ScopedTempFile> m_recoveryBackingFile;
    QPointer<QPdfView> m_view;
    QPointer<AnnotationOverlay> m_overlay;
    QPointer<SelectableTextLayer> m_textLayer;
    QPointer<FormOverlay> m_formOverlay;
    // AUGMENT wiring (decision record 2026-07-21-two-page-layout, D1-A):
    // createView returns a QStackedWidget holding the QPdfView surface
    // (Single/Continuous, unchanged) at index 0 and the custom TwoPageView
    // (Two-Pages mode) at index 1. applyViewMode swaps the visible page.
    QPointer<QStackedWidget> m_viewStack;
    QPointer<TwoPageView> m_twoPageView;
    // Live current page (0-based leading page of the top-most visible spread)
    // reported by TwoPageView as the user free-scrolls in Two-Pages mode. The
    // hidden QPdfView navigator can't observe that surface's scrolling, so
    // currentPage() reads this instead when m_viewMode == TwoPages, keeping the
    // sidebar current-page highlight live. Updated by a signal, never scrolled
    // back (no feedback loop).
    int m_twoPageCurrentPage = 0;
    AnnotationStore m_annotations;
    SelectableTextStore m_selectableText;
    ViewMode m_viewMode = ViewMode::Continuous;
    int m_currentResult = -1;
    // Async-populate seed guard (ADR 0006 R1). The search model streams
    // rowsInserted incrementally in page order, so each populate re-seeds
    // against the growing model until the current-page rows arrive.
    // m_seedPending is set true when a non-empty query is issued and is
    // cleared only on a reset event: a new/empty query (search() or
    // clearSearch()) or genuine user navigation freezing the seed. It is
    // deliberately NOT cleared when the async seed simply settles — a
    // fully-populated model keeps re-running onSearchResultsPopulated, which
    // recomputes the same converged seed each time, so leaving the flag set
    // is harmless. m_seedFromPage is the viewport page captured at query
    // time; m_provisionalSeedIndex is the last seed index WE pushed, so a
    // differing view index reads as genuine user navigation and freezes the
    // seed.
    bool m_seedPending = false;
    int m_seedFromPage = 0;
    int m_provisionalSeedIndex = -1;
    bool m_valid = false;
    bool m_dirty = false;
    bool m_annotationsModified = false;
    bool m_needsPassword = false;
    // Lazy-open state (see ensureEditorLoaded/startBackgroundLoad).
    // m_editorLoaded is mutable so const capability probes can trigger
    // the parse. m_password is remembered from unlock() so the deferred
    // editor load (and the background worker's isolated editors) can
    // re-unlock the qpdf side; it is mutable so the const editor probe can
    // consume+clear it, and it is dropped once both consumers have taken
    // their copy so plaintext isn't retained for the doc lifetime.
    // m_hasFormFieldsCache memoises the one form-detection scan; invalidated
    // on any edit that rebuilds the editor's page graph.
    //
    // Threading note: every flag here is touched only on the GUI thread.
    // The background worker touches NONE of them — it operates purely on
    // value copies (path/password) and its own local, isolated PdfEditor
    // instances, handed back through the QFuture result — so there is no
    // GUI/worker race on this object's state.
    mutable bool m_editorLoaded = false;
    // m_annotationsLoaded: the sweep result has been committed into the
    // store. m_backgroundLoadStarted: the background worker has been kicked
    // (guards against a second launch). m_annotationHooksWired: the store's
    // history/modified mirrors have been connected.
    bool m_annotationsLoaded = false;
    bool m_backgroundLoadStarted = false;
    bool m_annotationHooksWired = false;
    mutable QString m_password;
    mutable std::optional<bool> m_hasFormFieldsCache;
    // Emitter fired once the background load has adopted the editor + detected
    // the AcroForm, so MainWindow re-runs its forms-toolbar setup (PR #63).
    CapabilityNotifier m_capabilityNotifier;
    // Emitter fired on every navigator page change so page-driven UI (Sidebar
    // sync, auto-OCR / missing-model hint) reacts to a signal instead of a
    // poll timer. Owned by value; its QObject lifetime is bounded by this doc.
    PageChangeNotifier m_pageChangeNotifier;
    // Watches the unified background-load future (annotation sweep + editor
    // parse + AcroForm detection). Held as a member so its lifetime is bounded
    // by this document: the destructor resets it so a still-pending finished
    // signal cannot fire on a half-destroyed this. The worker lambda captures
    // only value copies and its own local editors, so it stays safe as it
    // winds down after the watcher is dropped.
    std::unique_ptr<QFutureWatcher<BackgroundLoadResult>> m_backgroundWatcher;
    // --- Off-GUI-thread initial open state (see startDocOpen) ---
    // m_docOpenStarted: the ctor kicked the worker (guards a second launch).
    // m_docLoaded: the worker's result has been adopted into m_doc on the GUI
    // thread (m_valid/m_needsPassword are then definitive). Until adoption
    // m_doc is null, so every deref is gated behind ensureDocLoaded()/m_valid.
    // m_docOpenWatcher is reset in the destructor so a still-pending finished
    // signal can't fire on a half-destroyed this (the worker lambda captures
    // only value copies, so it stays self-contained as it winds down).
    bool m_docOpenStarted = false;
    bool m_docLoaded = false;
    std::unique_ptr<QFutureWatcher<DocOpenResult>> m_docOpenWatcher;
    // Test-only injectable device factory for the background open (see the
    // public setLoadDeviceFactoryForTesting). Null in production.
    static LoadDeviceFactory s_loadDeviceFactory;
    // The container returned by createView. Holds the "Loading…" placeholder
    // until the open settles, then buildRealView() swaps in the QPdfView. A
    // QPointer so a container destroyed by its parent (tab close mid-load)
    // reads back null in the finished slot.
    QPointer<QWidget> m_viewContainer;
    QPointer<QWidget> m_placeholder;
    // One-shot guard for applyInitialFitZoom — fit-to-content is
    // applied the first time the viewport has a real size, then never
    // again so the user's zoom choices stick.
    bool m_initialZoomApplied = false;

    // qpdf-mutation undo stacks. Each PdfCommand owns its own
    // forward+revert state (e.g. a RotatePageCommand keeps the
    // original page index and rotation delta). Pushing a new
    // command clears the redo stack — the conventional behaviour
    // when the user undoes, then makes a different change.
    std::vector<std::unique_ptr<PdfCommand>> m_pdfUndoStack;
    std::vector<std::unique_ptr<PdfCommand>> m_pdfRedoStack;
    // Append a PdfCommand entry to the chronological undo log and
    // invalidate all redo (both qpdf + annotation redo, and m_redoLog).
    // Called after a qpdf-level command applies.
    void recordPdfCommandApplied();

    // AnnotationStore mirror hooks, connected to historyPushed /
    // historyEvicted in both the constructor and unlock(). The store
    // owns the annotation history depth; these keep the chronological
    // log's Annotation entries in lockstep with the store's undo stack
    // so the log never claims an undo the store cannot perform.
    void onAnnotationHistoryPushed();
    void onAnnotationHistoryEvicted();
    void connectAnnotationHistory();

    // Unified chronological undo/redo log: one entry per committed op,
    // recording which stack it went to, so undo()/redo() pop the truly
    // most-recent op regardless of source. Replaces the old
    // most-recently-touched-stack heuristic.
    enum class UndoSource { None, Annotation, PdfCommand };
    std::vector<UndoSource> m_undoLog;
    std::vector<UndoSource> m_redoLog;
    // Guards the historyPushed handler during a post-save reload so the
    // re-read annotations aren't logged as user undo steps.
    bool m_suppressUndoLog = false;
};

class PdfAdapter : public IFormatAdapter {
  public:
    QStringList mimeTypes() const override;
    QStringList extensions() const override;
    std::unique_ptr<IDocument> open(const QString &path) override;

    // Password-prompt hook. open() calls this when a PDF refuses to
    // load for IncorrectPassword reasons. Return a populated optional
    // to attempt an unlock; return std::nullopt to stop prompting
    // (equivalent to the user clicking Cancel). `attempt` starts at 0
    // so the hook can word the message differently after a failed try.
    //
    // The default hook uses QInputDialog on the active window. UAT
    // tests install a shim here so they can exercise the retry loop
    // without an interactive dialog.
    using PasswordPrompt = std::function<std::optional<QString>(const QString &path, int attempt)>;
    static void setPasswordPrompt(PasswordPrompt prompt);
    static PasswordPrompt passwordPrompt();
};

} // namespace trailer
