#pragma once

#include "IDocument.h"
#include "IFormatAdapter.h"
#include "PdfEditor.h"
#include "annotation/AnnotationStore.h"

#include <QPointer>
#include <QString>
#include <QStringList>
#include <memory>

class QTemporaryFile;

class QPdfDocument;
class QPdfSearchModel;
class QPdfView;

namespace trailer {

class AnnotationOverlay;

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

    bool isValid() const { return m_valid; }

private:
    void applyViewMode();
    void applyZoomFactor(double factor);
    bool reloadViewerFromEditor();

    QString m_path;
    std::unique_ptr<QPdfDocument> m_doc;
    std::unique_ptr<QPdfSearchModel> m_searchModel;
    std::unique_ptr<PdfEditor> m_editor;
    std::unique_ptr<QTemporaryFile> m_previewFile;
    QPointer<QPdfView> m_view;
    QPointer<AnnotationOverlay> m_overlay;
    AnnotationStore m_annotations;
    ViewMode m_viewMode = ViewMode::Continuous;
    int m_currentResult = -1;
    bool m_valid = false;
    bool m_dirty = false;
    bool m_annotationsModified = false;
};

class PdfAdapter : public IFormatAdapter {
public:
    QStringList mimeTypes() const override;
    QStringList extensions() const override;
    std::unique_ptr<IDocument> open(const QString& path) override;
};

}  // namespace trailer
