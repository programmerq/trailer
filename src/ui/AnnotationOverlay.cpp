#include "AnnotationOverlay.h"

#include "annotation/AnnotationStore.h"

#include <QContextMenuEvent>
#include <QEvent>
#include <QFont>
#include <QFrame>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QResizeEvent>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

namespace trailer {

AnnotationOverlay::AnnotationOverlay(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setMouseTracking(true);
    // ClickFocus makes the overlay accept keyboard focus when the
    // user clicks on an annotation. Without this, Delete / arrow
    // nudge events would never reach keyPressEvent because focus
    // would have stayed on whatever the previous widget was (PDF
    // viewport, sidebar, etc.).
    setFocusPolicy(Qt::ClickFocus);
    m_docToView  = [](QPointF p, int /*page*/) { return p; };
    m_viewToDoc  = [](QPointF p, int /*page*/) { return p; };
    m_pageAtView = [this](QPointF) { return m_page; };
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
    const Qt::CursorShape shape =
        tool == AnnotationTool::None   ? Qt::ArrowCursor
        : tool == AnnotationTool::Select ? Qt::IBeamCursor
                                         : Qt::CrossCursor;
    setCursor(shape);
    if (tool != AnnotationTool::Select) {
        m_pendingSelection.clear();
    }
    if (tool != AnnotationTool::Text) {
        // Drop the FormToolbar glyph preset when the user moves off the
        // Text tool so a later plain "Text" selection isn't unexpectedly
        // skipping the dialog.
        m_pendingTextPreset.clear();
    }
    if (tool != AnnotationTool::Signature) {
        // Drop the saved-signature PNG pointer when the user switches
        // away — otherwise a later Signature selection could surprise
        // them with a stale image.
        m_pendingSignaturePath.clear();
    }
    update();
}

void AnnotationOverlay::setStyle(const AnnotationStyle& style) {
    m_style = style;
}

void AnnotationOverlay::setPage(int page) {
    if (m_page == page) return;
    m_page = page;
    update();
}

void AnnotationOverlay::setDocumentToView(DocToView fn) {
    m_docToView = std::move(fn);
    update();
}

void AnnotationOverlay::setViewToDocument(ViewToDoc fn) {
    m_viewToDoc = std::move(fn);
}

void AnnotationOverlay::setPageAtViewPoint(PageAtView fn) {
    m_pageAtView = std::move(fn);
}

void AnnotationOverlay::setTextSelectionProvider(TextSelectionProvider fn) {
    m_textSelection = std::move(fn);
}

void AnnotationOverlay::setSourceSampler(SourceSampler fn) {
    m_sourceSampler = std::move(fn);
    update();
}

QRectF AnnotationOverlay::docRectToView(const QRectF& r, int page) const {
    const QPointF tl = m_docToView(r.topLeft(), page);
    const QPointF br = m_docToView(r.bottomRight(), page);
    return QRectF(tl, br).normalized();
}

QPointF AnnotationOverlay::toDoc(const QPointF& viewPt, int page) const {
    return m_viewToDoc(viewPt, page);
}

int AnnotationOverlay::pageAt(const QPointF& viewPt) const {
    if (m_pageAtView) {
        const int p = m_pageAtView(viewPt);
        if (p >= 0) return p;
    }
    return m_page;
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
        const int page = a.page;
        const QRectF viewRect = docRectToView(a.bounds, page);
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
                const QPointF a0 = m_docToView(a.points[0], page);
                const QPointF a1 = m_docToView(a.points[1], page);
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
                QPainterPath path(m_docToView(a.points[0], page));
                for (size_t i = 1; i < a.points.size(); ++i) {
                    path.lineTo(m_docToView(a.points[i], page));
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
                const QPointF tl = m_docToView(a.bounds.topLeft(), page);
                const QRectF icon(tl.x(), tl.y(), 18.0, 18.0);
                const QColor noteColour = a.style.fill.alpha() > 0
                    ? a.style.fill : QColor(255, 225, 120);
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
            case AnnotationType::Redaction: {
                // Provisional preview — rendered as a flat black block.
                // On save the underlying page region is raster-flattened
                // so the glyph content is actually destroyed (§6.11.6).
                p.fillRect(viewRect, Qt::black);
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
                    const QPointF tail = m_docToView(a.points.front(), page);
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
            case AnnotationType::Signature: {
                if (a.imagePath.isEmpty() || viewRect.isEmpty()) break;
                const std::string key = a.imagePath.toStdString();
                auto it = m_signatureCache.find(key);
                if (it == m_signatureCache.end()) {
                    QImage img(a.imagePath);
                    if (img.isNull()) break;
                    it = m_signatureCache.emplace(key, std::move(img)).first;
                }
                const QImage& img = it->second;
                if (img.isNull()) break;
                // Fit the PNG into the annotation rect while preserving
                // aspect ratio. Signatures usually come out tall-and-wide
                // rather than square, so centering keeps the ink where
                // the user dragged.
                const double srcAspect =
                    static_cast<double>(img.width()) / std::max(1, img.height());
                const double dstAspect = viewRect.width() / std::max(0.001, viewRect.height());
                QRectF target = viewRect;
                if (srcAspect > dstAspect) {
                    const double h = viewRect.width() / srcAspect;
                    target = QRectF(viewRect.left(),
                                    viewRect.top() + (viewRect.height() - h) / 2.0,
                                    viewRect.width(), h);
                } else {
                    const double w = viewRect.height() * srcAspect;
                    target = QRectF(viewRect.left() + (viewRect.width() - w) / 2.0,
                                    viewRect.top(),
                                    w, viewRect.height());
                }
                p.drawImage(target, img);
                break;
            }
            case AnnotationType::Highlight:
            case AnnotationType::Underline:
            case AnnotationType::StrikeOut: {
                const std::vector<QRectF>& rects =
                    a.quads.empty() ? std::vector<QRectF>{a.bounds} : a.quads;
                for (const QRectF& r : rects) {
                    const QRectF vr = docRectToView(r, page);
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
        drawOne(a);
    }

    // Selection affordance for the active annotation: a thin blue
    // dashed rectangle just outside the bounds. Drawn on top of the
    // shape so it sits above any fill / stroke. Visible whenever an
    // annotation is selected, regardless of the active tool — the
    // user can always see what would receive Delete or arrow nudges.
    if (m_selectedAnnotationId != 0 && m_store) {
        if (const Annotation* sel = m_store->find(m_selectedAnnotationId)) {
            const QRectF view = docRectToView(sel->bounds, sel->page);
            const QRectF inflated = view.adjusted(-3, -3, 3, 3);
            QPen pen(QColor(60, 120, 220, 220));
            pen.setWidth(1);
            pen.setStyle(Qt::DashLine);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawRect(inflated);
        }
    }

    if (m_tool == AnnotationTool::Select && !m_pendingSelection.empty()) {
        QColor selFill(80, 140, 220, 110);
        for (const QRectF& r : m_pendingSelection) {
            p.fillRect(docRectToView(r, m_page), selFill);
        }
    }

    if (m_dragging) {
        Annotation preview;
        preview.page = m_dragPage;
        preview.type = [this]() {
            switch (m_tool) {
                case AnnotationTool::Ellipse:        return AnnotationType::Ellipse;
                case AnnotationTool::Line:           return AnnotationType::Line;
                case AnnotationTool::Arrow:          return AnnotationType::Arrow;
                case AnnotationTool::Ink:            return AnnotationType::Ink;
                case AnnotationTool::HighlightShape: return AnnotationType::HighlightShape;
                case AnnotationTool::SpeechBubble:   return AnnotationType::SpeechBubble;
                case AnnotationTool::ZoomLens:       return AnnotationType::ZoomLens;
                case AnnotationTool::Signature:      return AnnotationType::Signature;
                case AnnotationTool::Redaction:      return AnnotationType::Redaction;
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
        } else if (preview.type == AnnotationType::Signature) {
            preview.imagePath = m_pendingSignaturePath;
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
    if (m_inlineEditor && m_inlineEditorAnnotationId != 0) {
        if (auto* edit = qobject_cast<QPlainTextEdit*>(obj)) {
            // Three terminations to handle:
            //   commit  — Ctrl+Enter or focus loss → write text back
            //   cancel  — Escape → discard the edit
            // For a freshly-placed annotation, both "cancel" and
            // "commit with empty text" remove the placeholder so the
            // user is not left with an invisible stamp on the page.
            auto finish = [this, edit](bool commitText) {
                if (!m_store) return;
                const int id = m_inlineEditorAnnotationId;
                const Annotation* a = m_store->find(id);
                if (!a) return;
                const QString typed = edit->toPlainText();
                if (m_inlineEditorIsNew &&
                    (typed.isEmpty() || !commitText)) {
                    // Nothing typed (or the user pressed Esc on a
                    // fresh annotation): remove the placeholder.
                    m_store->remove(id);
                } else if (commitText) {
                    Annotation updated = *a;
                    updated.text = typed;
                    m_store->update(updated);
                }
                // Edits to existing annotations that the user
                // cancelled simply leave the original text in place.
            };
            if (event->type() == QEvent::KeyPress) {
                auto* key = static_cast<QKeyEvent*>(event);
                const bool commit = (key->key() == Qt::Key_Return ||
                                     key->key() == Qt::Key_Enter) &&
                                    (key->modifiers() & Qt::ControlModifier);
                const bool cancel = key->key() == Qt::Key_Escape;
                if (commit) {
                    finish(/*commitText=*/true);
                    m_inlineEditorIsNew = false;
                    m_inlineEditor->deleteLater();
                    return true;
                }
                if (cancel) {
                    finish(/*commitText=*/false);
                    m_inlineEditorIsNew = false;
                    m_inlineEditor->deleteLater();
                    return true;
                }
            } else if (event->type() == QEvent::FocusOut) {
                finish(/*commitText=*/true);
                m_inlineEditorIsNew = false;
                m_inlineEditor->deleteLater();
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}

void AnnotationOverlay::mousePressEvent(QMouseEvent* event) {
    if (!m_store || m_tool == AnnotationTool::None) {
        event->ignore();
        return;
    }
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    // Select-tool press has two behaviours depending on what the user
    // clicked: hitting an existing annotation selects (and may begin
    // a move drag) it; hitting empty space clears any selection and
    // falls through to text-selection. Selection is sticky between
    // clicks until the user clicks empty space or invokes Esc /
    // Delete.
    if (m_tool == AnnotationTool::Select) {
        const int hitId = hitTest(event->position());
        if (hitId != 0) {
            const bool wasAlreadySelected = (m_selectedAnnotationId == hitId);
            if (!wasAlreadySelected) {
                m_selectedAnnotationId = hitId;
                emit selectionChanged(hitId);
            }
            m_pendingSelection.clear();
            if (wasAlreadySelected && m_store) {
                if (const Annotation* a = m_store->find(hitId)) {
                    // Begin a move-drag: track the press point and
                    // the original bounds so mouseMoveEvent can
                    // translate without accumulating drift.
                    m_movingSelected = true;
                    m_dragPage = a->page;
                    m_moveStartDoc = toDoc(event->position(), a->page);
                    m_moveOriginalBounds = a->bounds;
                }
            }
            setFocus(Qt::MouseFocusReason);  // accept Delete / arrow keys
            update();
            return;
        }
        // Empty-space click: clear any annotation selection and let
        // the text-selection drag below run.
        if (m_selectedAnnotationId != 0) {
            m_selectedAnnotationId = 0;
            emit selectionChanged(0);
            update();
        }
        m_pendingSelection.clear();
    }
    m_dragPage = pageAt(event->position());
    m_dragStartDoc = toDoc(event->position(), m_dragPage);
    m_dragCurrentDoc = m_dragStartDoc;
    m_dragging = true;
    m_inkPoints.clear();
    if (m_tool == AnnotationTool::Ink) {
        m_inkPoints.push_back(m_dragStartDoc);
    }
    update();
}

void AnnotationOverlay::mouseMoveEvent(QMouseEvent* event) {
    // Move-drag for the selected annotation runs alongside the
    // shape-creation drag tracked by m_dragging. We translate the
    // annotation's bounds in document space and emit an in-place
    // update; the store's changed() signal repaints automatically.
    if (m_movingSelected && m_store && m_selectedAnnotationId != 0) {
        const QPointF here = toDoc(event->position(), m_dragPage);
        const QPointF delta = here - m_moveStartDoc;
        if (const Annotation* a = m_store->find(m_selectedAnnotationId)) {
            Annotation updated = *a;
            updated.bounds = m_moveOriginalBounds.translated(delta);
            // Translate auxiliary point lists too (Line/Arrow
            // endpoints, Ink polyline) so moving doesn't snap back
            // to the original anchors.
            for (QPointF& p : updated.points) {
                p += delta;
            }
            m_store->update(updated);
        }
        update();
        return;
    }
    if (!m_dragging) return;
    m_dragCurrentDoc = toDoc(event->position(), m_dragPage);
    if (m_tool == AnnotationTool::Ink) {
        m_inkPoints.push_back(m_dragCurrentDoc);
    }
    update();
}

void AnnotationOverlay::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    // End-of-move bookkeeping: the bounds were updated incrementally
    // in mouseMoveEvent; release just clears the drag state. The
    // already-emitted store changed() signals took care of paint
    // and dirty propagation.
    if (m_movingSelected) {
        m_movingSelected = false;
        update();
        return;
    }
    if (!m_dragging) return;
    m_dragging = false;
    const QPointF end = toDoc(event->position(), m_dragPage);

    if (m_tool == AnnotationTool::Select) {
        m_pendingSelection.clear();
        if (m_textSelection) {
            m_pendingSelection = m_textSelection(m_dragStartDoc, end, m_dragPage);
        }
        m_page = m_dragPage;
        update();
        return;
    }

    Annotation a;
    a.page = m_dragPage;
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
            if (!m_pendingTextPreset.isEmpty()) {
                // FormToolbar path: drop a pre-set glyph (✓ / ✗)
                // without prompting. Use a small square so the glyph
                // sits where the user clicked rather than filling a
                // wide multi-line box.
                if (rect.width() < 10.0 || rect.height() < 10.0) {
                    rect = QRectF(m_dragStartDoc - QPointF(10.0, 10.0),
                                  QSizeF(24.0, 24.0));
                }
                a.type = AnnotationType::Text;
                a.bounds = rect;
                a.text = m_pendingTextPreset;
                break;
            }
            if (rect.width() < 40.0 || rect.height() < 20.0) {
                rect = QRectF(m_dragStartDoc, QSizeF(200.0, 40.0));
            }
            // Drop an empty placeholder and focus an inline editor
            // anchored at the rect. Modal QInputDialog is gone — the
            // user types directly into the document. Ctrl+Enter or
            // focus loss commits; Esc removes the placeholder
            // (handled in eventFilter via the m_inlineEditorIsNew
            // flag set below).
            a.type = AnnotationType::Text;
            a.bounds = rect;
            a.text = QString();
            const int newId = m_store->add(std::move(a));
            m_inlineEditorIsNew = true;
            openInlineEditor(newId);
            update();
            return;
        }
        case AnnotationTool::Note: {
            // Same inline-editor pattern as Text. Note keeps a tiny
            // 18×18 bounds in doc space (the rendered icon size); the
            // inline editor frame is sized in view space anchored at
            // the click so the user has room to type.
            a.type = AnnotationType::Note;
            a.bounds = QRectF(m_dragStartDoc, QSizeF(18.0, 18.0));
            a.text = QString();
            const int newId = m_store->add(std::move(a));
            m_inlineEditorIsNew = true;
            openInlineEditor(newId);
            update();
            return;
        }
        case AnnotationTool::HighlightShape:
            a.type = AnnotationType::HighlightShape;
            break;
        case AnnotationTool::Redaction:
            a.type = AnnotationType::Redaction;
            break;
        case AnnotationTool::SpeechBubble: {
            QRectF rect = a.bounds;
            if (rect.width() < 40.0 || rect.height() < 20.0) {
                rect = QRectF(m_dragStartDoc, QSizeF(200.0, 80.0));
            }
            a.type = AnnotationType::SpeechBubble;
            a.bounds = rect;
            a.text = QString();
            const QPointF tail(rect.left() - 20.0, rect.bottom() + 30.0);
            a.points = {tail};
            const int newId = m_store->add(std::move(a));
            m_inlineEditorIsNew = true;
            openInlineEditor(newId);
            update();
            return;
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
        case AnnotationTool::Signature: {
            if (m_pendingSignaturePath.isEmpty()) {
                // No signature picked — silently ignore the drag rather
                // than dropping an empty stamp.
                update();
                return;
            }
            QRectF rect = a.bounds;
            if (rect.width() < 10.0 || rect.height() < 10.0) {
                // Treat a click (no drag) as "drop at natural size
                // centred on the click". 160×60 doc units is roughly
                // the aspect of a typed-signature capture — the render
                // code recentres if the PNG is taller or wider.
                const QSizeF defSize(160.0, 60.0);
                rect = QRectF(m_dragStartDoc - QPointF(defSize.width() / 2.0,
                                                      defSize.height() / 2.0),
                              defSize);
            }
            a.type = AnnotationType::Signature;
            a.bounds = rect;
            a.imagePath = m_pendingSignaturePath;
            break;
        }
        case AnnotationTool::Highlight:
        case AnnotationTool::Underline:
        case AnnotationTool::StrikeOut: {
            a.type = (m_tool == AnnotationTool::Highlight) ? AnnotationType::Highlight
                   : (m_tool == AnnotationTool::Underline) ? AnnotationType::Underline
                                                           : AnnotationType::StrikeOut;
            if (m_textSelection) {
                a.quads = m_textSelection(m_dragStartDoc, end, m_dragPage);
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

int AnnotationOverlay::hitTest(const QPointF& viewPt) const {
    if (!m_store) return 0;
    const int page = (m_pageAtView ? m_pageAtView(viewPt) : m_page);
    const auto& all = m_store->annotations();
    for (auto it = all.rbegin(); it != all.rend(); ++it) {
        if (page >= 0 && it->page != page) continue;
        if (docRectToView(it->bounds, it->page).contains(viewPt)) {
            return it->id;
        }
    }
    return 0;
}

void AnnotationOverlay::openInlineEditor(int annotationId) {
    if (!m_store) return;
    const Annotation* a = m_store->find(annotationId);
    if (!a) return;
    if (m_inlineEditor) {
        m_inlineEditor->deleteLater();
        m_inlineEditor = nullptr;
    }
    if (a->type != AnnotationType::Text &&
        a->type != AnnotationType::Note &&
        a->type != AnnotationType::SpeechBubble) {
        return;
    }

    auto* frame = new QFrame(this);
    frame->setFrameShape(QFrame::Box);
    frame->setAutoFillBackground(true);
    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(2, 2, 2, 2);
    auto* edit = new QPlainTextEdit(frame);
    edit->setPlainText(a->text);
    QFont f = edit->font();
    if (!a->style.fontFamily.isEmpty()) f.setFamily(a->style.fontFamily);
    f.setPointSize(a->style.fontPointSize > 0 ? a->style.fontPointSize : 12);
    edit->setFont(f);
    layout->addWidget(edit);

    QRect frameRect;
    if (a->type == AnnotationType::Note) {
        // Note bounds are a tiny 18×18 doc-space icon — give the
        // popover a fixed view-space size anchored at the top-left
        // of the icon so the user has room to type.
        const QPointF tl = docRectToView(a->bounds, a->page).topLeft();
        frameRect = QRect(tl.toPoint(), QSize(220, 96));
    } else {
        frameRect = docRectToView(a->bounds, a->page).toRect();
    }
    frame->setGeometry(frameRect);
    frame->show();
    edit->setFocus();
    edit->moveCursor(QTextCursor::End);

    m_inlineEditor = frame;
    m_inlineEditorAnnotationId = annotationId;

    connect(edit, &QPlainTextEdit::destroyed, this, [this]() {
        m_inlineEditor = nullptr;
        m_inlineEditorAnnotationId = 0;
    });

    edit->installEventFilter(this);
}

void AnnotationOverlay::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    const int id = hitTest(event->position());
    if (id == 0) {
        event->ignore();
        return;
    }
    openInlineEditor(id);
}

void AnnotationOverlay::keyPressEvent(QKeyEvent* event) {
    if (m_selectedAnnotationId == 0 || !m_store) {
        event->ignore();
        return;
    }
    // Delete / Backspace removes the selected annotation. The
    // store's changed() signal repaints automatically.
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        m_store->remove(m_selectedAnnotationId);
        m_selectedAnnotationId = 0;
        emit selectionChanged(0);
        update();
        return;
    }
    // Arrow keys nudge by 1 doc-space point (Shift = 10 pt). This
    // is the keyboard equivalent of the drag-to-move gesture and
    // gives the user precise positioning without touching the
    // mouse.
    const double step = (event->modifiers() & Qt::ShiftModifier) ? 10.0 : 1.0;
    switch (event->key()) {
        case Qt::Key_Left:  nudgeSelected(-step, 0.0); return;
        case Qt::Key_Right: nudgeSelected(+step, 0.0); return;
        case Qt::Key_Up:    nudgeSelected(0.0, -step); return;
        case Qt::Key_Down:  nudgeSelected(0.0, +step); return;
        default: break;
    }
    event->ignore();
}

void AnnotationOverlay::nudgeSelected(double dx, double dy) {
    if (m_selectedAnnotationId == 0 || !m_store) return;
    const Annotation* a = m_store->find(m_selectedAnnotationId);
    if (!a) return;
    Annotation updated = *a;
    updated.bounds.translate(dx, dy);
    for (QPointF& p : updated.points) {
        p.rx() += dx;
        p.ry() += dy;
    }
    m_store->update(updated);
    update();
}

void AnnotationOverlay::contextMenuEvent(QContextMenuEvent* event) {
    if (!m_store || m_tool != AnnotationTool::Select ||
        m_pendingSelection.empty()) {
        event->ignore();
        return;
    }
    QMenu menu(this);
    QAction* hi = menu.addAction(tr("Highlight"));
    QAction* un = menu.addAction(tr("Underline"));
    QAction* st = menu.addAction(tr("Strikeout"));
    QAction* chosen = menu.exec(event->globalPos());
    if (!chosen) return;

    Annotation a;
    a.page = m_dragPage;
    a.style = m_style;
    a.quads = m_pendingSelection;
    QRectF bbox = a.quads.front();
    for (const QRectF& r : a.quads) bbox = bbox.united(r);
    a.bounds = bbox;
    if      (chosen == hi) a.type = AnnotationType::Highlight;
    else if (chosen == un) a.type = AnnotationType::Underline;
    else if (chosen == st) a.type = AnnotationType::StrikeOut;
    else return;

    const int id = m_store->add(std::move(a));
    emit annotationCommitted(id);
    m_pendingSelection.clear();
    update();
}

}  // namespace trailer
