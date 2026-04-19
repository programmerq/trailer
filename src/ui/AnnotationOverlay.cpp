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

    auto drawOne = [&](const Annotation& a) {
        QPen pen(a.style.stroke);
        pen.setWidthF(a.style.strokeWidth);
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
        preview.type = [this]() {
            switch (m_tool) {
                case AnnotationTool::Ellipse: return AnnotationType::Ellipse;
                case AnnotationTool::Line:    return AnnotationType::Line;
                case AnnotationTool::Arrow:   return AnnotationType::Arrow;
                case AnnotationTool::Ink:     return AnnotationType::Ink;
                default:                      return AnnotationType::Rectangle;
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
