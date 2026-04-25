#pragma once

#include "IDocument.h"
#include "IFormatAdapter.h"
#include "PdfEditor.h"
#include "annotation/AnnotationStore.h"

#include <QPointer>
#include <QString>
#include <QStringList>
#include <functional>
#include <memory>
#include <optional>

class QTemporaryFile;

class QPdfDocument;
class QPdfSearchModel;
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
    QWidget* createView(QWidget* parent) override;

    bool supportsZoom() const override { return true; }
    void zoomIn() override;
    void zoomOut() override;
    void zoomActual() override;
    void zoomFitWidth() override;

    bool supportsViewModes() const override { return true; }
    ViewMode viewMode() const override { return m_viewMode; }
    void setViewMode(ViewMode mode) override;

    bool supportsThumbnails() const override { return true; }
    int pageCount() const override;
    QImage renderThumbnail(int pageIndex, QSize targetSize) override;
    int currentPage() const override;
    void goToPage(int pageIndex) override;

    bool supportsSearch() const override { return true; }
    void setSearchQuery(const QString& query) override;
    void findNext() override;
    void findPrevious() override;
    void clearSearch() override;

    bool supportsPrint() const override { return m_valid; }
    void print(QWidget* dialogParent) override;

    bool supportsEditing() const override { return m_valid; }
    bool supportsPasswordExport() const override {
        return m_valid && m_editor && m_editor->isValid();
    }
    bool exportWithPassword(const QString& destPath,
                            const QString& password) override;

    bool supportsFileSizeReduction() const override {
        return m_valid && m_editor && m_editor->isValid();
    }
    bool reduceFileSize(const QString& destPath) override;

    bool supportsFormFilling() const override {
        return m_valid && m_editor && m_editor->isValid()
               && m_editor->hasFormFields();
    }
    std::vector<FormField> formFields() const override;
    bool setFormFieldValue(int id, const QString& value) override;
    void setFormFillingActive(bool active) override;
    void refreshFormView() override;
    bool isDirty() const override {
        return m_dirty || m_annotationsModified;
    }
    bool canUndo() const override { return m_annotations.canUndo(); }
    bool canRedo() const override { return m_annotations.canRedo(); }
    void undo() override { m_annotations.undo(); }
    void redo() override { m_annotations.redo(); }
    void rotatePage(int pageIndex, int degreesClockwise) override;
    void deletePages(const std::vector<int>& pageIndices) override;
    void movePage(int from, int to) override;
    bool insertPagesFrom(const QString& sourcePath, int insertAtIndex) override;
    bool extractPages(const std::vector<int>& pageIndices, const QString& destPath) const override;
    bool cropPage(int pageIndex, double leftPts, double topPts,
                  double rightPts, double bottomPts) override;
    bool cropPages(const std::vector<int>& pageIndices,
                   double leftPts, double topPts,
                   double rightPts, double bottomPts) override;
    bool save(const QString& newPath = {}) override;

    AnnotationStore* annotations() override { return &m_annotations; }
    void setAnnotationTool(AnnotationTool tool) override;
    void setAnnotationStyle(const AnnotationStyle& style) override;
    void setPendingAnnotationText(const QString& text) override;
    void setPendingSignaturePath(const QString& path) override;

    bool isValid() const { return m_valid; }

    // Phase 5: password-protected open flow. If load() hits
    // IncorrectPassword, needsPassword() returns true and unlock() can
    // be retried with a password from the user. Stays false for plain
    // PDFs.
    bool needsPassword() const { return m_needsPassword; }
    bool unlock(const QString& password);

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

    QString m_path;
    std::unique_ptr<QPdfDocument> m_doc;
    std::unique_ptr<QPdfSearchModel> m_searchModel;
    std::unique_ptr<PdfEditor> m_editor;
    std::unique_ptr<QTemporaryFile> m_previewFile;
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
};

class PdfAdapter : public IFormatAdapter {
public:
    QStringList mimeTypes() const override;
    QStringList extensions() const override;
    std::unique_ptr<IDocument> open(const QString& path) override;

    // Password-prompt hook. open() calls this when a PDF refuses to
    // load for IncorrectPassword reasons. Return a populated optional
    // to attempt an unlock; return std::nullopt to stop prompting
    // (equivalent to the user clicking Cancel). `attempt` starts at 0
    // so the hook can word the message differently after a failed try.
    //
    // The default hook uses QInputDialog on the active window. UAT
    // tests install a shim here so they can exercise the retry loop
    // without an interactive dialog.
    using PasswordPrompt = std::function<std::optional<QString>(
        const QString& path, int attempt)>;
    static void setPasswordPrompt(PasswordPrompt prompt);
    static PasswordPrompt passwordPrompt();
};

}  // namespace trailer
