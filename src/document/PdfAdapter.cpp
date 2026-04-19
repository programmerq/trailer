#include "PdfAdapter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfDocument>
#include <QPdfPageNavigator>
#include <QPdfSearchModel>
#include <QPdfView>
#include <QPrintDialog>
#include <QPrinter>
#include <QScrollBar>
#include <QSizeF>
#include <QTemporaryFile>
#include <QVBoxLayout>

namespace trailer {

namespace {
constexpr double kZoomStep = 1.1;
constexpr double kZoomMin = 0.10;
constexpr double kZoomMax = 16.0;
}  // namespace

PdfDocument::PdfDocument(QString path)
    : m_path(std::move(path)),
      m_doc(std::make_unique<QPdfDocument>()),
      m_editor(std::make_unique<PdfEditor>()) {
    const QPdfDocument::Error error = m_doc->load(m_path);
    m_valid = (error == QPdfDocument::Error::None);
    if (m_valid) {
        m_editor->load(m_path);
    }
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
    QScrollBar* hbar = m_view->horizontalScrollBar();
    hbar->setValue((hbar->minimum() + hbar->maximum()) / 2);
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

void PdfDocument::print(QWidget* dialogParent) {
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
        if (pagePts.isEmpty()) continue;

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
    auto preview = std::make_unique<QTemporaryFile>(
        QDir::tempPath() + QStringLiteral("/trailer-preview-XXXXXX.pdf"));
    preview->setAutoRemove(true);
    if (!preview->open()) {
        return false;
    }
    const QString previewPath = preview->fileName();
    preview->close();
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
    return true;
}

void PdfDocument::rotatePage(int pageIndex, int degreesClockwise) {
    if (!m_valid || !m_editor || !m_editor->isValid()) {
        return;
    }
    if (pageIndex < 0 || pageIndex >= pageCount()) {
        return;
    }
    m_editor->rotatePage(pageIndex, degreesClockwise);
    if (reloadViewerFromEditor()) {
        m_dirty = true;
    }
}

void PdfDocument::deletePages(const std::vector<int>& pageIndices) {
    if (!m_valid || !m_editor || !m_editor->isValid() || pageIndices.empty()) {
        return;
    }
    const int before = m_editor->pageCount();
    if (static_cast<int>(pageIndices.size()) >= before) {
        return;  // refuse to delete every page
    }
    m_editor->deletePages(pageIndices);
    if (reloadViewerFromEditor()) {
        m_dirty = true;
    }
}

bool PdfDocument::extractPages(const std::vector<int>& pageIndices,
                               const QString& destPath) const {
    if (!m_valid || !m_editor || !m_editor->isValid()) return false;
    return m_editor->extractPages(pageIndices, destPath);
}

bool PdfDocument::insertPagesFrom(const QString& sourcePath, int insertAtIndex) {
    if (!m_valid || !m_editor || !m_editor->isValid()) {
        return false;
    }
    if (!m_editor->insertPagesFrom(sourcePath, insertAtIndex)) {
        return false;
    }
    if (reloadViewerFromEditor()) {
        m_dirty = true;
        return true;
    }
    return false;
}

void PdfDocument::movePage(int from, int to) {
    if (!m_valid || !m_editor || !m_editor->isValid()) {
        return;
    }
    const int total = m_editor->pageCount();
    if (from < 0 || from >= total || to < 0 || to >= total || from == to) {
        return;
    }
    m_editor->movePage(from, to);
    if (reloadViewerFromEditor()) {
        m_dirty = true;
    }
}

bool PdfDocument::save(const QString& newPath) {
    if (!m_valid || !m_editor || !m_editor->isValid()) {
        return false;
    }
    const QString targetPath = newPath.isEmpty() ? m_path : newPath;
    if (targetPath.isEmpty()) {
        return false;
    }

    if (QFileInfo(targetPath).canonicalFilePath()
        == QFileInfo(m_path).canonicalFilePath() && !m_path.isEmpty()) {
        auto temp = std::make_unique<QTemporaryFile>(
            QDir::tempPath() + QStringLiteral("/trailer-save-XXXXXX.pdf"));
        temp->setAutoRemove(true);
        if (!temp->open()) {
            return false;
        }
        const QString tempPath = temp->fileName();
        temp->close();
        if (!m_editor->save(tempPath)) {
            return false;
        }
        m_doc->close();
        if (!QFile::remove(targetPath) && QFile::exists(targetPath)) {
            m_doc->load(m_path);
            return false;
        }
        if (!QFile::rename(tempPath, targetPath)) {
            m_doc->load(m_path);
            return false;
        }
        temp->setAutoRemove(false);
    } else {
        if (!m_editor->save(targetPath)) {
            return false;
        }
    }

    m_path = targetPath;
    m_editor = std::make_unique<PdfEditor>();
    m_editor->load(m_path);

    const int savedPage = currentPage();
    const double savedZoom = m_view ? m_view->zoomFactor() : 1.0;

    m_doc->close();
    const QPdfDocument::Error error = m_doc->load(m_path);
    if (error != QPdfDocument::Error::None) {
        return false;
    }
    m_previewFile.reset();

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
    return true;
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
