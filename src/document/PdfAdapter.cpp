#include "PdfAdapter.h"

#include "ui/AnnotationOverlay.h"

#include <QDir>
#include <QEvent>
#include <QObject>
#include <QResizeEvent>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QKeyEvent>
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

class NavigablePdfView : public QPdfView {
public:
    explicit NavigablePdfView(QWidget* parent) : QPdfView(parent) {}

protected:
    void keyPressEvent(QKeyEvent* e) override {
        if (pageMode() == QPdfView::PageMode::SinglePage) {
            const int key = e->key();
            QScrollBar* vbar = verticalScrollBar();
            const bool atBottom = vbar->value() >= vbar->maximum();
            const bool atTop = vbar->value() <= vbar->minimum();
            auto* nav = pageNavigator();
            const int current = nav->currentPage();
            const int last = document() ? document()->pageCount() - 1 : 0;
            if ((key == Qt::Key_Down || key == Qt::Key_PageDown ||
                 key == Qt::Key_Space) && atBottom && current < last) {
                nav->jump(current + 1, QPointF{}, zoomFactor());
                verticalScrollBar()->setValue(verticalScrollBar()->minimum());
                e->accept();
                return;
            }
            if ((key == Qt::Key_Up || key == Qt::Key_PageUp) && atTop &&
                current > 0) {
                nav->jump(current - 1, QPointF{}, zoomFactor());
                verticalScrollBar()->setValue(verticalScrollBar()->maximum());
                e->accept();
                return;
            }
        }
        QPdfView::keyPressEvent(e);
    }
};
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

    auto* view = new NavigablePdfView(parent);
    view->setDocument(m_doc.get());
    view->setZoomMode(QPdfView::ZoomMode::Custom);
    view->setZoomFactor(1.0);
    m_view = view;
    if (m_searchModel) {
        view->setSearchModel(m_searchModel.get());
        if (m_currentResult >= 0) {
            view->setCurrentSearchResultIndex(m_currentResult);
        }
    }
    applyViewMode();

    auto* overlay = new AnnotationOverlay(view->viewport());
    overlay->setStore(&m_annotations);
    overlay->setPage(view->pageNavigator()->currentPage());
    auto pageOriginInView = [this]() -> QPointF {
        if (!m_view) return {};
        const int page = m_view->pageNavigator()->currentPage();
        const QSizeF pt = m_doc->pagePointSize(page);
        const double z = m_view->zoomFactor();
        const QSize vp = m_view->viewport()->size();
        const double pw = pt.width() * z;
        const double ph = pt.height() * z;
        const double ox = std::max(0.0, (vp.width() - pw) / 2.0)
            - m_view->horizontalScrollBar()->value();
        const double oy = std::max(0.0, (vp.height() - ph) / 2.0)
            - m_view->verticalScrollBar()->value();
        return QPointF(ox, oy);
    };
    overlay->setDocumentToView([this, pageOriginInView](QPointF p) {
        if (!m_view) return p;
        const double z = m_view->zoomFactor();
        const QPointF origin = pageOriginInView();
        return QPointF(origin.x() + p.x() * z, origin.y() + p.y() * z);
    });
    overlay->setViewToDocument([this, pageOriginInView](QPointF p) {
        if (!m_view) return p;
        const double z = m_view->zoomFactor();
        if (z <= 0.0) return p;
        const QPointF origin = pageOriginInView();
        return QPointF((p.x() - origin.x()) / z, (p.y() - origin.y()) / z);
    });
    overlay->setTextSelectionProvider(
        [this](QPointF startDoc, QPointF endDoc, int page)
            -> std::vector<QRectF> {
            if (!m_doc || page < 0) return {};
            const QPdfSelection sel = m_doc->getSelection(page, startDoc, endDoc);
            if (!sel.isValid()) return {};
            std::vector<QRectF> out;
            for (const QPolygonF& poly : sel.bounds()) {
                out.push_back(poly.boundingRect());
            }
            return out;
        });
    overlay->setGeometry(view->viewport()->rect());
    overlay->show();
    m_overlay = overlay;

    QObject::connect(view->pageNavigator(), &QPdfPageNavigator::currentPageChanged,
                     overlay, [overlay](int page) {
                         if (overlay) overlay->setPage(page);
                     });
    QObject::connect(view->verticalScrollBar(), &QScrollBar::valueChanged,
                     overlay, QOverload<>::of(&QWidget::update));
    QObject::connect(view->horizontalScrollBar(), &QScrollBar::valueChanged,
                     overlay, QOverload<>::of(&QWidget::update));
    QObject::connect(view, &QPdfView::zoomFactorChanged,
                     overlay, QOverload<>::of(&QWidget::update));
    view->viewport()->installEventFilter(overlay);

    return view;
}

void PdfDocument::setAnnotationTool(AnnotationTool tool) {
    if (m_overlay) m_overlay->setActiveTool(tool);
}

void PdfDocument::setAnnotationStyle(const AnnotationStyle& style) {
    if (m_overlay) m_overlay->setStyle(style);
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

bool PdfDocument::cropPage(int pageIndex, double leftPts, double topPts,
                           double rightPts, double bottomPts) {
    if (!m_valid || !m_editor || !m_editor->isValid()) return false;
    if (!m_editor->cropPage(pageIndex, leftPts, topPts, rightPts, bottomPts)) {
        return false;
    }
    if (reloadViewerFromEditor()) {
        m_dirty = true;
        return true;
    }
    return false;
}

bool PdfDocument::cropPages(const std::vector<int>& pageIndices,
                            double leftPts, double topPts,
                            double rightPts, double bottomPts) {
    if (!m_valid || !m_editor || !m_editor->isValid() || pageIndices.empty()) {
        return false;
    }
    bool any = false;
    for (int idx : pageIndices) {
        if (m_editor->cropPage(idx, leftPts, topPts, rightPts, bottomPts)) {
            any = true;
        }
    }
    if (!any) return false;
    if (reloadViewerFromEditor()) {
        m_dirty = true;
        return true;
    }
    return false;
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

    if (!m_editor->writeAnnotations(m_annotations.annotations())) {
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
    m_annotations.clear();
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
