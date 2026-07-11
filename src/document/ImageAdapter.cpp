#include "ImageAdapter.h"

#include "annotation/AnnotationStore.h"
#include "filters/ImageFilter.h"
#include "ui/AnnotationOverlay.h"
#include "ui/SelectableTextLayer.h"

#include <QColor>
#include <QEvent>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QLabel>
#include <QMovie>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPainterPath>
#include <QPdfWriter>
#include <QPixmap>
#include <QPrintDialog>
#include <QPrinter>
#include <QResizeEvent>
#include <QRect>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QTransform>

#include <cmath>

#include <algorithm>

namespace trailer {

namespace {

constexpr const char *kExtensions[] = {
    "png",  "jpg", "jpeg", "bmp", "gif", "tiff", "tif",
    "webp", "ppm", "pgm",  "pbm", "xbm", "xpm",  "ico",
};

// Step multiplier per zoom-in / zoom-out tap (~10% per step) —
// matches the rate Preview / Acrobat use. Change if dogfooding
// finds zoom feels jumpy (lower) or sluggish (higher).
constexpr double kZoomStep = 1.1;

// Hard zoom bounds. 0.05 (5%) preserves the user's place at a
// thumbnail-overview scale without letting a typical document
// collapse to invisibility. 32× (3200%) is pixel-inspection
// territory; past it Qt's int-pixel coordinate math risks
// overflowing on large images. Change if a real workflow hits
// either edge — e.g. a multi-gigapixel scan can't fit in the
// viewport, or zoomed-in pixel art needs more headroom.
constexpr double kZoomMin = 0.05;
constexpr double kZoomMax = 32.0;

// Cap on retained image snapshots per direction. Each snapshot is
// a full QImage copy, so the cap is lower than AnnotationStore's
// (small-struct snapshots). 32 holds a few hundred MB at typical
// photo sizes. Bump only if a real edit session runs out of undo.
constexpr size_t kMaxUndoSteps = 32;

Qt::PenStyle toPenStyle(DashStyle d) {
    switch (d) {
    case DashStyle::Solid:
        return Qt::SolidLine;
    case DashStyle::Dashed:
        return Qt::DashLine;
    case DashStyle::Dotted:
        return Qt::DotLine;
    }
    return Qt::SolidLine;
}

QImage flattenAnnotations(const QImage &base, const std::vector<Annotation> &anns) {
    if (anns.empty() || base.isNull())
        return base;
    QImage flat = base.convertToFormat(QImage::Format_ARGB32);
    QPainter p(&flat);
    p.setRenderHint(QPainter::Antialiasing, true);
    for (const Annotation &a : anns) {
        if (a.page != 0)
            continue;
        QPen pen(a.style.stroke);
        pen.setWidthF(a.style.strokeWidth);
        pen.setStyle(toPenStyle(a.style.dash));
        p.setPen(pen);
        p.setBrush(a.style.fill.alpha() > 0 ? QBrush(a.style.fill) : Qt::NoBrush);
        switch (a.type) {
        case AnnotationType::Rectangle:
            p.drawRect(a.bounds);
            break;
        case AnnotationType::Ellipse:
            p.drawEllipse(a.bounds);
            break;
        case AnnotationType::Line:
        case AnnotationType::Arrow: {
            if (a.points.size() < 2)
                break;
            p.drawLine(a.points[0], a.points[1]);
            if (a.type == AnnotationType::Arrow) {
                const QLineF line(a.points[1], a.points[0]);
                QLineF l1 = line, l2 = line;
                l1.setLength(12.0);
                l2.setLength(12.0);
                l1.setAngle(line.angle() + 25.0);
                l2.setAngle(line.angle() - 25.0);
                p.drawLine(l1);
                p.drawLine(l2);
            }
            break;
        }
        case AnnotationType::Ink: {
            if (a.points.size() < 2)
                break;
            for (size_t i = 1; i < a.points.size(); ++i) {
                p.drawLine(a.points[i - 1], a.points[i]);
            }
            break;
        }
        case AnnotationType::Text: {
            QFont f = p.font();
            if (!a.style.fontFamily.isEmpty())
                f.setFamily(a.style.fontFamily);
            f.setWeight(static_cast<QFont::Weight>(a.style.fontWeight));
            f.setPointSize(a.style.fontPointSize > 0 ? a.style.fontPointSize : 12);
            p.setFont(f);
            p.setPen(a.style.stroke);
            p.drawText(a.bounds, Qt::AlignLeft | Qt::TextWordWrap, a.text);
            break;
        }
        case AnnotationType::Note: {
            const QRectF icon(a.bounds.topLeft(), QSizeF(18.0, 18.0));
            const QColor noteColour =
                a.style.fill.alpha() > 0 ? a.style.fill : QColor(255, 225, 120);
            p.setBrush(noteColour);
            p.setPen(QPen(a.style.stroke, 1.0));
            p.drawRect(icon);
            QFont f = p.font();
            f.setPointSize(10);
            f.setBold(true);
            p.setFont(f);
            p.drawText(icon, Qt::AlignCenter, QStringLiteral("N"));
            break;
        }
        case AnnotationType::HighlightShape: {
            QColor fill = a.style.fill.alpha() > 0 ? a.style.fill : a.style.stroke;
            fill.setAlpha(a.style.fill.alpha() > 0 ? a.style.fill.alpha() : 90);
            p.fillRect(a.bounds, fill);
            p.setBrush(Qt::NoBrush);
            p.drawRect(a.bounds);
            break;
        }
        case AnnotationType::SpeechBubble: {
            const double radius =
                std::min(12.0, std::min(a.bounds.width(), a.bounds.height()) / 4.0);
            QPainterPath body;
            body.addRoundedRect(a.bounds, radius, radius);
            if (!a.points.empty()) {
                const QPointF tail = a.points.front();
                const QPointF anchor(a.bounds.left() + a.bounds.width() * 0.25, a.bounds.bottom());
                const QPointF anchor2(anchor.x() + radius, a.bounds.bottom());
                QPainterPath tailPath;
                tailPath.moveTo(anchor);
                tailPath.lineTo(tail);
                tailPath.lineTo(anchor2);
                tailPath.closeSubpath();
                body.addPath(tailPath);
            }
            p.drawPath(body);
            if (!a.text.isEmpty()) {
                QFont f = p.font();
                if (!a.style.fontFamily.isEmpty())
                    f.setFamily(a.style.fontFamily);
                f.setWeight(static_cast<QFont::Weight>(a.style.fontWeight));
                f.setPointSize(a.style.fontPointSize > 0 ? a.style.fontPointSize : 12);
                p.setFont(f);
                p.setPen(a.style.stroke);
                p.drawText(a.bounds.adjusted(8, 4, -8, -4), Qt::AlignCenter | Qt::TextWordWrap,
                           a.text);
            }
            break;
        }
        case AnnotationType::Redaction: {
            p.fillRect(a.bounds, Qt::black);
            break;
        }
        case AnnotationType::Signature: {
            if (a.imagePath.isEmpty() || a.bounds.isEmpty())
                break;
            QImage sig(a.imagePath);
            if (sig.isNull())
                break;
            // Scale to fit preserving aspect ratio (matches the
            // on-screen overlay preview).
            const QImage scaled =
                sig.scaled(a.bounds.size().toSize(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            const QPointF topLeft(a.bounds.left() + (a.bounds.width() - scaled.width()) / 2.0,
                                  a.bounds.top() + (a.bounds.height() - scaled.height()) / 2.0);
            p.drawImage(topLeft, scaled);
            break;
        }
        case AnnotationType::ZoomLens: {
            if (a.bounds.width() < 1 || a.bounds.height() < 1)
                break;
            const double z = a.style.zoomFactor > 0 ? a.style.zoomFactor : 2.0;
            const QSizeF srcSize(a.bounds.width() / z, a.bounds.height() / z);
            const QPointF centre = a.bounds.center();
            const QRectF src(centre.x() - srcSize.width() / 2, centre.y() - srcSize.height() / 2,
                             srcSize.width(), srcSize.height());
            const QRect srcRect =
                src.toRect().intersected(QRect(0, 0, base.width(), base.height()));
            if (!srcRect.isEmpty()) {
                const QImage slice = base.copy(srcRect).scaled(
                    a.bounds.size().toSize(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
                p.save();
                QPainterPath clip;
                clip.addEllipse(a.bounds);
                p.setClipPath(clip);
                p.drawImage(a.bounds.topLeft(), slice);
                p.restore();
            }
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(a.bounds);
            break;
        }
        default:
            break;
        }
    }
    return flat;
}

} // namespace

ImageDocument::ImageDocument(QString path) : m_path(std::move(path)) {
    QImageReader reader(m_path);
    reader.setAutoTransform(true);
    m_animated = reader.supportsAnimation() && reader.imageCount() > 1;
    if (m_animated) {
        m_frameCount = reader.imageCount();
        m_image = reader.read();
    } else {
        m_image = reader.read();
    }
    connectAnnotationHistory();
}

void ImageDocument::connectAnnotationHistory() {
    // ImageDocument is not a QObject; the store doubles as the
    // connection context so the lambdas can never outlive `this`
    // (m_annotations is a member and dies with the document).
    //
    // Unlike PdfDocument there is no m_suppressUndoLog counterpart:
    // images have no bulk annotation-load path (no sidecar reload, no
    // post-save re-read), so every historyPushed really is a user
    // edit. If session-restore for image annotations ever lands, it
    // will need the same suppress-and-rebuild bracket PdfDocument uses
    // in saveCommitOnUi().
    QObject::connect(&m_annotations, &AnnotationStore::historyPushed, &m_annotations,
                     [this]() { onAnnotationHistoryPushed(); });
    QObject::connect(&m_annotations, &AnnotationStore::historyEvicted, &m_annotations,
                     [this]() { onAnnotationHistoryEvicted(); });
}

void ImageDocument::onAnnotationHistoryPushed() {
    // A new annotation edit (one frame, compound-coalesced):
    // record it in the unified log and invalidate all redo.
    m_undoLog.push_back(UndoSource::Annotation);
    m_redoLog.clear();
    m_redoStack.clear();
}

void ImageDocument::onAnnotationHistoryEvicted() {
    // The store dropped its oldest frame to stay within its depth cap.
    // Drop the oldest Annotation entry from the chronological log so
    // the log's annotation count matches what the store can actually
    // undo.
    const auto it = std::find(m_undoLog.begin(), m_undoLog.end(), UndoSource::Annotation);
    if (it != m_undoLog.end()) {
        m_undoLog.erase(it);
    } else {
        qWarning("ImageDocument: AnnotationStore evicted an undo frame but the "
                 "chronological log holds no Annotation entry — log/store desync");
    }
}

QString ImageDocument::displayName() const {
    return QFileInfo(m_path).fileName();
}

QString ImageDocument::filePath() const {
    return m_path;
}

QWidget *ImageDocument::createView(QWidget *parent) {
    auto *scroll = new QScrollArea(parent);
    scroll->setAlignment(Qt::AlignCenter);
    scroll->setBackgroundRole(QPalette::Base);

    auto *label = new QLabel(scroll);
    label->setAlignment(Qt::AlignCenter);
    label->setBackgroundRole(QPalette::Base);

    if (m_animated) {
        auto *movie = new QMovie(m_path, QByteArray(), label);
        m_movie = movie;
        label->setMovie(movie);
        if (movie->isValid()) {
            movie->start();
        }
    } else if (!m_image.isNull()) {
        label->setPixmap(QPixmap::fromImage(m_image));
        label->adjustSize();
    } else {
        label->setText(QObject::tr("Could not decode image:\n%1").arg(m_path));
    }

    scroll->setWidget(label);
    m_scroll = scroll;
    m_label = label;
    installResizeWatcher();

    if (!m_animated && !m_image.isNull()) {
        // Fit-to-content on first show, capped at 100%. Same shape as
        // PdfDocument's applyInitialFitZoom: schedule on the event
        // loop so the scroll-area viewport has settled, then either
        // shrink-to-fit or leave the image at actual size.
        // Capture m_aliveFlag so the timer no-ops if the document is
        // destroyed before this tick fires — `this` would otherwise
        // dangle (the view widget is deleteLater()'d but the document
        // is destroyed immediately when the tab closes).
        if (!m_aliveFlag)
            m_aliveFlag = std::make_shared<bool>(true);
        auto alive = m_aliveFlag;
        QTimer::singleShot(0, scroll, [this, alive]() {
            if (alive && *alive)
                applyInitialFitZoom();
        });
    }

    if (!m_animated && !m_image.isNull()) {
        // Stacking order on the QLabel host: SelectableTextLayer
        // sits beneath the AnnotationOverlay so a user-drawn
        // annotation paints over selectable-text highlights. The
        // I-beam cursor lives on the text layer; the overlay was
        // changed to no longer claim it unconditionally.
        auto *textLayer = new SelectableTextLayer(label);
        textLayer->setStore(&m_selectableText);
        textLayer->setDocToView(
            [this](QPointF p, int /*page*/) { return QPointF(p.x() * m_scale, p.y() * m_scale); });
        textLayer->setPageAtView([](QPointF) { return 0; });
        textLayer->setGeometry(label->rect());
        textLayer->show();
        m_textLayer = textLayer;

        auto *overlay = new AnnotationOverlay(label);
        overlay->setStore(&m_annotations);
        overlay->setDocumentToView(
            [this](QPointF p, int /*page*/) { return QPointF(p.x() * m_scale, p.y() * m_scale); });
        overlay->setViewToDocument([this](QPointF p, int /*page*/) {
            if (m_scale <= 0.0)
                return p;
            return QPointF(p.x() / m_scale, p.y() / m_scale);
        });
        overlay->setSourceSampler([this](QRectF docRect, QSize outPx, int /*page*/) -> QImage {
            if (m_image.isNull())
                return {};
            const QRect src = docRect.toRect().intersected(m_image.rect());
            if (src.isEmpty())
                return {};
            return m_image.copy(src).scaled(outPx, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        });
        overlay->setGeometry(label->rect());
        overlay->show();
        // Raise so the overlay paints on top of the text layer.
        overlay->raise();
        m_overlay = overlay;
    }

    return scroll;
}

void ImageDocument::applyScale(double factor) {
    if (!m_label || m_image.isNull()) {
        return;
    }
    m_scale = std::clamp(factor, kZoomMin, kZoomMax);
    const QSize target = m_image.size() * m_scale;
    const QPixmap scaled =
        QPixmap::fromImage(m_image).scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_label->setPixmap(scaled);
    m_label->adjustSize();
    if (m_overlay) {
        m_overlay->setGeometry(m_label->rect());
    }
    if (m_textLayer) {
        m_textLayer->setGeometry(m_label->rect());
    }
}

void ImageDocument::zoomIn() {
    // Any explicit zoom step puts the document back into Custom mode —
    // the user is asking for a specific factor, not "track the
    // viewport". Mirrors PdfDocument::applyZoomFactor's behaviour.
    m_zoomMode = ZoomMode::Custom;
    applyScale(m_scale * kZoomStep);
}

void ImageDocument::zoomOut() {
    m_zoomMode = ZoomMode::Custom;
    applyScale(m_scale / kZoomStep);
}

void ImageDocument::zoomActual() {
    m_zoomMode = ZoomMode::Actual;
    applyScale(1.0);
}

void ImageDocument::zoomFitWidth() {
    m_zoomMode = ZoomMode::FitToWidth;
    reapplyFitMode();
}

void ImageDocument::zoomFitPage() {
    m_zoomMode = ZoomMode::FitInView;
    reapplyFitMode();
}

void ImageDocument::reapplyFitMode() {
    // No-op for non-fit modes — the user has set an explicit scale
    // factor (Custom/Actual) and doesn't want it to track viewport size.
    if (m_zoomMode != ZoomMode::FitInView && m_zoomMode != ZoomMode::FitToWidth)
        return;
    if (!m_scroll || !m_label || m_image.isNull())
        return;
    const int availW = m_scroll->viewport()->width();
    const int availH = m_scroll->viewport()->height();
    if (availW <= 0 || m_image.width() <= 0)
        return;
    if (m_zoomMode == ZoomMode::FitToWidth) {
        applyScale(static_cast<double>(availW) / static_cast<double>(m_image.width()));
        return;
    }
    // FitInView: constrain both dimensions.
    if (availH <= 0 || m_image.height() <= 0)
        return;
    const double scaleW =
        static_cast<double>(availW) / static_cast<double>(m_image.width());
    const double scaleH =
        static_cast<double>(availH) / static_cast<double>(m_image.height());
    applyScale(std::min(scaleW, scaleH));
}

namespace {

// Tiny QObject-derived helper. eventFilter is on QObject; ImageDocument
// isn't a QObject (the adapter interfaces aren't QObject-aware) so we
// can't install it directly. A small bridge owned by the scroll
// area's viewport gets the same effect.
//
// Lifetime: parented to the viewport, so it dies with the widget
// hierarchy. The alive-flag (shared with ImageDocument) handles the
// case where the document is destroyed before the view — the flag
// goes false and the watcher stops calling into freed memory.
class FitModeResizeWatcher : public QObject {
  public:
    FitModeResizeWatcher(ImageDocument *doc, std::shared_ptr<bool> alive, QObject *parent)
        : QObject(parent), m_doc(doc), m_alive(std::move(alive)) {}

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (event->type() == QEvent::Resize && m_alive && *m_alive) {
            m_doc->reapplyFitMode();
        }
        return QObject::eventFilter(watched, event);
    }

  private:
    ImageDocument *m_doc;
    std::shared_ptr<bool> m_alive;
};

} // namespace

void ImageDocument::installResizeWatcher() {
    if (!m_scroll || !m_scroll->viewport())
        return;
    if (m_resizeWatcher)
        return;
    if (!m_aliveFlag)
        m_aliveFlag = std::make_shared<bool>(true);
    auto *watcher = new FitModeResizeWatcher(this, m_aliveFlag, m_scroll->viewport());
    m_scroll->viewport()->installEventFilter(watcher);
    m_resizeWatcher = watcher;
}

ImageDocument::~ImageDocument() {
    if (m_aliveFlag)
        *m_aliveFlag = false;
}

void ImageDocument::applyInitialFitZoom() {
    if (m_initialZoomApplied)
        return;
    if (!m_scroll || !m_label || m_image.isNull())
        return;
    const int availW = m_scroll->viewport()->width();
    const int availH = m_scroll->viewport()->height();
    if (availW <= 0 || availH <= 0 ||
        m_image.width() <= 0 || m_image.height() <= 0) {
        // Layout hasn't settled — retry on the next tick. Guard with
        // the alive flag the resize watcher already maintains, so a
        // tab close mid-retry doesn't dereference a freed document.
        if (!m_aliveFlag)
            m_aliveFlag = std::make_shared<bool>(true);
        auto alive = m_aliveFlag;
        QTimer::singleShot(0, m_scroll, [this, alive]() {
            if (alive && *alive)
                applyInitialFitZoom();
        });
        return;
    }
    m_initialZoomApplied = true;
    const double scaleW =
        static_cast<double>(availW) / static_cast<double>(m_image.width());
    const double scaleH =
        static_cast<double>(availH) / static_cast<double>(m_image.height());
    const double fit = std::min(scaleW, scaleH);
    // Cap at 100%: a 200×100 thumbnail shouldn't blow up to fill the
    // window. Larger images shrink to fit instead.
    applyScale(std::min(1.0, fit));
    // Park the mode at FitInView so subsequent window resizes refit
    // (the watcher above calls reapplyFitMode on each viewport resize).
    m_zoomMode = ZoomMode::FitInView;
}

void ImageDocument::applyZoomState(ZoomMode mode, double factor) {
    switch (mode) {
    case ZoomMode::FitInView:
        zoomFitPage();
        return;
    case ZoomMode::FitToWidth:
        zoomFitWidth();
        return;
    case ZoomMode::Actual:
        zoomActual();
        return;
    case ZoomMode::Custom:
        if (factor > 0.0) {
            m_zoomMode = ZoomMode::Custom;
            applyScale(factor);
        }
        return;
    }
}

int ImageDocument::scrollY() const {
    if (!m_scroll)
        return 0;
    if (auto *bar = m_scroll->verticalScrollBar())
        return bar->value();
    return 0;
}

void ImageDocument::applyScrollY(int y) {
    if (!m_scroll)
        return;
    auto *bar = m_scroll->verticalScrollBar();
    if (!bar)
        return;
    const int clamped = std::clamp(y, bar->minimum(), bar->maximum());
    bar->setValue(clamped);
}

void ImageDocument::refreshView() {
    if (!m_label || m_image.isNull())
        return;
    applyScale(m_scale);
}

void ImageDocument::setAnnotationTool(AnnotationTool tool) {
    if (m_overlay)
        m_overlay->setActiveTool(tool);
}

void ImageDocument::setAnnotationStyle(const AnnotationStyle &style) {
    if (m_overlay)
        m_overlay->setStyle(style);
}

void ImageDocument::setPendingAnnotationText(const QString &text) {
    if (m_overlay)
        m_overlay->setPendingTextPreset(text);
}

void ImageDocument::setPendingSignaturePath(const QString &path) {
    if (m_overlay)
        m_overlay->setPendingSignaturePath(path);
}

void ImageDocument::pushUndoSnapshot() {
    m_undoStack.push_back(m_image);
    if (m_undoStack.size() > kMaxUndoSteps) {
        m_undoStack.erase(m_undoStack.begin());
        // Evict from the chronological log in the same breath — the
        // oldest ImageOp entry, matching the snapshot just dropped —
        // so the log never claims a pixel undo the stack cannot
        // perform.
        const auto it = std::find(m_undoLog.begin(), m_undoLog.end(), UndoSource::ImageOp);
        if (it != m_undoLog.end()) {
            m_undoLog.erase(it);
        } else {
            qWarning("ImageDocument: pixel snapshot evicted but the chronological "
                     "log holds no ImageOp entry — log/stack desync");
        }
    }
    // A new pixel edit invalidates all redo — both stacks and the
    // unified redo log — then appends to the chronological undo log.
    m_redoStack.clear();
    m_annotations.clearRedo();
    m_redoLog.clear();
    m_undoLog.push_back(UndoSource::ImageOp);
    // Any pixel-level mutation invalidates the OCR cache for the
    // single page. The next auto-OCR pump will re-enqueue work for
    // the now-stale page; until then, the SelectableTextLayer paints
    // nothing for it (no popup, no error — quiet degradation).
    m_selectableText.invalidate(0);
}

bool ImageDocument::undo() {
    // Pop the single chronological log so the most recent op is undone
    // first, regardless of which stack it came from. Mirrors
    // PdfDocument::undo().
    if (m_undoLog.empty())
        return false;
    const UndoSource src = m_undoLog.back();
    if (src == UndoSource::Annotation) {
        if (!m_annotations.canUndo()) {
            qWarning("ImageDocument::undo: log expects an annotation frame but the "
                     "AnnotationStore history is empty; dropping the orphaned entry");
            m_undoLog.pop_back();
            return false;
        }
        m_undoLog.pop_back();
        m_annotations.undo();
    } else {
        if (m_undoStack.empty()) {
            qWarning("ImageDocument::undo: log expects a pixel snapshot but the "
                     "snapshot stack is empty; dropping the orphaned entry");
            m_undoLog.pop_back();
            return false;
        }
        m_undoLog.pop_back();
        m_redoStack.push_back(m_image);
        m_image = m_undoStack.back();
        m_undoStack.pop_back();
        m_dirty = !m_undoStack.empty() || m_dirty;
        // The page contents changed — clear any cached OCR.
        m_selectableText.invalidate(0);
        refreshView();
    }
    m_redoLog.push_back(src);
    return true;
}

bool ImageDocument::redo() {
    // Inverse of undo(): pop the redo log and re-apply on the stack the
    // entry came from, in the order the ops were originally undone.
    if (m_redoLog.empty())
        return false;
    const UndoSource src = m_redoLog.back();
    if (src == UndoSource::Annotation) {
        if (!m_annotations.canRedo()) {
            qWarning("ImageDocument::redo: log expects an annotation frame but the "
                     "AnnotationStore redo history is empty; dropping the orphaned entry");
            m_redoLog.pop_back();
            return false;
        }
        m_redoLog.pop_back();
        m_annotations.redo();
    } else {
        if (m_redoStack.empty()) {
            qWarning("ImageDocument::redo: log expects a pixel snapshot but the "
                     "snapshot stack is empty; dropping the orphaned entry");
            m_redoLog.pop_back();
            return false;
        }
        m_redoLog.pop_back();
        m_undoStack.push_back(m_image);
        m_image = m_redoStack.back();
        m_redoStack.pop_back();
        m_dirty = true;
        m_selectableText.invalidate(0);
        refreshView();
    }
    m_undoLog.push_back(src);
    return true;
}

void ImageDocument::rotatePage(int /*pageIndex*/, int degreesClockwise) {
    if (m_image.isNull() || m_animated)
        return;
    pushUndoSnapshot();
    QTransform t;
    t.rotate(degreesClockwise);
    m_image = m_image.transformed(t, Qt::SmoothTransformation);
    m_dirty = true;
    refreshView();
}

void ImageDocument::flipHorizontal() {
    if (m_image.isNull() || m_animated)
        return;
    pushUndoSnapshot();
    // Qt 6.9 introduced QImage::flipped(Qt::Orientations) and deprecated
    // mirrored(); the deprecation warning fires under -Werror on Qt 6.11+
    // local builds. CI's Qt 6.8 doesn't have flipped() yet, so we keep
    // both paths until the minimum Qt version moves to 6.9.
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    m_image = m_image.flipped(Qt::Horizontal);
#else
    m_image = m_image.mirrored(/*horizontally=*/true, /*vertically=*/false);
#endif
    m_dirty = true;
    refreshView();
}

void ImageDocument::flipVertical() {
    if (m_image.isNull() || m_animated)
        return;
    pushUndoSnapshot();
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    m_image = m_image.flipped(Qt::Vertical);
#else
    m_image = m_image.mirrored(/*horizontally=*/false, /*vertically=*/true);
#endif
    m_dirty = true;
    refreshView();
}

bool ImageDocument::resizeImage(int width, int height, bool smoothScaling) {
    if (m_image.isNull() || m_animated || width <= 0 || height <= 0) {
        return false;
    }
    pushUndoSnapshot();
    const Qt::TransformationMode mode =
        smoothScaling ? Qt::SmoothTransformation : Qt::FastTransformation;
    m_image = m_image.scaled(width, height, Qt::IgnoreAspectRatio, mode);
    m_dirty = true;
    refreshView();
    return true;
}

bool ImageDocument::cropToRect(int x, int y, int width, int height) {
    if (m_image.isNull() || m_animated)
        return false;
    const QRect bounds(0, 0, m_image.width(), m_image.height());
    const QRect rect = QRect(x, y, width, height).intersected(bounds);
    if (rect.isEmpty())
        return false;
    pushUndoSnapshot();
    m_image = m_image.copy(rect);
    m_dirty = true;
    refreshView();
    return true;
}

namespace {

QImage applyColourTransform(const QImage &src, double brightness, double contrast,
                            double saturation) {
    const double bAdd = std::clamp(brightness, -1.0, 1.0) * 255.0;
    const double cFactor = (1.0 + std::clamp(contrast, -1.0, 1.0));
    const double sFactor = (1.0 + std::clamp(saturation, -1.0, 1.0));
    const double kLumR = 0.299;
    const double kLumG = 0.587;
    const double kLumB = 0.114;

    QImage work = src.convertToFormat(QImage::Format_ARGB32);
    const int h = work.height();
    const int w = work.width();
    for (int y = 0; y < h; ++y) {
        auto *scanline = reinterpret_cast<QRgb *>(work.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb px = scanline[x];
            double r = qRed(px);
            double g = qGreen(px);
            double b = qBlue(px);
            const int a = qAlpha(px);

            r += bAdd;
            g += bAdd;
            b += bAdd;
            r = (r - 128.0) * cFactor + 128.0;
            g = (g - 128.0) * cFactor + 128.0;
            b = (b - 128.0) * cFactor + 128.0;
            const double lum = r * kLumR + g * kLumG + b * kLumB;
            r = lum + (r - lum) * sFactor;
            g = lum + (g - lum) * sFactor;
            b = lum + (b - lum) * sFactor;

            scanline[x] = qRgba(std::clamp(static_cast<int>(std::lround(r)), 0, 255),
                                std::clamp(static_cast<int>(std::lround(g)), 0, 255),
                                std::clamp(static_cast<int>(std::lround(b)), 0, 255), a);
        }
    }
    return work;
}

} // namespace

bool ImageDocument::adjustColour(double brightness, double contrast, double saturation) {
    if (m_image.isNull() || m_animated)
        return false;
    pushUndoSnapshot();
    m_image = applyColourTransform(m_image, brightness, contrast, saturation);
    m_dirty = true;
    refreshView();
    return true;
}

void ImageDocument::previewColour(double brightness, double contrast, double saturation) {
    if (!m_label || m_image.isNull() || m_animated)
        return;
    const QImage preview = applyColourTransform(m_image, brightness, contrast, saturation);
    const QSize target = preview.size() * m_scale;
    m_label->setPixmap(
        QPixmap::fromImage(preview).scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void ImageDocument::clearColourPreview() {
    refreshView();
}

bool ImageDocument::replaceImage(const QImage &replacement) {
    if (m_image.isNull() || m_animated || replacement.isNull())
        return false;
    // ML features produce ARGB32 output the same size as the input;
    // anything else is a bug in the caller, so we hard-fail rather
    // than silently resample.
    if (replacement.size() != m_image.size())
        return false;
    pushUndoSnapshot();
    m_image = replacement;
    m_dirty = true;
    refreshView();
    return true;
}

bool ImageDocument::exportAs(const QString &destPath, const QString &format, int quality,
                             const QString &filterId) const {
    if (m_image.isNull())
        return false;
    QImage out = flattenAnnotations(m_image, m_annotations.annotations());
    if (!filterId.isEmpty()) {
        // Apply colour filter after flattening so annotations are
        // tinted along with the rest of the image — matches what the
        // user sees on screen when they toggle a preview.
        out = applyFilter(filterId, out);
    }
    // PDF export is a separate code path: a one-page PDF whose
    // /MediaBox matches the image's pixel size at 72 DPI, with the
    // image painted to fill. QImageWriter doesn't support PDF, so
    // we use QPdfWriter directly. This is the "I emailed the photo
    // of the form to my CPA but they want a PDF" workflow.
    if (format.compare(QLatin1String("pdf"), Qt::CaseInsensitive) == 0) {
        QPdfWriter writer(destPath);
        writer.setResolution(72);
        const QPageSize ps(out.size(), QPageSize::Point, QStringLiteral("ImagePage"),
                           QPageSize::ExactMatch);
        writer.setPageSize(ps);
        writer.setPageMargins(QMarginsF(0, 0, 0, 0));
        QPainter painter(&writer);
        if (!painter.isActive())
            return false;
        painter.drawImage(QRectF(0, 0, out.width(), out.height()), out);
        painter.end();
        return true;
    }
    QImageWriter writer(destPath, format.toLatin1());
    if (quality >= 0)
        writer.setQuality(quality);
    return writer.write(out);
}

bool ImageDocument::save(const QString &newPath) {
    if (m_image.isNull() || m_animated)
        return false;
    const QString target = newPath.isEmpty() ? m_path : newPath;
    if (target.isEmpty())
        return false;
    const QImage out = flattenAnnotations(m_image, m_annotations.annotations());
    const QByteArray format = QFileInfo(target).suffix().toLatin1().toLower();
    QImageWriter writer(target, format.isEmpty() ? QByteArray("png") : format);
    if (!writer.write(out))
        return false;
    m_image = out;
    m_annotations.clear();
    m_path = target;
    m_dirty = false;
    return true;
}

int ImageDocument::currentFrame() const {
    return m_movie ? m_movie->currentFrameNumber() : 0;
}

void ImageDocument::setCurrentFrame(int frame) {
    if (!m_movie || frame < 0 || (m_frameCount > 0 && frame >= m_frameCount)) {
        return;
    }
    if (m_movie->state() == QMovie::Running) {
        m_movie->setPaused(true);
    }
    m_movie->jumpToFrame(frame);
}

bool ImageDocument::isAnimationPlaying() const {
    return m_movie && m_movie->state() == QMovie::Running;
}

void ImageDocument::setAnimationPlaying(bool playing) {
    if (!m_movie) {
        return;
    }
    if (playing) {
        if (m_movie->state() == QMovie::Paused) {
            m_movie->setPaused(false);
        } else if (m_movie->state() == QMovie::NotRunning) {
            m_movie->start();
        }
    } else {
        if (m_movie->state() == QMovie::Running) {
            m_movie->setPaused(true);
        }
    }
}

QImage ImageDocument::renderThumbnail(int pageIndex, QSize targetSize) {
    if (pageIndex != 0 || m_image.isNull() || !targetSize.isValid()) {
        return {};
    }
    return m_image.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

void ImageDocument::print(QWidget *dialogParent) {
    if (m_image.isNull()) {
        return;
    }
    QPrinter printer(QPrinter::HighResolution);
    printer.setDocName(displayName());
    QPrintDialog dialog(&printer, dialogParent);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    QPainter painter;
    if (!painter.begin(&printer)) {
        return;
    }
    const QRect target = printer.pageLayout().paintRectPixels(printer.resolution());
    const QImage scaled =
        m_image.scaled(target.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const int x = target.x() + (target.width() - scaled.width()) / 2;
    const int y = target.y() + (target.height() - scaled.height()) / 2;
    painter.drawImage(QPoint(x, y), scaled);
    painter.end();
}

QStringList ImageAdapter::mimeTypes() const {
    return {
        QStringLiteral("image/png"),
        QStringLiteral("image/jpeg"),
        QStringLiteral("image/bmp"),
        QStringLiteral("image/gif"),
        QStringLiteral("image/tiff"),
        QStringLiteral("image/webp"),
        QStringLiteral("image/x-portable-pixmap"),
        QStringLiteral("image/x-portable-graymap"),
        QStringLiteral("image/x-portable-bitmap"),
        QStringLiteral("image/x-xbitmap"),
        QStringLiteral("image/x-xpixmap"),
        QStringLiteral("image/vnd.microsoft.icon"),
    };
}

QStringList ImageAdapter::extensions() const {
    QStringList out;
    out.reserve(static_cast<int>(std::size(kExtensions)));
    for (const char *ext : kExtensions) {
        out.append(QString::fromLatin1(ext));
    }
    return out;
}

std::unique_ptr<IDocument> ImageAdapter::open(const QString &path) {
    return std::make_unique<ImageDocument>(path);
}

} // namespace trailer
