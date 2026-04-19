#include "PdfAdapter.h"

#include <QFileInfo>
#include <QLabel>
#include <QPdfDocument>
#include <QPdfPageNavigator>
#include <QPdfSearchModel>
#include <QPdfView>
#include <QSizeF>
#include <QVBoxLayout>

namespace trailer {

namespace {
constexpr double kZoomStep = 1.25;
constexpr double kZoomMin = 0.10;
constexpr double kZoomMax = 16.0;
}  // namespace

PdfDocument::PdfDocument(QString path)
    : m_path(std::move(path)), m_doc(std::make_unique<QPdfDocument>()) {
    const QPdfDocument::Error error = m_doc->load(m_path);
    m_valid = (error == QPdfDocument::Error::None);
}

PdfDocument::~PdfDocument() = default;

QString PdfDocument::displayName() const {
    return QFileInfo(m_path).fileName();
}

QString PdfDocument::filePath() const {
    return m_path;
}

int PdfDocument::pageCount() const {
    return m_valid ? m_doc->pageCount() : 0;
}

QWidget* PdfDocument::createView(QWidget* parent) {
    if (!m_valid) {
        auto* container = new QWidget(parent);
        auto* layout = new QVBoxLayout(container);
        auto* label = new QLabel(
            QObject::tr("Could not open PDF:\n%1").arg(m_path), container);
        label->setAlignment(Qt::AlignCenter);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(label);
        return container;
    }

    auto* view = new QPdfView(parent);
    view->setDocument(m_doc.get());
    view->setZoomMode(QPdfView::ZoomMode::FitToWidth);
    m_view = view;
    if (m_searchModel) {
        view->setSearchModel(m_searchModel.get());
        if (m_currentResult >= 0) {
            view->setCurrentSearchResultIndex(m_currentResult);
        }
    }
    applyViewMode();
    return view;
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
}

void PdfDocument::zoomIn() {
    if (!m_view) return;
    applyZoomFactor(m_view->zoomFactor() * kZoomStep);
}

void PdfDocument::zoomOut() {
    if (!m_view) return;
    applyZoomFactor(m_view->zoomFactor() / kZoomStep);
}

void PdfDocument::zoomActual() {
    applyZoomFactor(1.0);
}

void PdfDocument::zoomFitWidth() {
    if (!m_view) return;
    m_view->setZoomMode(QPdfView::ZoomMode::FitToWidth);
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
    return m_doc->render(pageIndex, QSize(w, h));
}

int PdfDocument::currentPage() const {
    if (!m_view) return 0;
    return m_view->pageNavigator()->currentPage();
}

void PdfDocument::goToPage(int pageIndex) {
    if (!m_view || pageIndex < 0 || pageIndex >= pageCount()) {
        return;
    }
    m_view->pageNavigator()->jump(pageIndex, QPointF{}, m_view->zoomFactor());
}

void PdfDocument::setSearchQuery(const QString& query) {
    if (!m_valid) {
        return;
    }
    if (!m_searchModel) {
        m_searchModel = std::make_unique<QPdfSearchModel>();
        m_searchModel->setDocument(m_doc.get());
    }
    m_searchModel->setSearchString(query);
    m_currentResult = query.isEmpty() ? -1 : 0;
    if (m_view) {
        m_view->setSearchModel(m_searchModel.get());
        if (m_currentResult >= 0 && m_searchModel->rowCount({}) > 0) {
            m_view->setCurrentSearchResultIndex(m_currentResult);
        }
    }
}

void PdfDocument::findNext() {
    if (!m_view || !m_searchModel) return;
    const int count = m_searchModel->rowCount({});
    if (count <= 0) return;
    m_currentResult = (m_currentResult + 1) % count;
    m_view->setCurrentSearchResultIndex(m_currentResult);
}

void PdfDocument::findPrevious() {
    if (!m_view || !m_searchModel) return;
    const int count = m_searchModel->rowCount({});
    if (count <= 0) return;
    m_currentResult = (m_currentResult - 1 + count) % count;
    m_view->setCurrentSearchResultIndex(m_currentResult);
}

void PdfDocument::clearSearch() {
    if (m_searchModel) {
        m_searchModel->setSearchString(QString());
    }
    m_currentResult = -1;
    if (m_view) {
        m_view->setCurrentSearchResultIndex(-1);
    }
}

QStringList PdfAdapter::mimeTypes() const {
    return {QStringLiteral("application/pdf")};
}

QStringList PdfAdapter::extensions() const {
    return {QStringLiteral("pdf")};
}

std::unique_ptr<IDocument> PdfAdapter::open(const QString& path) {
    return std::make_unique<PdfDocument>(path);
}

}  // namespace trailer
