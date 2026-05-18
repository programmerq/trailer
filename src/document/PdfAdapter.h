#pragma once

#include "IDocument.h"
#include "IFormatAdapter.h"
#include "PdfCommands.h"
#include "PdfEditor.h"
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

namespace trailer {

class AnnotationOverlay;
class FormOverlay;

class PdfDocument : public IDocument {
  public:
    explicit PdfDocument(QString path);
    ~PdfDocument() override;

    QString displayName() const override;
    QString filePath() const override;
    QWidget *createView(QWidget *parent) override;

    bool supportsZoom() const override { return true; }
    void zoomIn() override;
    void zoomOut() override;
    void zoomActual() override;
    void zoomFitWidth() override;
    void zoomFitPage() override;

    bool supportsViewModes() const override { return true; }
    ViewMode viewMode() const override { return m_viewMode; }
    void setViewMode(ViewMode mode) override;

    bool supportsThumbnails() const override { return true; }
    int pageCount() const override;
    QImage renderThumbnail(int pageIndex, QSize targetSize) override;
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
    bool supportsPasswordExport() const override {
        return m_valid && m_editor && m_editor->isValid();
    }
    bool exportWithPassword(const QString &destPath, const QString &password) override;

    bool supportsFileSizeReduction() const override {
        return m_valid && m_editor && m_editor->isValid();
    }
    bool reduceFileSize(const QString &destPath) override;

    bool supportsFormFilling() const override {
        return m_valid && m_editor && m_editor->isValid() && m_editor->hasFormFields();
    }
    // PDFs always carry a text layer (even scan-only PDFs typically
    // expose an empty layer). Text-aware markup tools are offered.
    bool hasTextLayer() const override { return m_valid; }

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
    // PdfCommand stack for qpdf-level mutations (rotate today;
    // delete / move / insert / crop are scoped for follow-up).
    // Undo prefers the most recently-touched stack; the
    // last-touched stack is tracked via m_lastUndoSource.
    bool canUndo() const override;
    bool canRedo() const override;
    void undo() override;
    void redo() override;
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

    AnnotationStore *annotations() override { return &m_annotations; }
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
    void applyViewMode();
    void applyZoomFactor(double factor);
    bool reloadViewerFromEditor();
    // Called from the search model's rowsInserted signal on the GUI
    // thread once the asynchronous search produces at least one hit.
    // Pushes m_currentResult into the view so the match is highlighted
    // and scrolled into view — this is where the "Find found nothing"
    // bug on OCR'd PDFs used to live.
    void onSearchResultsPopulated();
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
    QPointer<FormOverlay> m_formOverlay;
    AnnotationStore m_annotations;
    ViewMode m_viewMode = ViewMode::Continuous;
    int m_currentResult = -1;
    bool m_valid = false;
    bool m_dirty = false;
    bool m_annotationsModified = false;
    bool m_needsPassword = false;

    // qpdf-mutation undo stacks. Each PdfCommand owns its own
    // forward+revert state (e.g. a RotatePageCommand keeps the
    // original page index and rotation delta). Pushing a new
    // command clears the redo stack — the conventional behaviour
    // when the user undoes, then makes a different change.
    std::vector<std::unique_ptr<PdfCommand>> m_pdfUndoStack;
    std::vector<std::unique_ptr<PdfCommand>> m_pdfRedoStack;
    // Tracks which undo log got the most recent push so the
    // unified IDocument::undo prefers the right stack. Annotations
    // and qpdf commands aren't merged into one chronological log
    // yet (TODO).
    enum class UndoSource { None, Annotation, PdfCommand };
    UndoSource m_lastUndoSource = UndoSource::None;
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
