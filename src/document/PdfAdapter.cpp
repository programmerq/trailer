#include "PdfAdapter.h"

#include <QFileInfo>
#include <QLabel>
#include <QPdfDocument>
#include <QPdfPageNavigator>
#include <QPdfView>
#include <QVBoxLayout>

namespace trailer {

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
    view->setPageMode(QPdfView::PageMode::MultiPage);
    view->setZoomMode(QPdfView::ZoomMode::FitToWidth);
    return view;
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
