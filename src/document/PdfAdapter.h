#pragma once

#include "IDocument.h"
#include "IFormatAdapter.h"

#include <QPointer>
#include <QString>
#include <QStringList>
#include <memory>

class QPdfDocument;
class QPdfSearchModel;
class QPdfView;

namespace trailer {

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

    bool isValid() const { return m_valid; }

private:
    void applyViewMode();
    void applyZoomFactor(double factor);

    QString m_path;
    std::unique_ptr<QPdfDocument> m_doc;
    std::unique_ptr<QPdfSearchModel> m_searchModel;
    QPointer<QPdfView> m_view;
    ViewMode m_viewMode = ViewMode::Continuous;
    int m_currentResult = -1;
    bool m_valid = false;
};

class PdfAdapter : public IFormatAdapter {
public:
    QStringList mimeTypes() const override;
    QStringList extensions() const override;
    std::unique_ptr<IDocument> open(const QString& path) override;
};

}  // namespace trailer
