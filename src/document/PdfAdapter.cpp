#include "PdfAdapter.h"

#include "ui/AnnotationOverlay.h"
#include "ui/FormOverlay.h"
#include "ui/SelectableTextLayer.h"
#include "util/TempPath.h"

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
#include <QIdentityProxyModel>
#include <QPdfBookmarkModel>
#include <QPdfLink>
#include <QPdfSearchModel>
#include <QPdfView>
#include <QPrintDialog>
#include <QPrinter>
#include <QScrollBar>
#include <QSizeF>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

namespace trailer {

namespace {
constexpr double kZoomStep = 1.1;
constexpr double kZoomMin = 0.10;
constexpr double kZoomMax = 16.0;

// Bridge proxy for QPdfBookmarkModel: a vanilla QTreeView fetches
// row text via Qt::DisplayRole, but QPdfBookmarkModel exposes its
// title under the model's `Title` role (numerically Qt::UserRole).
// Without this remap the tree shows a column of empty rows.
class OutlineProxyModel : public QIdentityProxyModel {
  public:
    explicit OutlineProxyModel(QObject *parent = nullptr) : QIdentityProxyModel(parent) {}

    QVariant data(const QModelIndex &proxyIndex, int role) const override {
        if (role == Qt::DisplayRole) {
            return QIdentityProxyModel::data(proxyIndex,
                                             static_cast<int>(QPdfBookmarkModel::Role::Title));
        }
        return QIdentityProxyModel::data(proxyIndex, role);
    }
};

class NavigablePdfView : public QPdfView {
  public:
    explicit NavigablePdfView(QWidget *parent) : QPdfView(parent) {}

  protected:
    void keyPressEvent(QKeyEvent *e) override {
        if (pageMode() == QPdfView::PageMode::SinglePage) {
            const int key = e->key();
            QScrollBar *vbar = verticalScrollBar();
            const bool atBottom = vbar->value() >= vbar->maximum();
            const bool atTop = vbar->value() <= vbar->minimum();
            auto *nav = pageNavigator();
            const int current = nav->currentPage();
            const int last = document() ? document()->pageCount() - 1 : 0;
            // In fit modes the entire page is meant to fit the viewport,
            // so Down/Space should step to the next page outright. The
            // "scroll until you hit the bottom, then step" behaviour is
            // correct for Custom zoom (the user might be reading a
            // zoomed-in page) but wrong for fit modes — with slightly
            // varying page sizes the user otherwise sees a small scroll
            // before the step.
            const bool inFitMode = zoomMode() == QPdfView::ZoomMode::FitInView ||
                                   zoomMode() == QPdfView::ZoomMode::FitToWidth;
            const bool stepDownReady = inFitMode || atBottom;
            const bool stepUpReady = inFitMode || atTop;
            if ((key == Qt::Key_Down || key == Qt::Key_PageDown || key == Qt::Key_Space) &&
                stepDownReady && current < last) {
                // Capture the active fit mode before jumping so we can
                // re-apply it after the page change — passing the
                // current zoomFactor instead would freeze the view at
                // whatever scale the previous page chose, which is
                // wrong when page sizes vary.
                const QPdfView::ZoomMode mode = zoomMode();
                nav->jump(current + 1, QPointF{}, zoomFactor());
                if (inFitMode) {
                    setZoomMode(mode);
                }
                verticalScrollBar()->setValue(verticalScrollBar()->minimum());
                e->accept();
                return;
            }
            if ((key == Qt::Key_Up || key == Qt::Key_PageUp) && stepUpReady && current > 0) {
                const QPdfView::ZoomMode mode = zoomMode();
                nav->jump(current - 1, QPointF{}, zoomFactor());
                if (inFitMode) {
                    setZoomMode(mode);
                }
                verticalScrollBar()->setValue(verticalScrollBar()->maximum());
                e->accept();
                return;
            }
        }
        QPdfView::keyPressEvent(e);
    }
};
} // namespace

PdfDocument::PdfDocument(QString path)
    : m_path(std::move(path)), m_doc(std::make_unique<QPdfDocument>()),
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
        for (Annotation &a : m_editor->readAnnotations()) {
            m_annotations.add(std::move(a));
        }
        m_annotations.clearHistory();
        QObject::connect(&m_annotations, &AnnotationStore::changed, m_doc.get(), [this]() {
            m_annotationsModified = true;
            m_lastUndoSource = UndoSource::Annotation;
        });
    }
}

bool PdfDocument::unlock(const QString &password) {
    if (m_valid)
        return true;
    if (!m_needsPassword)
        return false;

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
        for (Annotation &a : m_editor->readAnnotations()) {
            m_annotations.add(std::move(a));
        }
        m_annotations.clearHistory();
        QObject::connect(&m_annotations, &AnnotationStore::changed, m_doc.get(), [this]() {
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

QWidget *PdfDocument::createView(QWidget *parent) {
    if (!m_valid) {
        auto *container = new QWidget(parent);
        auto *layout = new QVBoxLayout(container);
        auto *label = new QLabel(QObject::tr("Could not open PDF:\n%1").arg(m_path), container);
        label->setAlignment(Qt::AlignCenter);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(label);
        return container;
    }

    auto *view = new NavigablePdfView(parent);
    view->setDocument(m_doc.get());
    view->setZoomMode(QPdfView::ZoomMode::Custom);
    view->setZoomFactor(1.0);
    // Fit-to-content on first show. Defer to the event loop so the
    // viewport has its real size after the tab insert + layout pass.
    // Re-checks zero size and bails — a later resize will re-trigger
    // this via the standard QPdfView FitInView path if it stuck. The
    // small-doc upscale guard is the spec: docs that already fit at
    // 100% stay at 100% rather than blowing up to fill the window.
    QTimer::singleShot(0, view, [this, view]() { applyInitialFitZoom(view); });
    // QPdfView paints search matches using the palette's Highlight
    // role. Override to a translucent yellow so matches look like
    // a marker-pen highlighter instead of a system selection.
    // (Qt versions that ignore the role for PDF render fall back
    // gracefully — the change is harmless.)
    QPalette pdfPalette = view->palette();
    pdfPalette.setColor(QPalette::Highlight, QColor(255, 235, 50, 160));
    pdfPalette.setColor(QPalette::HighlightedText, Qt::black);
    view->setPalette(pdfPalette);
    m_view = view;
    if (m_searchModel) {
        view->setSearchModel(m_searchModel.get());
        if (m_currentResult >= 0) {
            view->setCurrentSearchResultIndex(m_currentResult);
        }
    }
    applyViewMode();

    auto *overlay = new AnnotationOverlay(view->viewport());
    overlay->setStore(&m_annotations);
    overlay->setPage(view->pageNavigator()->currentPage());
    auto pageOriginInView = [this](int page) -> QPointF {
        if (!m_view || !m_doc || page < 0)
            return {};
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
            if (page != cur)
                return QPointF(-1e9, -1e9);
            const double contentW = maxW + m.left() + m.right();
            const double contentH = m_doc->pagePointSize(page).height() * z + m.top() + m.bottom();
            const double extraX = std::max(0.0, (vp.width() - contentW) / 2.0);
            const double extraY = std::max(0.0, (vp.height() - contentH) / 2.0);
            return QPointF(extraX + m.left() + (maxW - pw) / 2.0 -
                               m_view->horizontalScrollBar()->value(),
                           extraY + m.top() - m_view->verticalScrollBar()->value());
        }

        double y = m.top();
        for (int i = 0; i < page; ++i) {
            y += m_doc->pagePointSize(i).height() * z + spacing;
        }
        double contentH = m.top() + m.bottom();
        for (int i = 0; i < total; ++i) {
            contentH += m_doc->pagePointSize(i).height() * z;
            if (i > 0)
                contentH += spacing;
        }
        const double contentW = maxW + m.left() + m.right();
        const double extraX = std::max(0.0, (vp.width() - contentW) / 2.0);
        const double extraY = std::max(0.0, (vp.height() - contentH) / 2.0);
        return QPointF(extraX + m.left() + (maxW - pw) / 2.0 -
                           m_view->horizontalScrollBar()->value(),
                       extraY + y - m_view->verticalScrollBar()->value());
    };
    overlay->setDocumentToView([this, pageOriginInView](QPointF p, int page) {
        if (!m_view)
            return p;
        const double z = m_view->zoomFactor();
        const QPointF origin = pageOriginInView(page);
        return QPointF(origin.x() + p.x() * z, origin.y() + p.y() * z);
    });
    overlay->setViewToDocument([this, pageOriginInView](QPointF p, int page) {
        if (!m_view)
            return p;
        const double z = m_view->zoomFactor();
        if (z <= 0.0)
            return p;
        const QPointF origin = pageOriginInView(page);
        return QPointF((p.x() - origin.x()) / z, (p.y() - origin.y()) / z);
    });
    overlay->setPageAtViewPoint([this, pageOriginInView](QPointF viewPt) -> int {
        if (!m_view || !m_doc)
            return -1;
        const double z = m_view->zoomFactor();
        const int total = m_doc->pageCount();
        for (int i = 0; i < total; ++i) {
            const QPointF origin = pageOriginInView(i);
            const QSizeF pt = m_doc->pagePointSize(i);
            const QRectF rect(origin.x(), origin.y(), pt.width() * z, pt.height() * z);
            if (rect.contains(viewPt))
                return i;
        }
        return m_view->pageNavigator()->currentPage();
    });
    overlay->setSourceSampler([this](QRectF docRect, QSize outPx, int page) -> QImage {
        if (!m_doc || page < 0 || docRect.isEmpty())
            return {};
        const QSizeF pagePts = m_doc->pagePointSize(page);
        if (pagePts.isEmpty())
            return {};
        const double sx = outPx.width() / docRect.width();
        const double sy = outPx.height() / docRect.height();
        const QSize fullPx(std::max(1, static_cast<int>(pagePts.width() * sx)),
                           std::max(1, static_cast<int>(pagePts.height() * sy)));
        QPdfDocumentRenderOptions opts;
        opts.setScaledSize(fullPx);
        opts.setScaledClipRect(QRect(static_cast<int>(docRect.x() * sx),
                                     static_cast<int>(docRect.y() * sy), outPx.width(),
                                     outPx.height()));
        return m_doc->render(page, outPx, opts);
    });
    overlay->setTextSelectionProvider(
        [this](QPointF startDoc, QPointF endDoc, int page) -> std::vector<QRectF> {
            if (!m_doc || page < 0)
                return {};
            const QPdfSelection sel = m_doc->getSelection(page, startDoc, endDoc);
            if (!sel.isValid())
                return {};
            std::vector<QRectF> out;
            for (const QPolygonF &poly : sel.bounds()) {
                out.push_back(poly.boundingRect());
            }
            return out;
        });
    overlay->setGeometry(view->viewport()->rect());
    overlay->show();
    m_overlay = overlay;

    QObject::connect(view->pageNavigator(), &QPdfPageNavigator::currentPageChanged, overlay,
                     [overlay](int page) {
                         if (overlay)
                             overlay->setPage(page);
                     });
    QObject::connect(view->verticalScrollBar(), &QScrollBar::valueChanged, overlay,
                     QOverload<>::of(&QWidget::update));
    QObject::connect(view->horizontalScrollBar(), &QScrollBar::valueChanged, overlay,
                     QOverload<>::of(&QWidget::update));
    QObject::connect(view, &QPdfView::zoomFactorChanged, overlay,
                     QOverload<>::of(&QWidget::update));
    view->viewport()->installEventFilter(overlay);

    // --- Selectable-text layer (Phase 6F / Workstream F) ---
    // Sits beneath the annotation overlay so user-drawn shapes paint
    // on top of any highlighted selection. Initially empty (no OCR
    // results); MainWindow's auto-OCR pump or the Recognize Text
    // dialog populates the store and the layer wakes up.
    auto *textLayer = new SelectableTextLayer(view->viewport());
    textLayer->setStore(&m_selectableText);
    textLayer->setDocToView([this, pageOriginInView](QPointF p, int page) {
        if (!m_view)
            return p;
        const double z = m_view->zoomFactor();
        const QPointF origin = pageOriginInView(page);
        return QPointF(origin.x() + p.x() * z, origin.y() + p.y() * z);
    });
    textLayer->setPageAtView([this, pageOriginInView](QPointF viewPt) -> int {
        if (!m_view || !m_doc)
            return -1;
        const double z = m_view->zoomFactor();
        const int total = m_doc->pageCount();
        for (int i = 0; i < total; ++i) {
            const QPointF origin = pageOriginInView(i);
            const QSizeF pt = m_doc->pagePointSize(i);
            const QRectF rect(origin.x(), origin.y(), pt.width() * z, pt.height() * z);
            if (rect.contains(viewPt))
                return i;
        }
        return m_view->pageNavigator()->currentPage();
    });
    textLayer->setCurrentPage(view->pageNavigator()->currentPage());
    textLayer->setGeometry(view->viewport()->rect());
    textLayer->lower(); // sit below annotation overlay in the z-order
    textLayer->show();
    m_textLayer = textLayer;

    QObject::connect(view->pageNavigator(), &QPdfPageNavigator::currentPageChanged, textLayer,
                     [textLayer](int page) {
                         if (textLayer)
                             textLayer->setCurrentPage(page);
                     });
    QObject::connect(view->verticalScrollBar(), &QScrollBar::valueChanged, textLayer,
                     QOverload<>::of(&QWidget::update));
    QObject::connect(view->horizontalScrollBar(), &QScrollBar::valueChanged, textLayer,
                     QOverload<>::of(&QWidget::update));
    QObject::connect(view, &QPdfView::zoomFactorChanged, textLayer,
                     QOverload<>::of(&QWidget::update));

    // --- Form overlay (Phase 5) ---
    auto *formOverlay = new FormOverlay(view->viewport());
    formOverlay->setDocumentToView([this, pageOriginInView](QPointF p, int page) {
        if (!m_view)
            return p;
        const double z = m_view->zoomFactor();
        const QPointF origin = pageOriginInView(page);
        return QPointF(origin.x() + p.x() * z, origin.y() + p.y() * z);
    });
    formOverlay->setPageSize([this](int page) -> QSizeF {
        if (!m_doc || page < 0)
            return {};
        return m_doc->pagePointSize(page);
    });
    if (m_editor && m_editor->isValid()) {
        formOverlay->setFields(m_editor->readFormFields());
    }
    formOverlay->setGeometry(view->viewport()->rect());
    formOverlay->hide(); // shown by MainWindow when form-filling is toggled on
    m_formOverlay = formOverlay;

    // Relayout form widgets on scroll / zoom / resize.
    QObject::connect(view->verticalScrollBar(), &QScrollBar::valueChanged, formOverlay,
                     &FormOverlay::relayout);
    QObject::connect(view->horizontalScrollBar(), &QScrollBar::valueChanged, formOverlay,
                     &FormOverlay::relayout);
    QObject::connect(view, &QPdfView::zoomFactorChanged, formOverlay, &FormOverlay::relayout);
    // When the user edits a widget, write the value back to the editor.
    QObject::connect(formOverlay, &FormOverlay::fieldValueChanged, view,
                     [this](int id, const QString &value) { setFormFieldValue(id, value); });

    return view;
}

void PdfDocument::setAnnotationTool(AnnotationTool tool) {
    if (m_overlay)
        m_overlay->setActiveTool(tool);
}

void PdfDocument::setAnnotationStyle(const AnnotationStyle &style) {
    if (m_overlay)
        m_overlay->setStyle(style);
}

void PdfDocument::setPendingAnnotationText(const QString &text) {
    if (m_overlay)
        m_overlay->setPendingTextPreset(text);
}

void PdfDocument::setPendingSignaturePath(const QString &path) {
    if (m_overlay)
        m_overlay->setPendingSignaturePath(path);
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
    QScrollBar *hbar = m_view->horizontalScrollBar();
    hbar->setValue((hbar->minimum() + hbar->maximum()) / 2);
}

void PdfDocument::zoomIn() {
    if (!m_view)
        return;
    applyZoomFactor(m_view->zoomFactor() * kZoomStep);
}

void PdfDocument::zoomOut() {
    if (!m_view)
        return;
    applyZoomFactor(m_view->zoomFactor() / kZoomStep);
}

void PdfDocument::zoomActual() {
    applyZoomFactor(1.0);
}

void PdfDocument::zoomFitWidth() {
    if (!m_view)
        return;
    m_view->setZoomMode(QPdfView::ZoomMode::FitToWidth);
}

void PdfDocument::zoomFitPage() {
    if (!m_view)
        return;
    m_view->setZoomMode(QPdfView::ZoomMode::FitInView);
}

QSize PdfDocument::contentSizeHint() const {
    if (!m_valid || !m_doc || m_doc->pageCount() <= 0)
        return {};
    const QSizeF pts = m_doc->pagePointSize(0);
    if (pts.isEmpty())
        return {};
    // QPdfView maps 1 PDF point to 1 logical pixel at zoom 1.0, so
    // the natural display size in CSS pixels is just the point size.
    return QSize(static_cast<int>(std::ceil(pts.width())),
                 static_cast<int>(std::ceil(pts.height())));
}

void PdfDocument::applyInitialFitZoom(QPdfView *view) {
    if (!view || !m_doc || m_doc->pageCount() <= 0)
        return;
    if (m_initialZoomApplied)
        return;
    const QSizeF pagePts = m_doc->pagePointSize(0);
    if (pagePts.isEmpty())
        return;
    const QSize vp = view->viewport()->size();
    if (vp.width() <= 0 || vp.height() <= 0) {
        // Layout hasn't settled — retry on the next tick. The retry
        // chain stops as soon as the viewport reports a real size or
        // the view is destroyed.
        QTimer::singleShot(0, view, [this, view]() { applyInitialFitZoom(view); });
        return;
    }
    m_initialZoomApplied = true;
    const QMargins m = view->documentMargins();
    const double availW = std::max(1, vp.width() - m.left() - m.right());
    const double availH = std::max(1, vp.height() - m.top() - m.bottom());
    const double scaleW = availW / pagePts.width();
    const double scaleH = availH / pagePts.height();
    const double fit = std::min(scaleW, scaleH);
    if (fit >= 1.0) {
        // Doc already fits at 100% — leave it at actual size rather
        // than upscaling. zoomFactor was already set to 1.0 above.
        return;
    }
    // Use FitInView so a later window resize re-fits without the user
    // having to hit ⌘0 again. zoomFitPage() picks the same mode.
    view->setZoomMode(QPdfView::ZoomMode::FitInView);
}

ZoomMode PdfDocument::zoomMode() const {
    if (!m_view)
        return ZoomMode::Custom;
    switch (m_view->zoomMode()) {
    case QPdfView::ZoomMode::FitInView:
        return ZoomMode::FitInView;
    case QPdfView::ZoomMode::FitToWidth:
        return ZoomMode::FitToWidth;
    case QPdfView::ZoomMode::Custom:
        break;
    }
    // QPdfView treats "actual size" as a custom zoom of 1.0. We report
    // it separately so the persistence layer can preserve the user's
    // intent (⌘0 vs an exact 100% custom factor) — they're identical
    // mechanically but the user thinks of them differently.
    if (qFuzzyCompare(m_view->zoomFactor(), 1.0))
        return ZoomMode::Actual;
    return ZoomMode::Custom;
}

double PdfDocument::zoomFactor() const {
    return m_view ? m_view->zoomFactor() : 1.0;
}

void PdfDocument::applyZoomState(ZoomMode mode, double factor) {
    if (!m_view)
        return;
    switch (mode) {
    case ZoomMode::FitInView:
        m_view->setZoomMode(QPdfView::ZoomMode::FitInView);
        return;
    case ZoomMode::FitToWidth:
        m_view->setZoomMode(QPdfView::ZoomMode::FitToWidth);
        return;
    case ZoomMode::Actual:
        applyZoomFactor(1.0);
        return;
    case ZoomMode::Custom:
        if (factor > 0.0)
            applyZoomFactor(factor);
        return;
    }
}

int PdfDocument::scrollY() const {
    if (!m_view)
        return 0;
    return m_view->verticalScrollBar()->value();
}

void PdfDocument::applyScrollY(int y) {
    if (!m_view)
        return;
    auto *bar = m_view->verticalScrollBar();
    if (!bar)
        return;
    // Clamp to the bar's range — a saved scroll position from a doc
    // that has since been edited (pages removed, zoom changed) may
    // exceed the new maximum. Falling back to the closest valid
    // value is friendlier than landing at 0.
    const int clamped = std::clamp(y, bar->minimum(), bar->maximum());
    bar->setValue(clamped);
}

QImage PdfDocument::renderPageForOcr(int pageIndex) const {
    if (!m_valid || !m_doc || pageIndex < 0 || pageIndex >= m_doc->pageCount()) {
        return {};
    }
    // PP-OCRv3 caps the long side at 960 px internally, but we want a
    // little extra so smaller scans render legible glyphs. A 144 DPI
    // raster of a US-letter page is ~1224×1584 — comfortably above the
    // detector's stride threshold and well below the 4× memory blow-up
    // a 300 DPI render would cost on long PDFs.
    constexpr double kDpi = 144.0;
    const QSizeF pagePts = m_doc->pagePointSize(pageIndex);
    if (pagePts.isEmpty())
        return {};
    const int w = std::max(1, static_cast<int>(pagePts.width() / 72.0 * kDpi));
    const int h = std::max(1, static_cast<int>(pagePts.height() / 72.0 * kDpi));
    QImage rendered = m_doc->render(pageIndex, QSize(w, h));
    if (rendered.isNull())
        return rendered;
    // Background-flatten so the OCR detector sees a white-paper
    // colour rather than transparent pixels (which the detector reads
    // as "outside the document").
    QImage canvas(rendered.size(), QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::white);
    QPainter painter(&canvas);
    painter.drawImage(0, 0, rendered);
    painter.end();
    return canvas;
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
    if (rendered.isNull())
        return rendered;
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
    if (!m_view)
        return 0;
    return m_view->pageNavigator()->currentPage();
}

void PdfDocument::goToPage(int pageIndex) {
    if (!m_view || pageIndex < 0 || pageIndex >= pageCount()) {
        return;
    }
    m_view->pageNavigator()->jump(pageIndex, QPointF{}, m_view->zoomFactor());
}

void PdfDocument::setSearchQuery(const QString &query) {
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
        QObject::connect(m_searchModel.get(), &QAbstractItemModel::rowsInserted,
                         m_searchModel.get(),
                         [this](const QModelIndex &, int, int) { onSearchResultsPopulated(); });
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
    // Push the (possibly empty) match list to the overlay so an
    // empty / cleared query removes stale yellow highlights from a
    // previous search.
    refreshSearchHighlights();
}

void PdfDocument::onSearchResultsPopulated() {
    if (!m_view || !m_searchModel)
        return;
    if (m_currentResult < 0)
        return;
    if (m_searchModel->rowCount({}) <= 0)
        return;
    // Don't stomp on user navigation: if findNext/findPrevious bumped
    // the index while the search was still populating, leave it alone.
    if (m_view->currentSearchResultIndex() >= 0) {
        refreshSearchHighlights();
        return;
    }
    m_view->setCurrentSearchResultIndex(m_currentResult);
    refreshSearchHighlights();
}

QAbstractItemModel *PdfDocument::outlineModel() {
    if (!m_valid || !m_doc)
        return nullptr;
    if (!m_bookmarkModel) {
        m_bookmarkModel = std::make_unique<QPdfBookmarkModel>();
        m_bookmarkModel->setDocument(m_doc.get());
    }
    if (!m_outlineProxy) {
        m_outlineProxy = std::make_unique<OutlineProxyModel>();
        m_outlineProxy->setSourceModel(m_bookmarkModel.get());
    }
    return m_outlineProxy.get();
}

bool PdfDocument::hasOutline() const {
    if (!m_valid || !m_doc)
        return false;
    // Construct the model lazily on the pre-check too so MainWindow
    // can drive the Sidebar picker's enabled-state without forcing a
    // separate tree walk.
    if (!m_bookmarkModel) {
        m_bookmarkModel = std::make_unique<QPdfBookmarkModel>();
        m_bookmarkModel->setDocument(m_doc.get());
    }
    return m_bookmarkModel->rowCount({}) > 0;
}

void PdfDocument::goToOutlineEntry(const QModelIndex &index) {
    if (!m_valid || !m_view || !index.isValid())
        return;
    if (!m_bookmarkModel)
        return;
    // The index may be either a proxy index (from the Sidebar's
    // QTreeView attached to outlineModel() ) or a source index. Read
    // the page role through the index itself — QIdentityProxyModel
    // passes non-DisplayRole queries straight through, so either
    // works.
    const QVariant pageVar = index.data(static_cast<int>(QPdfBookmarkModel::Role::Page));
    bool ok = false;
    const int page = pageVar.toInt(&ok);
    if (!ok || page < 0 || page >= pageCount())
        return;
    goToPage(page);
}

void PdfDocument::refreshSearchHighlights() {
    if (!m_overlay)
        return;
    std::vector<AnnotationOverlay::SearchHighlight> highlights;
    if (m_searchModel) {
        const int n = m_searchModel->rowCount({});
        for (int i = 0; i < n; ++i) {
            const QPdfLink link = m_searchModel->resultAtIndex(i);
            if (!link.isValid())
                continue;
            const int page = link.page();
            const bool isCurrent = (i == m_currentResult);
            for (const QRectF &r : link.rectangles()) {
                highlights.push_back({page, r, isCurrent});
            }
        }
    }
    m_overlay->setSearchHighlights(std::move(highlights));
}

void PdfDocument::findNext() {
    if (!m_view || !m_searchModel)
        return;
    const int count = m_searchModel->rowCount({});
    if (count <= 0)
        return;
    m_currentResult = (m_currentResult + 1) % count;
    m_view->setCurrentSearchResultIndex(m_currentResult);
    refreshSearchHighlights();
}

void PdfDocument::findPrevious() {
    if (!m_view || !m_searchModel)
        return;
    const int count = m_searchModel->rowCount({});
    if (count <= 0)
        return;
    m_currentResult = (m_currentResult - 1 + count) % count;
    m_view->setCurrentSearchResultIndex(m_currentResult);
    refreshSearchHighlights();
}

void PdfDocument::clearSearch() {
    if (m_searchModel) {
        m_searchModel->setSearchString(QString());
    }
    m_currentResult = -1;
    if (m_view) {
        m_view->setCurrentSearchResultIndex(-1);
    }
    refreshSearchHighlights();
}

int PdfDocument::searchMatchCount() const {
    if (!m_searchModel)
        return 0;
    return m_searchModel->rowCount({});
}

int PdfDocument::currentSearchMatchIndex() const {
    if (!m_searchModel)
        return -1;
    if (m_currentResult < 0)
        return -1;
    if (m_currentResult >= m_searchModel->rowCount({}))
        return -1;
    // 1-based for display; the convention every "X of Y" UI uses.
    return m_currentResult + 1;
}

std::vector<int> PdfDocument::pagesWithSearchMatches() const {
    std::vector<int> out;
    if (!m_searchModel)
        return out;
    const int total = m_searchModel->rowCount({});
    for (int i = 0; i < total; ++i) {
        // QPdfSearchModel exposes the page index via the
        // PageIndexRole (Qt::UserRole + 1). We dedupe inline by
        // remembering the last page seen — search results come
        // back ordered by page so we don't need a set.
        const QModelIndex idx = m_searchModel->index(i, 0);
        const int page = idx.data(static_cast<int>(QPdfSearchModel::Role::Page)).toInt();
        if (out.empty() || out.back() != page) {
            out.push_back(page);
        }
    }
    return out;
}

void PdfDocument::print(QWidget *dialogParent) {
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
        if (pagePts.isEmpty())
            continue;

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
    auto preview =
        std::make_unique<ScopedTempFile>(QStringLiteral("trailer-preview-XXXXXX.pdf"));
    if (!preview->isValid()) {
        return false;
    }
    const QString previewPath = preview->path();
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
    // Any reload is the result of a page-level mutation (rotate,
    // delete, move, crop, insert). The OCR cache is keyed on the raw
    // page raster — clear it wholesale rather than try to be clever
    // about which pages survived. The auto-OCR pump will re-enqueue
    // work for the visible page after the reload settles.
    m_selectableText.clear();
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
    if (!cmd->apply(*m_editor))
        return;
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
    if (m_lastUndoSource == UndoSource::PdfCommand && !m_pdfUndoStack.empty()) {
        auto cmd = std::move(m_pdfUndoStack.back());
        m_pdfUndoStack.pop_back();
        cmd->revert(*m_editor);
        m_pdfRedoStack.push_back(std::move(cmd));
        reloadViewerFromEditor();
        m_lastUndoSource =
            m_pdfUndoStack.empty()
                ? (m_annotations.canUndo() ? UndoSource::Annotation : UndoSource::None)
                : UndoSource::PdfCommand;
        return;
    }
    if (m_annotations.canUndo()) {
        m_annotations.undo();
        m_lastUndoSource =
            m_annotations.canUndo()
                ? UndoSource::Annotation
                : (m_pdfUndoStack.empty() ? UndoSource::None : UndoSource::PdfCommand);
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
        m_lastUndoSource = m_pdfUndoStack.empty() ? UndoSource::None : UndoSource::PdfCommand;
    }
}

void PdfDocument::redo() {
    // Symmetric to undo. We don't track which stack got the most
    // recent redo distinctly; if the user is redoing they almost
    // always want the inverse of their most recent undo, and the
    // last-source heuristic from undo() is the closest signal.
    if (m_lastUndoSource == UndoSource::PdfCommand && !m_pdfRedoStack.empty()) {
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

void PdfDocument::deletePages(const std::vector<int> &pageIndices) {
    if (!m_valid || !m_editor || !m_editor->isValid() || pageIndices.empty()) {
        return;
    }
    const int before = m_editor->pageCount();
    if (static_cast<int>(pageIndices.size()) >= before) {
        return; // refuse to delete every page
    }
    m_editor->deletePages(pageIndices);
    if (reloadViewerFromEditor()) {
        m_dirty = true;
    }
}

bool PdfDocument::extractPages(const std::vector<int> &pageIndices, const QString &destPath) const {
    if (!m_valid || !m_editor || !m_editor->isValid())
        return false;
    return m_editor->extractPages(pageIndices, destPath);
}

bool PdfDocument::cropPage(int pageIndex, double leftPts, double topPts, double rightPts,
                           double bottomPts) {
    if (!m_valid || !m_editor || !m_editor->isValid())
        return false;
    if (!m_editor->cropPage(pageIndex, leftPts, topPts, rightPts, bottomPts)) {
        return false;
    }
    if (reloadViewerFromEditor()) {
        m_dirty = true;
        return true;
    }
    return false;
}

bool PdfDocument::cropPages(const std::vector<int> &pageIndices, double leftPts, double topPts,
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
    if (!any)
        return false;
    if (reloadViewerFromEditor()) {
        m_dirty = true;
        return true;
    }
    return false;
}

bool PdfDocument::insertPagesFrom(const QString &sourcePath, int insertAtIndex) {
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

bool PdfDocument::save(const QString &newPath) {
    auto ctx = saveBeginQpdfPhase(newPath);
    if (!ctx)
        return false;
    return saveCommitOnUi(*ctx);
}

std::optional<PdfDocument::SaveContext> PdfDocument::saveBeginQpdfPhase(const QString &newPath) {
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
    ctx.sameFile = !m_path.isEmpty() && QFileInfo(targetPath).canonicalFilePath() ==
                                            QFileInfo(m_path).canonicalFilePath();

    if (ctx.sameFile) {
        // Stage to a temp file so a partial write doesn't clobber the
        // original. The UI-phase rename is atomic. makeUniqueTempPath
        // (not QTemporaryFile) so qpdf can open the path for writing
        // on Windows — see util/TempPath.h for the rationale.
        ctx.writePath = makeUniqueTempPath(QStringLiteral("trailer-save-XXXXXX.pdf"));
        if (ctx.writePath.isEmpty()) {
            return std::nullopt;
        }
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

bool PdfDocument::saveCommitOnUi(const SaveContext &ctx) {
    if (ctx.sameFile) {
        // Tear down our QPdfDocument's open handle so we can rename
        // over the file on Windows (Linux/macOS don't strictly need
        // this but it matches behaviour).
        m_doc->close();
        // Same story for the qpdf editor: QPDF::processFile leaves
        // m_path open for lazy stream reads, and Windows refuses
        // DeleteFile on a handle opened without FILE_SHARE_DELETE.
        // We rebuild a fresh editor from the post-rename file at the
        // end of this method, so dropping the old one now costs
        // nothing. (Linux/macOS would tolerate the open handle; this
        // is purely a Windows shield.)
        m_editor.reset();
        auto restoreOnFailure = [this]() {
            m_editor = std::make_unique<PdfEditor>();
            m_editor->load(m_path);
            m_doc->load(m_path);
        };
        if (QFile::exists(ctx.targetPath) && !QFile::remove(ctx.targetPath)) {
            // Restore the original handle and bail; the staged temp
            // is leaked on disk but the user's file is untouched.
            restoreOnFailure();
            return false;
        }
        if (!QFile::rename(ctx.writePath, ctx.targetPath)) {
            restoreOnFailure();
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
    for (Annotation &a : m_editor->readAnnotations()) {
        m_annotations.add(std::move(a));
    }
    m_annotations.clearHistory();
    m_annotationsModified = false;
    return true;
}

std::vector<FormField> PdfDocument::formFields() const {
    if (!m_valid || !m_editor || !m_editor->isValid())
        return {};
    return m_editor->readFormFields();
}

bool PdfDocument::setFormFieldValue(int id, const QString &value) {
    if (!m_valid || !m_editor || !m_editor->isValid())
        return false;
    const bool ok = m_editor->setFormFieldValue(id, value);
    if (ok)
        m_dirty = true;
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
            m_formOverlay->setGeometry(m_view ? m_view->viewport()->rect() : QRect{});
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
    if (!m_formOverlay || !m_editor || !m_editor->isValid())
        return;
    m_formOverlay->setFields(m_editor->readFormFields());
}

bool PdfDocument::exportWithPassword(const QString &destPath, const QString &password) {
    if (!m_valid || !m_editor || !m_editor->isValid())
        return false;
    if (destPath.isEmpty())
        return false;
    // Write annotations into the editor's QPDF graph (same as save),
    // then serialize to `destPath` with AES-256 encryption. We write to
    // a separate destination only — never overwrite the source file —
    // so the in-memory state remains unencrypted and further edits keep
    // working normally.
    if (!m_editor->applyRedactions(m_annotations.annotations()))
        return false;
    if (!m_editor->flattenSignatures(m_annotations.annotations()))
        return false;
    if (!m_editor->writeAnnotations(m_annotations.annotations()))
        return false;
    EncryptionOptions enc;
    enc.userPassword = password;
    return m_editor->save(destPath, enc);
}

bool PdfDocument::reduceFileSize(const QString &destPath) {
    if (!m_valid || !m_editor || !m_editor->isValid())
        return false;
    if (destPath.isEmpty())
        return false;
    // Flush pending annotations first so the reduced output reflects
    // everything the user sees on screen. Linearization + object-
    // stream regeneration then re-packs the document.
    if (!m_editor->applyRedactions(m_annotations.annotations()))
        return false;
    if (!m_editor->flattenSignatures(m_annotations.annotations()))
        return false;
    if (!m_editor->writeAnnotations(m_annotations.annotations()))
        return false;
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
std::optional<QString> defaultPasswordPrompt(const QString &path, int attempt) {
    QWidget *parent = QApplication::activeWindow();
    if (!parent)
        return std::nullopt;
    const int maxAttempts = 3;
    const QString title =
        attempt == 0
            ? QObject::tr("Password required")
            : QObject::tr("Password required (%1 attempts left)").arg(maxAttempts - attempt);
    const QString prompt = QObject::tr("“%1” is password-protected. Enter the password to open it.")
                               .arg(QFileInfo(path).fileName());
    bool ok = false;
    const QString pw =
        QInputDialog::getText(parent, title, prompt, QLineEdit::Password, QString(), &ok);
    if (!ok)
        return std::nullopt;
    return pw;
}

PdfAdapter::PasswordPrompt &activePasswordPrompt() {
    static PdfAdapter::PasswordPrompt prompt = defaultPasswordPrompt;
    return prompt;
}

} // namespace

void PdfAdapter::setPasswordPrompt(PasswordPrompt prompt) {
    activePasswordPrompt() = prompt ? std::move(prompt) : defaultPasswordPrompt;
}

PdfAdapter::PasswordPrompt PdfAdapter::passwordPrompt() {
    return activePasswordPrompt();
}

std::unique_ptr<IDocument> PdfAdapter::open(const QString &path) {
    auto doc = std::make_unique<PdfDocument>(path);

    // Password-gated PDF: prompt up to three times. Each iteration
    // asks the currently-installed PasswordPrompt hook; a nullopt
    // response ends the loop and leaves the document in its locked
    // state (createView falls back to a "Could not open" label).
    const int maxAttempts = 3;
    auto &prompt = activePasswordPrompt();
    for (int attempt = 0; attempt < maxAttempts && doc->needsPassword(); ++attempt) {
        std::optional<QString> pw = prompt(path, attempt);
        if (!pw)
            break;
        doc->unlock(*pw);
    }
    return doc;
}

} // namespace trailer
