#pragma once

#include "IDocument.h"
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

class QPdfDocument;
class QPdfSearchModel;
class QPdfBookmarkModel;
class QIdentityProxyModel;
class QPdfView;
template <typename T> class QFutureWatcher;
// tests/test_adapters.cpp — befriended so the desync test seam below
// stays private instead of shipping as callable production API.
class TestAdapters;

namespace trailer {

class AnnotationOverlay;
class FormOverlay;
class SelectableTextLayer;

class PdfDocument : public IDocument {
  public:
    explicit PdfDocument(QString path);
    ~PdfDocument() override;

    QString displayName() const override;
    QString filePath() const override;
    QWidget *createView(QWidget *parent) override;

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
    // docs/backlog/2026-07-13-startup-hang-large-pdf.md).
    bool supportsPasswordExport() const override { return m_valid; }
    bool exportWithPassword(const QString &destPath, const QString &password) override;

    bool supportsFileSizeReduction() const override { return m_valid; }
    bool reduceFileSize(const QString &destPath) override;

    // Form-field detection genuinely needs the qpdf editor (there is no
    // cheaper way to know a PDF carries an AcroForm). MainWindow queries
    // this synchronously when a document becomes current, so it is the
    // one capability probe that triggers the lazy editor load — the
    // bounded qpdf processFile parse, NOT the whole-document annotation
    // sweep (which stays deferred). The result is cached so repeated
    // toolbar refreshes don't re-scan.
    bool supportsFormFilling() const override {
        if (!m_valid)
            return false;
        ensureEditorLoaded();
        if (!m_editor->isValid())
            return false;
        if (!m_hasFormFieldsCache)
            m_hasFormFieldsCache = m_editor->hasFormFields();
        return *m_hasFormFieldsCache;
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
    bool setFormFieldValue(int id, const QString &value) override;
    void setFormFillingActive(bool active) override;
    void refreshFormView() override;
    bool isDirty() const override { return m_dirty || m_annotationsModified; }
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
        // First genuine annotation access kicks the deferred all-pages
        // sweep onto a BACKGROUND worker (idempotent) and returns the store
        // immediately — empty at first, populated live when the worker
        // commits. This is the P0 fix: the ~12s sweep on a heavily-annotated
        // document no longer runs on the GUI thread at view-attach. The
        // overlay/sidebar/inspector all subscribe to AnnotationStore::changed,
        // so the late populate propagates automatically.
        startAnnotationLoad();
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

    bool isValid() const { return m_valid; }

    // Phase 5: password-protected open flow. If load() hits
    // IncorrectPassword, needsPassword() returns true and unlock() can
    // be retried with a password from the user. Stays false for plain
    // PDFs.
    bool needsPassword() const { return m_needsPassword; }
    bool unlock(const QString &password);

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

    // --- Lazy open gates (P0 startup-hang fix) ---
    // The two heavy whole-document passes that used to run in the ctor
    // are deferred to first genuine access:
    //   ensureEditorLoaded()      — runs the qpdf processFile parse once
    //                               (and re-applies a remembered unlock
    //                               password on an encrypted doc). const
    //                               because capability probes / const
    //                               accessors gate on it; it only mutates
    //                               the mutable load-state members below.
    //   startAnnotationLoad()     — kicks the all-pages annotation sweep
    //                               onto a background worker ONCE (loading a
    //                               throwaway, isolated qpdf instance from
    //                               m_path — see the .cpp). The GUI-thread
    //                               finished slot then commits the result.
    //                               Wires the annotation history hooks
    //                               synchronously up front so user edits made
    //                               during the (possibly multi-second) load
    //                               are tracked; the bulk populate itself is
    //                               committed via AnnotationStore::addBatch
    //                               (no undo frame) under m_suppressUndoLog,
    //                               so it is never mistaken for a user edit.
    // Both are idempotent and no-op while the document is locked/invalid.
    void ensureEditorLoaded() const;
    void startAnnotationLoad();
    // Wire the AnnotationStore history/modified mirrors (idempotent). Split
    // out of the commit so it can also run synchronously the instant the
    // store is first handed out, keeping edits tracked during the load.
    void ensureAnnotationHooksWired();
    // Block for the deferred annotation load and commit its result NOW.
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
    // QFutureWatcher::finished slot — commits the worker's result unless a
    // sync-ensure already beat it to it.
    void onAnnotationLoadFinished();

    void applyViewMode();
    void applyZoomFactor(double factor);
    // Fit the freshly-opened doc into the viewport on first show.
    // Caps at 100% — small documents stay at actual size rather than
    // being upscaled. One-shot: subsequent currentDocumentChanged
    // events leave the user's zoom alone.
    void applyInitialFitZoom(QPdfView *view);
    bool reloadViewerFromEditor();
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
    std::unique_ptr<QPdfDocument> m_doc;
    std::unique_ptr<QPdfSearchModel> m_searchModel;
    // QPdfBookmarkModel is lazy: only created the first time
    // outlineModel() is called, so PDFs we never open a TOC view on
    // don't pay for the tree walk. The proxy remaps the model's
    // Title role onto Qt::DisplayRole so vanilla QTreeView shows
    // the bookmark titles without a custom delegate.
    mutable std::unique_ptr<QPdfBookmarkModel> m_bookmarkModel;
    mutable std::unique_ptr<QIdentityProxyModel> m_outlineProxy;
    std::unique_ptr<PdfEditor> m_editor;
    std::unique_ptr<ScopedTempFile> m_previewFile;
    QPointer<QPdfView> m_view;
    QPointer<AnnotationOverlay> m_overlay;
    QPointer<SelectableTextLayer> m_textLayer;
    QPointer<FormOverlay> m_formOverlay;
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
    // Lazy-open state (see ensureEditorLoaded/startAnnotationLoad).
    // m_editorLoaded is mutable so const capability probes can trigger
    // the parse. m_password is remembered from unlock() so the deferred
    // editor load (and the background sweep's throwaway editor) can
    // re-unlock the qpdf side; it is mutable so the const editor probe can
    // consume+clear it, and it is dropped once both consumers have taken
    // their copy so plaintext isn't retained for the doc lifetime.
    // m_hasFormFieldsCache memoises the one form-detection scan; invalidated
    // on any edit that rebuilds the editor's page graph.
    //
    // Threading note: every flag here is touched only on the GUI thread.
    // The background sweep worker touches NONE of them — it operates purely
    // on value copies (path/password) and its own local, isolated PdfEditor
    // — so there is no GUI/worker race on this object's state.
    mutable bool m_editorLoaded = false;
    // m_annotationsLoaded: the sweep result has been committed into the
    // store. m_annotationLoadStarted: the background worker has been kicked
    // (guards against a second launch). m_annotationHooksWired: the store's
    // history/modified mirrors have been connected.
    bool m_annotationsLoaded = false;
    bool m_annotationLoadStarted = false;
    bool m_annotationHooksWired = false;
    mutable QString m_password;
    mutable std::optional<bool> m_hasFormFieldsCache;
    // Watches the background annotation-load future. Held as a member so
    // its lifetime is bounded by this document: the destructor resets it so
    // a still-pending finished signal cannot fire on a half-destroyed this.
    // The worker lambda captures only value copies, so it stays safe as it
    // winds down after the watcher is dropped.
    std::unique_ptr<QFutureWatcher<std::vector<Annotation>>> m_annotationWatcher;
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
