#include "PdfAdapter.h"

#include "ui/AnnotationOverlay.h"
#include "ui/FormOverlay.h"

#include <QApplication>
#include <QDir>
#include <QEvent>
#include <QInputDialog>
#include <QLineEdit>
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
#include <QPdfDocumentRenderOptions>
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
    // Password-gated PDFs are a special kind of load failure: the
    // caller (PdfAdapter::open) can recover by prompting for a
    // password and calling unlock(). Everything else (corrupt,
    // missing, unsupported scheme) stays permanently invalid.
    m_needsPassword = (error == QPdfDocument::Error::IncorrectPassword);
    if (m_valid) {
        m_editor->load(m_path);
        for (Annotation& a : m_editor->readAnnotations()) {
            m_annotations.add(std::move(a));
        }
        m_annotations.clearHistory();
        QObject::connect(&m_annotations, &AnnotationStore::changed,
                         m_doc.get(), [this]() {
            m_annotationsModified = true;
            m_lastUndoSource = UndoSource::Annotation;
        });
    }
}

bool PdfDocument::unlock(const QString& password) {
    if (m_valid) return true;
    if (!m_needsPassword) return false;

    m_doc->setPassword(password);
    const QPdfDocument::Error error = m_doc->load(m_path);
    if (error != QPdfDocument::Error::None) {
        // Wrong password or some other problem. Keep m_needsPassword
        // true only if it's still a password issue so the caller can
        // re-prompt; anything else becomes a hard failure.
        m_needsPassword = (error == QPdfDocument::Error::IncorrectPassword);
        return false;
    }

    m_valid = true;
    m_needsPassword = false;

    // Mirror the unlock on the qpdf-backed editor so edits and
    // annotation round-tripping work. If the editor fails to load,
    // editing just won't work — the viewer path still does.
    m_editor->load(m_path);
    if (m_editor->isEncrypted()) {
        m_editor->unlock(password);
    }
    if (m_editor->isValid()) {
        for (Annotation& a : m_editor->readAnnotations()) {
            m_annotations.add(std::move(a));
        }
        m_annotations.clearHistory();
        QObject::connect(&m_annotations, &AnnotationStore::changed,
                         m_doc.get(), [this]() {
            m_annotationsModified = true;
            m_lastUndoSource = UndoSource::Annotation;
        });
    }
    return true;
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
    auto pageOriginInView = [this](int page) -> QPointF {
        if (!m_view || !m_doc || page < 0) return {};
        const double z = m_view->zoomFactor();
        const QMargins m = m_view->documentMargins();
        const int spacing = m_view->pageSpacing();
        const QSize vp = m_view->viewport()->size();

        double maxW = 0.0;
        const int total = m_doc->pageCount();
        for (int i = 0; i < total; ++i) {
            maxW = std::max(maxW, m_doc->pagePointSize(i).width() * z);
        }
        const double pw = m_doc->pagePointSize(page).width() * z;

        if (m_view->pageMode() == QPdfView::PageMode::SinglePage) {
            const int cur = m_view->pageNavigator()->currentPage();
            if (page != cur) return QPointF(-1e9, -1e9);
            const double contentW = maxW + m.left() + m.right();
            const double contentH = m_doc->pagePointSize(page).height() * z
                + m.top() + m.bottom();
            const double extraX = std::max(0.0, (vp.width() - contentW) / 2.0);
            const double extraY = std::max(0.0, (vp.height() - contentH) / 2.0);
            return QPointF(
                extraX + m.left() + (maxW - pw) / 2.0
                    - m_view->horizontalScrollBar()->value(),
                extraY + m.top()
                    - m_view->verticalScrollBar()->value());
        }

        double y = m.top();
        for (int i = 0; i < page; ++i) {
            y += m_doc->pagePointSize(i).height() * z + spacing;
        }
        double contentH = m.top() + m.bottom();
        for (int i = 0; i < total; ++i) {
            contentH += m_doc->pagePointSize(i).height() * z;
            if (i > 0) contentH += spacing;
        }
        const double contentW = maxW + m.left() + m.right();
        const double extraX = std::max(0.0, (vp.width() - contentW) / 2.0);
        const double extraY = std::max(0.0, (vp.height() - contentH) / 2.0);
        return QPointF(
            extraX + m.left() + (maxW - pw) / 2.0
                - m_view->horizontalScrollBar()->value(),
            extraY + y - m_view->verticalScrollBar()->value());
    };
    overlay->setDocumentToView([this, pageOriginInView](QPointF p, int page) {
        if (!m_view) return p;
        const double z = m_view->zoomFactor();
        const QPointF origin = pageOriginInView(page);
        return QPointF(origin.x() + p.x() * z, origin.y() + p.y() * z);
    });
    overlay->setViewToDocument([this, pageOriginInView](QPointF p, int page) {
        if (!m_view) return p;
        const double z = m_view->zoomFactor();
        if (z <= 0.0) return p;
        const QPointF origin = pageOriginInView(page);
        return QPointF((p.x() - origin.x()) / z, (p.y() - origin.y()) / z);
    });
    overlay->setPageAtViewPoint([this, pageOriginInView](QPointF viewPt) -> int {
        if (!m_view || !m_doc) return -1;
        const double z = m_view->zoomFactor();
        const int total = m_doc->pageCount();
        for (int i = 0; i < total; ++i) {
            const QPointF origin = pageOriginInView(i);
            const QSizeF pt = m_doc->pagePointSize(i);
            const QRectF rect(origin.x(), origin.y(),
                              pt.width() * z, pt.height() * z);
            if (rect.contains(viewPt)) return i;
        }
        return m_view->pageNavigator()->currentPage();
    });
    overlay->setSourceSampler(
        [this](QRectF docRect, QSize outPx, int page) -> QImage {
            if (!m_doc || page < 0 || docRect.isEmpty()) return {};
            const QSizeF pagePts = m_doc->pagePointSize(page);
            if (pagePts.isEmpty()) return {};
            const double sx = outPx.width() / docRect.width();
            const double sy = outPx.height() / docRect.height();
            const QSize fullPx(
                std::max(1, static_cast<int>(pagePts.width() * sx)),
                std::max(1, static_cast<int>(pagePts.height() * sy)));
            QPdfDocumentRenderOptions opts;
            opts.setScaledSize(fullPx);
            opts.setScaledClipRect(QRect(
                static_cast<int>(docRect.x() * sx),
                static_cast<int>(docRect.y() * sy),
                outPx.width(), outPx.height()));
            return m_doc->render(page, outPx, opts);
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

    // --- Form overlay (Phase 5) ---
    auto* formOverlay = new FormOverlay(view->viewport());
    formOverlay->setDocumentToView([this, pageOriginInView](QPointF p, int page) {
        if (!m_view) return p;
        const double z = m_view->zoomFactor();
        const QPointF origin = pageOriginInView(page);
        return QPointF(origin.x() + p.x() * z, origin.y() + p.y() * z);
    });
    formOverlay->setPageSize([this](int page) -> QSizeF {
        if (!m_doc || page < 0) return {};
        return m_doc->pagePointSize(page);
    });
    if (m_editor && m_editor->isValid()) {
        formOverlay->setFields(m_editor->readFormFields());
    }
    formOverlay->setGeometry(view->viewport()->rect());
    formOverlay->hide();   // shown by MainWindow when form-filling is toggled on
    m_formOverlay = formOverlay;

    // Relayout form widgets on scroll / zoom / resize.
    QObject::connect(view->verticalScrollBar(), &QScrollBar::valueChanged,
                     formOverlay, &FormOverlay::relayout);
    QObject::connect(view->horizontalScrollBar(), &QScrollBar::valueChanged,
                     formOverlay, &FormOverlay::relayout);
    QObject::connect(view, &QPdfView::zoomFactorChanged,
                     formOverlay, &FormOverlay::relayout);
    // When the user edits a widget, write the value back to the editor.
    QObject::connect(formOverlay, &FormOverlay::fieldValueChanged,
                     view, [this](int id, const QString& value) {
                         setFormFieldValue(id, value);
                     });

    return view;
}

void PdfDocument::setAnnotationTool(AnnotationTool tool) {
    if (m_overlay) m_overlay->setActiveTool(tool);
}

void PdfDocument::setAnnotationStyle(const AnnotationStyle& style) {
    if (m_overlay) m_overlay->setStyle(style);
}

void PdfDocument::setPendingAnnotationText(const QString& text) {
    if (m_overlay) m_overlay->setPendingTextPreset(text);
}

void PdfDocument::setPendingSignaturePath(const QString& path) {
    if (m_overlay) m_overlay->setPendingSignaturePath(path);
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
    QImage rendered = m_doc->render(pageIndex, QSize(w, h));
    if (rendered.isNull()) return rendered;
    // Many PDFs draw their content (text, vector ink) with no
    // explicit page background, leaving the rendered QImage with
    // transparent regions where paper would be. In dark mode the
    // sidebar's dock background shows through and the page reads
    // as floating black text on dark grey — unrecognisable. Force
    // an opaque white backdrop. PDFs that DO paint a background
    // colour just paint over it, no harm done.
    QImage canvas(rendered.size(), QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::white);
    QPainter painter(&canvas);
    painter.drawImage(0, 0, rendered);
    painter.end();
    return canvas;
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
        // setSearchString dispatches the actual search to a worker
        // thread — rowCount() is still 0 when this function returns.
        // Without this hook, the synchronous
        // setCurrentSearchResultIndex call below runs before any
        // matches exist, so the view never highlights the first hit
        // even when the model eventually populates. That's the
        // "Find found nothing" bug on OCR'd PDFs.
        //
        // Using the search model as the context so the lambda is
        // torn down automatically with it. PdfDocument itself isn't a
        // QObject so we can't bind to a member slot directly.
        QObject::connect(
            m_searchModel.get(), &QAbstractItemModel::rowsInserted,
            m_searchModel.get(),
            [this](const QModelIndex&, int, int) { onSearchResultsPopulated(); });
    }
    m_searchModel->setSearchString(query);
    m_currentResult = query.isEmpty() ? -1 : 0;
    if (m_view) {
        m_view->setSearchModel(m_searchModel.get());
        // Clear the view's current index so a late rowsInserted from
        // the *previous* query can't be mistaken for in-flight user
        // navigation by the onSearchResultsPopulated guard.
        m_view->setCurrentSearchResultIndex(-1);
        // Best-effort synchronous highlight for the cached-results
        // case. The async rowsInserted signal handles the common
        // "search still running" path.
        if (m_currentResult >= 0 && m_searchModel->rowCount({}) > 0) {
            m_view->setCurrentSearchResultIndex(m_currentResult);
        }
    }
}

void PdfDocument::onSearchResultsPopulated() {
    if (!m_view || !m_searchModel) return;
    if (m_currentResult < 0) return;
    if (m_searchModel->rowCount({}) <= 0) return;
    // Don't stomp on user navigation: if findNext/findPrevious bumped
    // the index while the search was still populating, leave it alone.
    if (m_view->currentSearchResultIndex() >= 0) return;
    m_view->setCurrentSearchResultIndex(m_currentResult);
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
    auto cmd = std::make_unique<RotatePageCommand>(pageIndex, degreesClockwise);
    if (!cmd->apply(*m_editor)) return;
    m_pdfUndoStack.push_back(std::move(cmd));
    m_pdfRedoStack.clear();
    m_lastUndoSource = UndoSource::PdfCommand;
    if (reloadViewerFromEditor()) {
        m_dirty = true;
    }
}

bool PdfDocument::canUndo() const {
    return m_annotations.canUndo() || !m_pdfUndoStack.empty();
}

bool PdfDocument::canRedo() const {
    return m_annotations.canRedo() || !m_pdfRedoStack.empty();
}

void PdfDocument::undo() {
    // Two parallel stacks; prefer the last-touched one so a
    // user's most recent action is undone first. A small
    // approximation of chronological undo until we unify the
    // logs (TODO: PdfCommand + AnnotationStore should share one
    // chronological list so multi-action undo always pops the
    // most recent thing the user did).
    if (m_lastUndoSource == UndoSource::PdfCommand &&
        !m_pdfUndoStack.empty()) {
        auto cmd = std::move(m_pdfUndoStack.back());
        m_pdfUndoStack.pop_back();
        cmd->revert(*m_editor);
        m_pdfRedoStack.push_back(std::move(cmd));
        reloadViewerFromEditor();
        m_lastUndoSource = m_pdfUndoStack.empty()
            ? (m_annotations.canUndo() ? UndoSource::Annotation
                                       : UndoSource::None)
            : UndoSource::PdfCommand;
        return;
    }
    if (m_annotations.canUndo()) {
        m_annotations.undo();
        m_lastUndoSource = m_annotations.canUndo()
            ? UndoSource::Annotation
            : (m_pdfUndoStack.empty() ? UndoSource::None
                                      : UndoSource::PdfCommand);
        return;
    }
    if (!m_pdfUndoStack.empty()) {
        // Fall-through case: lastSource was Annotation but the
        // annotation log is now exhausted.
        auto cmd = std::move(m_pdfUndoStack.back());
        m_pdfUndoStack.pop_back();
        cmd->revert(*m_editor);
        m_pdfRedoStack.push_back(std::move(cmd));
        reloadViewerFromEditor();
        m_lastUndoSource = m_pdfUndoStack.empty()
            ? UndoSource::None : UndoSource::PdfCommand;
    }
}

void PdfDocument::redo() {
    // Symmetric to undo. We don't track which stack got the most
    // recent redo distinctly; if the user is redoing they almost
    // always want the inverse of their most recent undo, and the
    // last-source heuristic from undo() is the closest signal.
    if (m_lastUndoSource == UndoSource::PdfCommand &&
        !m_pdfRedoStack.empty()) {
        auto cmd = std::move(m_pdfRedoStack.back());
        m_pdfRedoStack.pop_back();
        cmd->apply(*m_editor);
        m_pdfUndoStack.push_back(std::move(cmd));
        reloadViewerFromEditor();
        m_lastUndoSource = UndoSource::PdfCommand;
        return;
    }
    if (m_annotations.canRedo()) {
        m_annotations.redo();
        m_lastUndoSource = UndoSource::Annotation;
        return;
    }
    if (!m_pdfRedoStack.empty()) {
        auto cmd = std::move(m_pdfRedoStack.back());
        m_pdfRedoStack.pop_back();
        cmd->apply(*m_editor);
        m_pdfUndoStack.push_back(std::move(cmd));
        reloadViewerFromEditor();
        m_lastUndoSource = UndoSource::PdfCommand;
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
    auto ctx = saveBeginQpdfPhase(newPath);
    if (!ctx) return false;
    return saveCommitOnUi(*ctx);
}

std::optional<PdfDocument::SaveContext>
PdfDocument::saveBeginQpdfPhase(const QString& newPath) {
    if (!m_valid || !m_editor || !m_editor->isValid()) {
        return std::nullopt;
    }
    const QString targetPath = newPath.isEmpty() ? m_path : newPath;
    if (targetPath.isEmpty()) {
        return std::nullopt;
    }

    // Order matters: apply redactions first so their rasterised page
    // image replaces the old content stream before anything else runs.
    // Then flatten signatures so they survive as page content when the
    // file is re-read (readAnnotations does not reconstruct image
    // stamps). Finally, write every other annotation as /Annot.
    if (!m_editor->applyRedactions(m_annotations.annotations())) {
        return std::nullopt;
    }
    if (!m_editor->flattenSignatures(m_annotations.annotations())) {
        return std::nullopt;
    }
    if (!m_editor->writeAnnotations(m_annotations.annotations())) {
        return std::nullopt;
    }

    SaveContext ctx;
    ctx.targetPath = targetPath;
    ctx.sameFile = !m_path.isEmpty() &&
        QFileInfo(targetPath).canonicalFilePath()
            == QFileInfo(m_path).canonicalFilePath();

    if (ctx.sameFile) {
        // Stage to a temp file so a partial write doesn't clobber the
        // original. The UI-phase rename is atomic.
        auto temp = std::make_unique<QTemporaryFile>(
            QDir::tempPath() + QStringLiteral("/trailer-save-XXXXXX.pdf"));
        temp->setAutoRemove(false);  // we hand the file to the UI phase
        if (!temp->open()) {
            return std::nullopt;
        }
        ctx.writePath = temp->fileName();
        temp->close();
        if (!m_editor->save(ctx.writePath)) {
            QFile::remove(ctx.writePath);
            return std::nullopt;
        }
    } else {
        ctx.writePath = targetPath;
        if (!m_editor->save(ctx.writePath)) {
            return std::nullopt;
        }
    }
    return ctx;
}

bool PdfDocument::saveCommitOnUi(const SaveContext& ctx) {
    if (ctx.sameFile) {
        // Tear down our QPdfDocument's open handle so we can rename
        // over the file on Windows (Linux/macOS don't strictly need
        // this but it matches behaviour).
        m_doc->close();
        if (QFile::exists(ctx.targetPath) && !QFile::remove(ctx.targetPath)) {
            // Restore the original handle and bail; the staged temp
            // is leaked on disk but the user's file is untouched.
            m_doc->load(m_path);
            return false;
        }
        if (!QFile::rename(ctx.writePath, ctx.targetPath)) {
            m_doc->load(m_path);
            return false;
        }
    }

    m_path = ctx.targetPath;
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
    for (Annotation& a : m_editor->readAnnotations()) {
        m_annotations.add(std::move(a));
    }
    m_annotations.clearHistory();
    m_annotationsModified = false;
    return true;
}

std::vector<FormField> PdfDocument::formFields() const {
    if (!m_valid || !m_editor || !m_editor->isValid()) return {};
    return m_editor->readFormFields();
}

bool PdfDocument::setFormFieldValue(int id, const QString& value) {
    if (!m_valid || !m_editor || !m_editor->isValid()) return false;
    const bool ok = m_editor->setFormFieldValue(id, value);
    if (ok) m_dirty = true;
    return ok;
}

void PdfDocument::setFormFillingActive(bool active) {
    if (m_formOverlay) {
        if (active) {
            // Refresh fields in case the document changed since
            // the overlay was last populated.
            if (m_editor && m_editor->isValid()) {
                m_formOverlay->setFields(m_editor->readFormFields());
            }
            m_formOverlay->setGeometry(
                m_view ? m_view->viewport()->rect() : QRect{});
            m_formOverlay->show();
        } else {
            m_formOverlay->hide();
        }
    }
}

void PdfDocument::refreshFormView() {
    // Re-push field values into whichever widgets the overlay has
    // already built. Called after bulk writes (AutoFill) so the user
    // sees the new values immediately. Does not change the overlay's
    // visibility — if form-filling is off the refresh is a no-op until
    // the user toggles it on.
    if (!m_formOverlay || !m_editor || !m_editor->isValid()) return;
    m_formOverlay->setFields(m_editor->readFormFields());
}

bool PdfDocument::exportWithPassword(const QString& destPath,
                                     const QString& password) {
    if (!m_valid || !m_editor || !m_editor->isValid()) return false;
    if (destPath.isEmpty()) return false;
    // Write annotations into the editor's QPDF graph (same as save),
    // then serialize to `destPath` with AES-256 encryption. We write to
    // a separate destination only — never overwrite the source file —
    // so the in-memory state remains unencrypted and further edits keep
    // working normally.
    if (!m_editor->applyRedactions(m_annotations.annotations())) return false;
    if (!m_editor->flattenSignatures(m_annotations.annotations())) return false;
    if (!m_editor->writeAnnotations(m_annotations.annotations())) return false;
    EncryptionOptions enc;
    enc.userPassword = password;
    return m_editor->save(destPath, enc);
}

bool PdfDocument::reduceFileSize(const QString& destPath) {
    if (!m_valid || !m_editor || !m_editor->isValid()) return false;
    if (destPath.isEmpty()) return false;
    // Flush pending annotations first so the reduced output reflects
    // everything the user sees on screen. Linearization + object-
    // stream regeneration then re-packs the document.
    if (!m_editor->applyRedactions(m_annotations.annotations())) return false;
    if (!m_editor->flattenSignatures(m_annotations.annotations())) return false;
    if (!m_editor->writeAnnotations(m_annotations.annotations())) return false;
    return m_editor->saveReduced(destPath);
}

QStringList PdfAdapter::mimeTypes() const {
    return {QStringLiteral("application/pdf")};
}

QStringList PdfAdapter::extensions() const {
    return {QStringLiteral("pdf")};
}

namespace {

// Default prompt. Pops a modal QInputDialog on the active window with
// the password echo hidden. Returns nullopt if the user cancels or if
// there's no window to parent to (e.g. offscreen UAT without an
// installed test shim — we refuse to spin a dialog into the void).
std::optional<QString> defaultPasswordPrompt(const QString& path, int attempt) {
    QWidget* parent = QApplication::activeWindow();
    if (!parent) return std::nullopt;
    const int maxAttempts = 3;
    const QString title = attempt == 0
        ? QObject::tr("Password required")
        : QObject::tr("Password required (%1 attempts left)")
              .arg(maxAttempts - attempt);
    const QString prompt = QObject::tr(
        "“%1” is password-protected. Enter the password to open it.")
        .arg(QFileInfo(path).fileName());
    bool ok = false;
    const QString pw = QInputDialog::getText(
        parent, title, prompt, QLineEdit::Password, QString(), &ok);
    if (!ok) return std::nullopt;
    return pw;
}

PdfAdapter::PasswordPrompt& activePasswordPrompt() {
    static PdfAdapter::PasswordPrompt prompt = defaultPasswordPrompt;
    return prompt;
}

}  // namespace

void PdfAdapter::setPasswordPrompt(PasswordPrompt prompt) {
    activePasswordPrompt() = prompt ? std::move(prompt) : defaultPasswordPrompt;
}

PdfAdapter::PasswordPrompt PdfAdapter::passwordPrompt() {
    return activePasswordPrompt();
}

std::unique_ptr<IDocument> PdfAdapter::open(const QString& path) {
    auto doc = std::make_unique<PdfDocument>(path);

    // Password-gated PDF: prompt up to three times. Each iteration
    // asks the currently-installed PasswordPrompt hook; a nullopt
    // response ends the loop and leaves the document in its locked
    // state (createView falls back to a "Could not open" label).
    const int maxAttempts = 3;
    auto& prompt = activePasswordPrompt();
    for (int attempt = 0; attempt < maxAttempts && doc->needsPassword(); ++attempt) {
        std::optional<QString> pw = prompt(path, attempt);
        if (!pw) break;
        doc->unlock(*pw);
    }
    return doc;
}

}  // namespace trailer
