#include "PdfAdapter.h"

#include <QFileInfo>
#include <QLabel>
#include <QPdfDocument>
#include <QPdfView>
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
