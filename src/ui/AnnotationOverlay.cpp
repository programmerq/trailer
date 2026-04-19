#include "AnnotationOverlay.h"

#include "annotation/AnnotationStore.h"

#include <QEvent>
#include <QFont>
#include <QInputDialog>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QWidget>

#include <algorithm>

namespace trailer {

AnnotationOverlay::AnnotationOverlay(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setMouseTracking(true);
    m_docToView = [](QPointF p) { return p; };
    m_viewToDoc = [](QPointF p) { return p; };
}

void AnnotationOverlay::setStore(AnnotationStore* store) {
    if (m_store == store) return;
    if (m_store) disconnect(m_store, nullptr, this, nullptr);
    m_store = store;
    if (m_store) {
        connect(m_store, &AnnotationStore::changed,
                this, QOverload<>::of(&QWidget::update));
    }
    update();
}

void AnnotationOverlay::setActiveTool(AnnotationTool tool) {
    m_tool = tool;
    const bool interactive = tool != AnnotationTool::None;
    setAttribute(Qt::WA_TransparentForMouseEvents, !interactive);
    setCursor(interactive ? Qt::CrossCursor : Qt::ArrowCursor);
}

void AnnotationOverlay::setStyle(const AnnotationStyle& style) {
    m_style = style;
}

void AnnotationOverlay::setPage(int page) {
    if (m_page == page) return;
    m_page = page;
    update();
}

void AnnotationOverlay::setDocumentToView(std::function<QPointF(QPointF)> fn) {
    m_docToView = std::move(fn);
    update();
}

void AnnotationOverlay::setViewToDocument(std::function<QPointF(QPointF)> fn) {
    m_viewToDoc = std::move(fn);
}

void AnnotationOverlay::setTextSelectionProvider(TextSelectionProvider fn) {
    m_textSelection = std::move(fn);
}

void AnnotationOverlay::setSourceSampler(SourceSampler fn) {
    m_sourceSampler = std::move(fn);
    update();
}

QRectF AnnotationOverlay::docRectToView(const QRectF& r) const {
    const QPointF tl = m_docToView(r.topLeft());
    const QPointF br = m_docToView(r.bottomRight());
    return QRectF(tl, br).normalized();
}

QPointF AnnotationOverlay::toDoc(const QPointF& viewPt) const {
    return m_viewToDoc(viewPt);
}

void AnnotationOverlay::paintEvent(QPaintEvent* /*event*/) {
    if (!m_store) return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    auto applyDash = [](QPen& pen, DashStyle d) {
        switch (d) {
            case DashStyle::Solid:  pen.setStyle(Qt::SolidLine); break;
            case DashStyle::Dashed: pen.setStyle(Qt::DashLine); break;
            case DashStyle::Dotted: pen.setStyle(Qt::DotLine); break;
        }
    };

    auto drawOne = [&](const Annotation& a) {
        QPen pen(a.style.stroke);
        pen.setWidthF(a.style.strokeWidth);
        applyDash(pen, a.style.dash);
        p.setPen(pen);
        p.setBrush(a.style.fill.alpha() > 0 ? QBrush(a.style.fill) : Qt::NoBrush);
        const QRectF viewRect = docRectToView(a.bounds);
        switch (a.type) {
            case AnnotationType::Rectangle:
                p.drawRect(viewRect);
                break;
            case AnnotationType::Ellipse:
                p.drawEllipse(viewRect);
                break;
            case AnnotationType::Line:
            case AnnotationType::Arrow: {
                if (a.points.size() < 2) break;
                const QPointF a0 = m_docToView(a.points[0]);
                const QPointF a1 = m_docToView(a.points[1]);
                p.drawLine(a0, a1);
                if (a.type == AnnotationType::Arrow) {
                    const QLineF line(a1, a0);
                    QLineF l1 = line;
                    QLineF l2 = line;
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
                if (a.points.size() < 2) break;
                QPainterPath path(m_docToView(a.points[0]));
                for (size_t i = 1; i < a.points.size(); ++i) {
                    path.lineTo(m_docToView(a.points[i]));
                }
                p.drawPath(path);
                break;
            }
            case AnnotationType::Text: {
                QFont f = p.font();
                if (!a.style.fontFamily.isEmpty()) f.setFamily(a.style.fontFamily);
                f.setWeight(static_cast<QFont::Weight>(a.style.fontWeight));
                f.setPointSize(a.style.fontPointSize > 0 ? a.style.fontPointSize : 12);
                p.setFont(f);
                p.setPen(a.style.stroke);
                p.drawText(viewRect, Qt::AlignLeft | Qt::TextWordWrap, a.text);
                break;
            }
            case AnnotationType::Note: {
                const QPointF tl = m_docToView(a.bounds.topLeft());
                const QRectF icon(tl.x(), tl.y(), 18.0, 18.0);
                p.setBrush(QColor(255, 225, 120));
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
                QColor fill = a.style.fill.alpha() > 0 ? a.style.fill
                                                       : a.style.stroke;
                fill.setAlpha(a.style.fill.alpha() > 0 ? a.style.fill.alpha() : 90);
                p.fillRect(viewRect, fill);
                p.setBrush(Qt::NoBrush);
                p.drawRect(viewRect);
                break;
            }
            case AnnotationType::SpeechBubble: {
                const double radius = std::min(12.0, std::min(viewRect.width(),
                                                              viewRect.height()) / 4.0);
                QPainterPath body;
                body.addRoundedRect(viewRect, radius, radius);
                if (!a.points.empty()) {
                    const QPointF tail = m_docToView(a.points.front());
                    const QPointF anchor(viewRect.left() + viewRect.width() * 0.25,
                                         viewRect.bottom());
                    const QPointF anchor2(anchor.x() + radius, viewRect.bottom());
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
                    if (!a.style.fontFamily.isEmpty()) f.setFamily(a.style.fontFamily);
                    f.setWeight(static_cast<QFont::Weight>(a.style.fontWeight));
                    f.setPointSize(a.style.fontPointSize > 0 ? a.style.fontPointSize : 12);
                    p.setFont(f);
                    p.setPen(a.style.stroke);
                    p.drawText(viewRect.adjusted(8, 4, -8, -4),
                               Qt::AlignCenter | Qt::TextWordWrap, a.text);
                }
                break;
            }
            case AnnotationType::ZoomLens: {
                p.save();
                QPainterPath clip;
                clip.addEllipse(viewRect);
                p.setClipPath(clip);
                if (m_sourceSampler && viewRect.width() > 1 && viewRect.height() > 1) {
                    const double z = a.style.zoomFactor > 0 ? a.style.zoomFactor : 2.0;
                    const QSizeF docSize(a.bounds.width() / z, a.bounds.height() / z);
                    const QPointF center = a.bounds.center();
                    const QRectF srcDoc(center.x() - docSize.width() / 2.0,
                                        center.y() - docSize.height() / 2.0,
                                        docSize.width(), docSize.height());
                    const QSize outPx(std::max(1, static_cast<int>(viewRect.width())),
                                      std::max(1, static_cast<int>(viewRect.height())));
                    const QImage sampled = m_sourceSampler(srcDoc, outPx, a.page);
                    if (!sampled.isNull()) {
                        p.drawImage(viewRect, sampled);
                    }
                }
                p.restore();
                p.setBrush(Qt::NoBrush);
                p.drawEllipse(viewRect);
                break;
            }
            case AnnotationType::Highlight:
            case AnnotationType::Underline:
            case AnnotationType::StrikeOut: {
                const std::vector<QRectF>& rects =
                    a.quads.empty() ? std::vector<QRectF>{a.bounds} : a.quads;
                for (const QRectF& r : rects) {
                    const QRectF vr = docRectToView(r);
                    if (a.type == AnnotationType::Highlight) {
                        QColor fill = a.style.stroke;
                        fill.setAlpha(90);
                        p.fillRect(vr, fill);
                    } else {
                        QPen thin(a.style.stroke);
                        thin.setWidthF(std::max(1.0, a.style.strokeWidth));
                        p.setPen(thin);
                        const double y = (a.type == AnnotationType::Underline)
                            ? vr.bottom() - 1.0
                            : vr.center().y();
                        p.drawLine(QPointF(vr.left(), y), QPointF(vr.right(), y));
                    }
                }
                break;
            }
            default:
                break;
        }
    };

    for (const Annotation& a : m_store->annotations()) {
        if (a.page != m_page) continue;
        drawOne(a);
    }

    if (m_dragging) {
        Annotation preview;
        preview.page = m_page;
        preview.type = [this]() {
            switch (m_tool) {
                case AnnotationTool::Ellipse:        return AnnotationType::Ellipse;
                case AnnotationTool::Line:           return AnnotationType::Line;
                case AnnotationTool::Arrow:          return AnnotationType::Arrow;
                case AnnotationTool::Ink:            return AnnotationType::Ink;
                case AnnotationTool::HighlightShape: return AnnotationType::HighlightShape;
                case AnnotationTool::SpeechBubble:   return AnnotationType::SpeechBubble;
                case AnnotationTool::ZoomLens:       return AnnotationType::ZoomLens;
                default:                             return AnnotationType::Rectangle;
            }
        }();
        preview.style = m_style;
        preview.bounds = QRectF(m_dragStartDoc, m_dragCurrentDoc).normalized();
        if (preview.type == AnnotationType::Line ||
            preview.type == AnnotationType::Arrow) {
            preview.points = {m_dragStartDoc, m_dragCurrentDoc};
        } else if (preview.type == AnnotationType::Ink) {
            preview.points = m_inkPoints;
        }
        drawOne(preview);
    }
}

bool AnnotationOverlay::eventFilter(QObject* obj, QEvent* event) {
    if (obj == parentWidget() && event->type() == QEvent::Resize) {
        if (auto* w = qobject_cast<QWidget*>(obj)) {
            setGeometry(w->rect());
        }
    }
    return QWidget::eventFilter(obj, event);
}

void AnnotationOverlay::mousePressEvent(QMouseEvent* event) {
    if (!m_store || m_tool == AnnotationTool::None ||
        m_tool == AnnotationTool::Select) {
        event->ignore();
        return;
    }
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    m_dragStartDoc = toDoc(event->position());
    m_dragCurrentDoc = m_dragStartDoc;
    m_dragging = true;
    m_inkPoints.clear();
    if (m_tool == AnnotationTool::Ink) {
        m_inkPoints.push_back(m_dragStartDoc);
    }
    update();
}

void AnnotationOverlay::mouseMoveEvent(QMouseEvent* event) {
    if (!m_dragging) return;
    m_dragCurrentDoc = toDoc(event->position());
    if (m_tool == AnnotationTool::Ink) {
        m_inkPoints.push_back(m_dragCurrentDoc);
    }
    update();
}

void AnnotationOverlay::mouseReleaseEvent(QMouseEvent* event) {
    if (!m_dragging || event->button() != Qt::LeftButton) return;
    m_dragging = false;
    const QPointF end = toDoc(event->position());

    Annotation a;
    a.page = m_page;
    a.style = m_style;
    a.bounds = QRectF(m_dragStartDoc, end).normalized();

    switch (m_tool) {
        case AnnotationTool::Rectangle: a.type = AnnotationType::Rectangle; break;
        case AnnotationTool::Ellipse:   a.type = AnnotationType::Ellipse; break;
        case AnnotationTool::Line:
            a.type = AnnotationType::Line;
            a.points = {m_dragStartDoc, end};
            break;
        case AnnotationTool::Arrow:
            a.type = AnnotationType::Arrow;
            a.points = {m_dragStartDoc, end};
            break;
        case AnnotationTool::Ink: {
            if (m_inkPoints.size() < 2) { m_inkPoints.clear(); update(); return; }
            a.type = AnnotationType::Ink;
            a.points = m_inkPoints;
            qreal minX = m_inkPoints.front().x(), maxX = minX;
            qreal minY = m_inkPoints.front().y(), maxY = minY;
            for (const auto& p : m_inkPoints) {
                minX = std::min(minX, p.x()); maxX = std::max(maxX, p.x());
                minY = std::min(minY, p.y()); maxY = std::max(maxY, p.y());
            }
            a.bounds = QRectF(QPointF(minX, minY), QPointF(maxX, maxY));
            m_inkPoints.clear();
            break;
        }
        case AnnotationTool::Text: {
            QRectF rect = a.bounds;
            if (rect.width() < 40.0 || rect.height() < 20.0) {
                rect = QRectF(m_dragStartDoc, QSizeF(200.0, 40.0));
            }
            bool ok = false;
            const QString text = QInputDialog::getMultiLineText(
                this, tr("Text Annotation"), tr("Text:"), QString(), &ok);
            if (!ok || text.isEmpty()) { update(); return; }
            a.type = AnnotationType::Text;
            a.bounds = rect;
            a.text = text;
            break;
        }
        case AnnotationTool::Note: {
            bool ok = false;
            const QString text = QInputDialog::getMultiLineText(
                this, tr("Note"), tr("Note body:"), QString(), &ok);
            if (!ok) { update(); return; }
            a.type = AnnotationType::Note;
            a.bounds = QRectF(m_dragStartDoc, QSizeF(18.0, 18.0));
            a.text = text;
            break;
        }
        case AnnotationTool::HighlightShape:
            a.type = AnnotationType::HighlightShape;
            break;
        case AnnotationTool::SpeechBubble: {
            QRectF rect = a.bounds;
            if (rect.width() < 40.0 || rect.height() < 20.0) {
                rect = QRectF(m_dragStartDoc, QSizeF(200.0, 80.0));
            }
            bool ok = false;
            const QString text = QInputDialog::getMultiLineText(
                this, tr("Speech Bubble"), tr("Text:"), QString(), &ok);
            if (!ok) { update(); return; }
            a.type = AnnotationType::SpeechBubble;
            a.bounds = rect;
            a.text = text;
            const QPointF tail(rect.left() - 20.0, rect.bottom() + 30.0);
            a.points = {tail};
            break;
        }
        case AnnotationTool::ZoomLens: {
            QRectF rect = a.bounds;
            if (rect.width() < 20.0 || rect.height() < 20.0) {
                const double side = 100.0;
                rect = QRectF(m_dragStartDoc - QPointF(side / 2, side / 2),
                              QSizeF(side, side));
            }
            a.type = AnnotationType::ZoomLens;
            a.bounds = rect;
            break;
        }
        case AnnotationTool::Highlight:
        case AnnotationTool::Underline:
        case AnnotationTool::StrikeOut: {
            a.type = (m_tool == AnnotationTool::Highlight) ? AnnotationType::Highlight
                   : (m_tool == AnnotationTool::Underline) ? AnnotationType::Underline
                                                           : AnnotationType::StrikeOut;
            if (m_textSelection) {
                a.quads = m_textSelection(m_dragStartDoc, end, m_page);
            }
            if (a.quads.empty()) {
                if (a.bounds.width() < 1.0 && a.bounds.height() < 1.0) {
                    update();
                    return;
                }
                a.quads = {a.bounds};
            } else {
                QRectF bbox = a.quads.front();
                for (const QRectF& r : a.quads) bbox = bbox.united(r);
                a.bounds = bbox;
            }
            break;
        }
        default:
            m_inkPoints.clear();
            update();
            return;
    }

    if (a.type != AnnotationType::Ink &&
        a.bounds.width() < 1.0 && a.bounds.height() < 1.0) {
        update();
        return;
    }

    const int id = m_store->add(std::move(a));
    emit annotationCommitted(id);
    update();
}

}  // namespace trailer
