#include "AnnotationOverlay.h"

#include "SamController.h"
#include "annotation/AnnotationStore.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QEvent>
#include <QEventPoint>
#include <QFont>
#include <QGuiApplication>
#include <QFrame>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QTabletEvent>
#include <QResizeEvent>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

namespace trailer {

AnnotationOverlay::AnnotationOverlay(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setMouseTracking(true);
    // Tablet tracking lets the Ink tool receive QTabletEvents from
    // a Wacom / Surface pen; otherwise Qt funnels stylus input
    // through QMouseEvent and we lose pressure resolution.
    setTabletTracking(true);
    // ClickFocus makes the overlay accept keyboard focus when the
    // user clicks on an annotation. Without this, Delete / arrow
    // nudge events would never reach keyPressEvent because focus
    // would have stayed on whatever the previous widget was (PDF
    // viewport, sidebar, etc.).
    setFocusPolicy(Qt::ClickFocus);
    m_docToView = [](QPointF p, int /*page*/) { return p; };
    m_viewToDoc = [](QPointF p, int /*page*/) { return p; };
    m_pageAtView = [this](QPointF) { return m_page; };

    // Cmd-Tab / app-deactivate aborts any in-flight drag. Without
    // this, the user could start a Zoom Lens drag, switch to
    // another app, come back, and the drag state would still be
    // live — the next mousePress would treat the lingering
    // m_dragStartDoc as the start of a phantom drag whose release
    // bypasses normal undo bookkeeping.
    connect(qApp, &QGuiApplication::applicationStateChanged, this,
            [this](Qt::ApplicationState state) {
                if (state != Qt::ApplicationActive) {
                    abortInFlightDrag();
                }
            });
}

void AnnotationOverlay::abortInFlightDrag() {
    // A crop drag (draw / handle-resize / move) has no AnnotationStore
    // compound to close — just drop the in-flight flags so a return
    // from Cmd-Tab doesn't resume a phantom crop gesture. The already-
    // drawn pending rect (if any) is left intact so the user comes back
    // to what they had.
    if (m_cropDrawing || m_cropHandle != ResizeHandle::None || m_cropMoving) {
        m_cropDrawing = false;
        m_cropHandle = ResizeHandle::None;
        m_cropMoving = false;
        update();
    }
    if (!m_dragging && !m_movingSelected && m_resizingHandle == ResizeHandle::None) {
        return;
    }
    const bool hadCompound = m_movingSelected || m_resizingHandle != ResizeHandle::None;
    m_dragging = false;
    m_movingSelected = false;
    m_resizingHandle = ResizeHandle::None;
    m_inkPoints.clear();
    // If the drag was a move or resize, the store has an open
    // compound gesture that must be closed so the next user action
    // doesn't merge into the abandoned gesture's undo frame. The
    // pre-gesture snapshot already on the undo stack stays — undo
    // restores it as if the drag had been cancelled cleanly.
    if (hadCompound && m_store) {
        m_store->endCompound();
    }
    update();
}

void AnnotationOverlay::setStore(AnnotationStore *store) {
    if (m_store == store)
        return;
    if (m_store)
        disconnect(m_store, nullptr, this, nullptr);
    m_store = store;
    if (m_store) {
        connect(m_store, &AnnotationStore::changed, this, QOverload<>::of(&QWidget::update));
    }
    update();
}

void AnnotationOverlay::setActiveTool(AnnotationTool tool) {
    const AnnotationTool previous = m_tool;
    m_tool = tool;
    const bool interactive = tool != AnnotationTool::None;
    setAttribute(Qt::WA_TransparentForMouseEvents, !interactive);
    // Select-tool cursor used to be an unconditional I-beam over the
    // document area, which lied to the user on raster-text images
    // where nothing was selectable. The honest I-beam now lives on
    // SelectableTextLayer (which sits beneath the overlay): it shows
    // only over actual cached text blocks. For Select here, fall back
    // to the arrow so SelectableTextLayer's cursor wins under the
    // pointer. For drawing tools, the cross cursor still applies.
    const Qt::CursorShape shape = tool == AnnotationTool::None     ? Qt::ArrowCursor
                                  : tool == AnnotationTool::Select ? Qt::ArrowCursor
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
    if (previous == AnnotationTool::CropRect && tool != AnnotationTool::CropRect) {
        // Leaving the crop tool abandons any un-committed crop
        // rectangle + its dimmed preview so a later re-activation
        // starts clean.
        clearPendingCrop();
    }
    // SAM tool lifecycle. Activating a SAM tool kicks off a prepare
    // pass (if not cached); deactivating drops any pending prompts
    // and the preview mask so a later re-activation starts clean.
    if (isSamTool()) {
        if (previous != tool) {
            resetSamState();
        }
    } else if (previous == AnnotationTool::InstantAlpha ||
               previous == AnnotationTool::SmartLasso) {
        m_samMask = QImage();
        m_samPositives.clear();
        m_samNegatives.clear();
        m_samDraggingInstant = false;
        m_samPreparing = false;
        if (m_samController) {
            m_samController->cancelAll();
        }
    }
    update();
}

void AnnotationOverlay::setStyle(const AnnotationStyle &style) {
    m_style = style;
}

void AnnotationOverlay::setPage(int page) {
    if (m_page == page)
        return;
    m_page = page;
    update();
}

void AnnotationOverlay::setSearchHighlights(std::vector<SearchHighlight> highlights) {
    m_searchHighlights = std::move(highlights);
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

void AnnotationOverlay::setTextSelectionTextProvider(TextSelectionTextProvider fn) {
    m_textSelectionText = std::move(fn);
}

void AnnotationOverlay::setPointOverTextProvider(PointOverTextProvider fn) {
    m_pointOverText = std::move(fn);
}

void AnnotationOverlay::setSourceSampler(SourceSampler fn) {
    m_sourceSampler = std::move(fn);
    update();
}

void AnnotationOverlay::setSamController(SamController *controller) {
    m_samController = controller;
}

void AnnotationOverlay::setSamImageProvider(ImageProvider fn) {
    m_samImageProvider = std::move(fn);
}

void AnnotationOverlay::setInstantAlphaCommitHandler(InstantAlphaCommit fn) {
    m_instantAlphaCommit = std::move(fn);
}

void AnnotationOverlay::setSmartLassoCommitHandler(SmartLassoCommit fn) {
    m_smartLassoCommit = std::move(fn);
}

bool AnnotationOverlay::isSamToolActiveForTest() const {
    return isSamTool();
}

bool AnnotationOverlay::simulateSamPromptForTest(QPointF docPoint, bool positive) {
    if (!isSamTool() || !m_samController)
        return false;
    const QPoint p(static_cast<int>(docPoint.x()), static_cast<int>(docPoint.y()));
    if (m_tool == AnnotationTool::InstantAlpha) {
        m_samPositives = {p};
        m_samNegatives.clear();
    } else {
        if (positive)
            m_samPositives.append(p);
        else
            m_samNegatives.append(p);
    }
    requestSamPreview();
    return true;
}

void AnnotationOverlay::resetSamState() {
    m_samPositives.clear();
    m_samNegatives.clear();
    m_samMask = QImage();
    m_samDraggingInstant = false;
    m_samPreparing = false;
    if (!m_samController || !m_samImageProvider) {
        update();
        return;
    }
    const QImage source = m_samImageProvider();
    if (source.isNull()) {
        update();
        return;
    }
    // Encoder runs once per image. Cache-hit short-circuits to a
    // synchronous "prepared=true"; cache-miss queues an MlScheduler
    // task at UserAction priority. The cursor briefly turns into a
    // wait shape so the user knows the first click won't fire until
    // prepare lands.
    if (!m_samController->isModelReady()) {
        // Toolbar guards on this, but be defensive — no popup, just
        // no-op.
        update();
        return;
    }
    if (m_samController->isCachedForActive(source)) {
        // Already prepared for this (doc, page, hash) — proceed
        // immediately. SamController::prepareForActive will short-
        // circuit but going through it bumps the LRU.
        m_samPreparing = false;
    } else {
        m_samPreparing = true;
        setCursor(Qt::BusyCursor);
    }
    QPointer<AnnotationOverlay> self(this);
    m_samController->prepareForActive(source, [self](bool /*ok*/) {
        if (!self)
            return;
        self->m_samPreparing = false;
        // Restore the SAM-tool cursor. A null mask means the encoder
        // failed (model error or cancellation); we still pull the
        // cursor back so the user isn't stuck in a wait shape.
        self->setCursor(self->isSamTool() ? Qt::CrossCursor : Qt::ArrowCursor);
        self->update();
    });
    update();
}

void AnnotationOverlay::requestSamPreview() {
    if (!m_samController)
        return;
    if (m_samPositives.isEmpty() && m_samNegatives.isEmpty())
        return;
    QPointer<AnnotationOverlay> self(this);
    m_samController->requestSegment(m_samPositives, m_samNegatives,
                                    [self](const QImage &mask) {
                                        if (!self)
                                            return;
                                        // PHILOSOPHY: a null/empty mask
                                        // is silent — no popup. The
                                        // user is mid-drag; their next
                                        // click might land somewhere
                                        // that works.
                                        self->m_samMask = mask;
                                        self->update();
                                    });
}

void AnnotationOverlay::commitInstantAlpha() {
    if (!m_samController || !m_samImageProvider || !m_instantAlphaCommit) {
        resetSamState();
        return;
    }
    const QImage source = m_samImageProvider();
    if (source.isNull() || m_samMask.isNull()) {
        // PHILOSOPHY: no popup; the user can re-click and try again.
        m_samMask = QImage();
        update();
        return;
    }
    // Render the alpha-cut on the GUI thread — the mask lives in the
    // controller's session, but applyAsAlpha is read-only and cheap.
    QImage result = source.convertToFormat(QImage::Format_ARGB32);
    if (result.size() != m_samMask.size()) {
        // Mask vs image mismatch — usually means the user clicked
        // before the prepare landed. Silently drop the commit; the
        // status bar already flashed prepare's progress chip.
        return;
    }
    for (int y = 0; y < result.height(); ++y) {
        auto *dst = reinterpret_cast<QRgb *>(result.scanLine(y));
        const uchar *msk = m_samMask.constScanLine(y);
        for (int x = 0; x < result.width(); ++x) {
            const QRgb px = dst[x];
            dst[x] = qRgba(qRed(px), qGreen(px), qBlue(px), msk[x]);
        }
    }
    m_instantAlphaCommit(result);
    // Drop the prompt + mask so the next drag starts fresh; keep the
    // prepared encoder cached.
    m_samPositives.clear();
    m_samNegatives.clear();
    m_samMask = QImage();
    m_samDraggingInstant = false;
    update();
}

void AnnotationOverlay::commitSmartLasso() {
    if (!m_samController || !m_smartLassoCommit) {
        resetSamState();
        return;
    }
    const QPolygon poly = m_samController->lastContour();
    if (poly.isEmpty()) {
        // PHILOSOPHY: no popup. Smart Lasso needs at least one
        // positive prompt to produce a polygon.
        return;
    }
    m_smartLassoCommit(poly);
    m_samPositives.clear();
    m_samNegatives.clear();
    m_samMask = QImage();
    update();
}

QRectF AnnotationOverlay::docRectToView(const QRectF &r, int page) const {
    const QPointF tl = m_docToView(r.topLeft(), page);
    const QPointF br = m_docToView(r.bottomRight(), page);
    return QRectF(tl, br).normalized();
}

QPointF AnnotationOverlay::toDoc(const QPointF &viewPt, int page) const {
    return m_viewToDoc(viewPt, page);
}

int AnnotationOverlay::pageAt(const QPointF &viewPt) const {
    if (m_pageAtView) {
        const int p = m_pageAtView(viewPt);
        if (p >= 0)
            return p;
    }
    return m_page;
}

void AnnotationOverlay::paintEvent(QPaintEvent * /*event*/) {
    if (!m_store)
        return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // Search-match highlights live behind user annotations so the
    // user can mark up on top of a found match without losing the
    // visual cue underneath. Siblings get a soft highlighter-yellow
    // wash; the current match gets a brighter wash with a thin
    // outline so the user can spot it during Find Next / Previous
    // without scanning the whole page. The doc-to-view transform is
    // the same one annotations use, so highlights track zoom and
    // scroll without any extra plumbing.
    if (!m_searchHighlights.empty() && m_docToView) {
        const QColor siblingFill(255, 235, 50, 90);
        const QColor currentFill(255, 200, 0, 170);
        const QColor currentBorder(180, 130, 0, 220);
        for (const SearchHighlight &h : m_searchHighlights) {
            if (h.rect.isEmpty())
                continue;
            const QRectF viewRect = docRectToView(h.rect, h.page);
            if (viewRect.isEmpty())
                continue;
            p.setPen(h.isCurrent ? QPen(currentBorder, 1.0) : Qt::NoPen);
            p.setBrush(h.isCurrent ? currentFill : siblingFill);
            p.drawRect(viewRect);
        }
        // Reset painter brush so annotation-drawing code below isn't
        // surprised by leftover state.
        p.setBrush(Qt::NoBrush);
    }

    auto applyDash = [](QPen &pen, DashStyle d) {
        switch (d) {
        case DashStyle::Solid:
            pen.setStyle(Qt::SolidLine);
            break;
        case DashStyle::Dashed:
            pen.setStyle(Qt::DashLine);
            break;
        case DashStyle::Dotted:
            pen.setStyle(Qt::DotLine);
            break;
        }
    };

    auto drawOne = [&](const Annotation &a) {
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
            if (a.points.size() < 2)
                break;
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
            if (a.points.size() < 2)
                break;
            // When per-sample pressure was captured, draw each
            // segment with a width derived from its pressure so
            // a stylus / Force Touch trackpad stroke shows the
            // hand's variation. Without pressure, fall through
            // to a single QPainterPath for cheaper rendering.
            if (!a.pressures.empty() && a.pressures.size() == a.points.size()) {
                const qreal base = a.style.strokeWidth > 0.0 ? a.style.strokeWidth : 1.5;
                for (size_t i = 1; i < a.points.size(); ++i) {
                    const qreal pr = std::clamp<qreal>(static_cast<qreal>(a.pressures[i]),
                                                       0.0, 1.0);
                    // Same cubic curve as SignatureCanvas so a
                    // light touch is light and a heavy touch is
                    // confidently thick. base is the minimum.
                    const qreal shaped = pr * pr * pr;
                    const qreal w = base + shaped * 5.0;
                    QPen segPen(a.style.stroke, w, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
                    p.setPen(segPen);
                    p.drawLine(m_docToView(a.points[i - 1], page), m_docToView(a.points[i], page));
                }
                break;
            }
            QPainterPath path(m_docToView(a.points[0], page));
            for (size_t i = 1; i < a.points.size(); ++i) {
                path.lineTo(m_docToView(a.points[i], page));
            }
            p.drawPath(path);
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
            p.drawText(viewRect, Qt::AlignLeft | Qt::TextWordWrap, a.text);
            break;
        }
        case AnnotationType::Note: {
            const QPointF tl = m_docToView(a.bounds.topLeft(), page);
            const QRectF icon(tl.x(), tl.y(), 18.0, 18.0);
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
        case AnnotationType::Redaction: {
            // Provisional preview — rendered as a flat black block.
            // On save the underlying page region is raster-flattened
            // so the glyph content is actually destroyed (§6.11.6).
            p.fillRect(viewRect, Qt::black);
            break;
        }
        case AnnotationType::HighlightShape: {
            QColor fill = a.style.fill.alpha() > 0 ? a.style.fill : a.style.stroke;
            fill.setAlpha(a.style.fill.alpha() > 0 ? a.style.fill.alpha() : 90);
            p.fillRect(viewRect, fill);
            p.setBrush(Qt::NoBrush);
            p.drawRect(viewRect);
            break;
        }
        case AnnotationType::SpeechBubble: {
            const double radius =
                std::min(12.0, std::min(viewRect.width(), viewRect.height()) / 4.0);
            QPainterPath body;
            body.addRoundedRect(viewRect, radius, radius);
            if (!a.points.empty()) {
                const QPointF tail = m_docToView(a.points.front(), page);
                const QPointF anchor(viewRect.left() + viewRect.width() * 0.25, viewRect.bottom());
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
                if (!a.style.fontFamily.isEmpty())
                    f.setFamily(a.style.fontFamily);
                f.setWeight(static_cast<QFont::Weight>(a.style.fontWeight));
                f.setPointSize(a.style.fontPointSize > 0 ? a.style.fontPointSize : 12);
                p.setFont(f);
                p.setPen(a.style.stroke);
                p.drawText(viewRect.adjusted(8, 4, -8, -4), Qt::AlignCenter | Qt::TextWordWrap,
                           a.text);
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
                                    center.y() - docSize.height() / 2.0, docSize.width(),
                                    docSize.height());
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
            if (a.imagePath.isEmpty() || viewRect.isEmpty())
                break;
            const std::string key = a.imagePath.toStdString();
            auto it = m_signatureCache.find(key);
            if (it == m_signatureCache.end()) {
                QImage img(a.imagePath);
                if (img.isNull())
                    break;
                it = m_signatureCache.emplace(key, std::move(img)).first;
            }
            const QImage &img = it->second;
            if (img.isNull())
                break;
            // Fit the PNG into the annotation rect while preserving
            // aspect ratio. Signatures usually come out tall-and-wide
            // rather than square, so centering keeps the ink where
            // the user dragged.
            const double srcAspect = static_cast<double>(img.width()) / std::max(1, img.height());
            const double dstAspect = viewRect.width() / std::max(0.001, viewRect.height());
            QRectF target = viewRect;
            if (srcAspect > dstAspect) {
                const double h = viewRect.width() / srcAspect;
                target = QRectF(viewRect.left(), viewRect.top() + (viewRect.height() - h) / 2.0,
                                viewRect.width(), h);
            } else {
                const double w = viewRect.height() * srcAspect;
                target = QRectF(viewRect.left() + (viewRect.width() - w) / 2.0, viewRect.top(), w,
                                viewRect.height());
            }
            p.drawImage(target, img);
            break;
        }
        case AnnotationType::Highlight:
        case AnnotationType::Underline:
        case AnnotationType::StrikeOut: {
            const std::vector<QRectF> &rects =
                a.quads.empty() ? std::vector<QRectF>{a.bounds} : a.quads;
            for (const QRectF &r : rects) {
                const QRectF vr = docRectToView(r, page);
                if (a.type == AnnotationType::Highlight) {
                    QColor fill = a.style.stroke;
                    fill.setAlpha(90);
                    p.fillRect(vr, fill);
                } else {
                    QPen thin(a.style.stroke);
                    thin.setWidthF(std::max(1.0, a.style.strokeWidth));
                    p.setPen(thin);
                    const double y =
                        (a.type == AnnotationType::Underline) ? vr.bottom() - 1.0 : vr.center().y();
                    p.drawLine(QPointF(vr.left(), y), QPointF(vr.right(), y));
                }
            }
            break;
        }
        default:
            break;
        }
    };

    for (const Annotation &a : m_store->annotations()) {
        drawOne(a);
    }

    // Selection affordance for the active annotation: a thin blue
    // dashed rectangle just outside the bounds plus four corner
    // handles for resize. Drawn on top of the shape so it sits
    // above any fill / stroke. Visible whenever an annotation is
    // selected, regardless of the active tool.
    if (m_selectedAnnotationId != 0 && m_store) {
        if (const Annotation *sel = m_store->find(m_selectedAnnotationId)) {
            const QRectF view = docRectToView(sel->bounds, sel->page);
            const QRectF inflated = view.adjusted(-3, -3, 3, 3);
            const QColor accent(60, 120, 220, 220);
            QPen pen(accent);
            pen.setWidth(1);
            pen.setStyle(Qt::DashLine);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawRect(inflated);
            // Resize handles: solid white interior, accent border,
            // 6x6 view-space px (see handleRect). Big enough to grab
            // with a mouse, small enough that on a short Line/Arrow
            // a body-click doesn't fall inside the corner's hit zone.
            QPen hpen(accent);
            hpen.setStyle(Qt::SolidLine);
            hpen.setWidth(1);
            p.setPen(hpen);
            p.setBrush(QColor(255, 255, 255, 230));
            for (auto h : {ResizeHandle::TopLeft, ResizeHandle::TopRight, ResizeHandle::BottomLeft,
                           ResizeHandle::BottomRight}) {
                p.drawRect(handleRect(view, h));
            }
        }
    }

    // Draw selection outlines for extra annotations selected via
    // selectAll(). These receive the same dashed border as the primary
    // but no resize handles — they are not individually interactive.
    if (!m_extraSelectedIds.empty() && m_store) {
        const QColor accent(60, 120, 220, 220);
        QPen pen(accent);
        pen.setWidth(1);
        pen.setStyle(Qt::DashLine);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        for (int extraId : m_extraSelectedIds) {
            if (const Annotation* a = m_store->find(extraId)) {
                const QRectF view = docRectToView(a->bounds, a->page);
                p.drawRect(view.adjusted(-3, -3, 3, 3));
            }
        }
    }

    if (m_tool == AnnotationTool::Select && !m_pendingSelection.empty()) {
        QColor selFill(80, 140, 220, 110);
        for (const QRectF &r : m_pendingSelection) {
            p.fillRect(docRectToView(r, m_page), selFill);
        }
    }

    // SAM tool preview: tinted mask + prompt markers. Drawn on top of
    // annotations so the user sees what's being selected against the
    // page content, but using a translucent blue so they can still
    // read the underlying image. Polygon outline for Smart Lasso.
    if (isSamTool() && (!m_samPositives.isEmpty() || !m_samNegatives.isEmpty())) {
        if (!m_samMask.isNull()) {
            // The mask is in source-image pixel coords; map to view by
            // walking the mask + using m_docToView for the bbox corners.
            const QRectF maskRect(QPointF(0, 0),
                                  QPointF(static_cast<qreal>(m_samMask.width()),
                                          static_cast<qreal>(m_samMask.height())));
            const QRectF viewRect = docRectToView(maskRect, m_page);
            // Build an ARGB tint of the mask so QPainter can stretch
            // it through the doc→view transform without alpha
            // surprises.
            QImage tint(m_samMask.size(), QImage::Format_ARGB32);
            tint.fill(Qt::transparent);
            for (int y = 0; y < tint.height(); ++y) {
                auto *dst = reinterpret_cast<QRgb *>(tint.scanLine(y));
                const uchar *src = m_samMask.constScanLine(y);
                for (int x = 0; x < tint.width(); ++x) {
                    if (src[x])
                        dst[x] = qRgba(64, 128, 255, 96);
                }
            }
            p.drawImage(viewRect, tint);
        }
        // Polygon outline for Smart Lasso.
        if (m_tool == AnnotationTool::SmartLasso && m_samController) {
            const QPolygon poly = m_samController->lastContour();
            if (!poly.isEmpty()) {
                QPolygonF scaled;
                scaled.reserve(poly.size());
                for (const QPoint &pt : poly) {
                    scaled.append(m_docToView(QPointF(pt), m_page));
                }
                QPen outline(QColor(255, 200, 40), 2.0);
                outline.setJoinStyle(Qt::RoundJoin);
                p.setPen(outline);
                p.setBrush(Qt::NoBrush);
                p.drawPolygon(scaled);
            }
        }
        // Prompt markers — green for positive, red for negative.
        auto drawMarker = [&](QPoint srcPt, QColor colour) {
            const QPointF c = m_docToView(QPointF(srcPt), m_page);
            p.setPen(QPen(Qt::white, 2.0));
            p.setBrush(colour);
            p.drawEllipse(c, 5.0, 5.0);
        };
        for (const QPoint &pt : m_samPositives)
            drawMarker(pt, QColor(64, 192, 80));
        for (const QPoint &pt : m_samNegatives)
            drawMarker(pt, QColor(220, 64, 64));
    }

    // Only paint a shape preview when the active tool actually
    // creates a shape on release. With Select active the drag
    // routes to text selection (highlight rectangles drawn above
    // via m_pendingSelection), so leaking a Rectangle / Ellipse
    // outline here makes the user think they're in box-drawing
    // mode and confuses the affordance.
    if (m_dragging && m_tool != AnnotationTool::None && m_tool != AnnotationTool::Select &&
        !isSamTool()) {
        Annotation preview;
        preview.page = m_dragPage;
        preview.type = [this]() {
            switch (m_tool) {
            case AnnotationTool::Ellipse:
                return AnnotationType::Ellipse;
            case AnnotationTool::Line:
                return AnnotationType::Line;
            case AnnotationTool::Arrow:
                return AnnotationType::Arrow;
            case AnnotationTool::Ink:
                return AnnotationType::Ink;
            case AnnotationTool::HighlightShape:
                return AnnotationType::HighlightShape;
            case AnnotationTool::SpeechBubble:
                return AnnotationType::SpeechBubble;
            case AnnotationTool::ZoomLens:
                return AnnotationType::ZoomLens;
            case AnnotationTool::Signature:
                return AnnotationType::Signature;
            case AnnotationTool::Redaction:
                return AnnotationType::Redaction;
            default:
                return AnnotationType::Rectangle;
            }
        }();
        preview.style = m_style;
        preview.bounds = QRectF(m_dragStartDoc, m_dragCurrentDoc).normalized();
        if (preview.type == AnnotationType::Line || preview.type == AnnotationType::Arrow) {
            preview.points = {m_dragStartDoc, m_dragCurrentDoc};
        } else if (preview.type == AnnotationType::Ink) {
            preview.points = m_inkPoints;
        } else if (preview.type == AnnotationType::Signature) {
            preview.imagePath = m_pendingSignaturePath;
        }
        drawOne(preview);
    }

    // --- Crop tool: dimmed live preview of the region that will be
    // KEPT vs discarded. Drawn last so it sits on top of everything.
    // The rect is mapped from doc space every paint, so it tracks zoom
    // / scroll / dpr with no extra plumbing (same transform the
    // annotations use). ---
    if (m_tool == AnnotationTool::CropRect && (m_cropDrawing || hasPendingCrop())) {
        const QRectF viewRect = docRectToView(m_cropRectDoc, m_cropPage).intersected(rect());
        if (!viewRect.isEmpty()) {
            // Dim everything OUTSIDE the crop rect with a translucent
            // scrim so the kept region reads as "in focus". Four bands
            // avoid an even-odd QPainterPath fill (cheaper, and no
            // seam artifacts at the rect edges).
            const QColor scrim(0, 0, 0, 110); // ~43% black — enough to
                                              // read as dimmed without
                                              // hiding page content the
                                              // user is aiming at.
            const QRectF full = rect();
            p.setPen(Qt::NoPen);
            p.setBrush(scrim);
            // top / bottom full-width bands, then left / right within
            // the crop's vertical span.
            p.drawRect(QRectF(full.left(), full.top(), full.width(), viewRect.top() - full.top()));
            p.drawRect(QRectF(full.left(), viewRect.bottom(), full.width(),
                              full.bottom() - viewRect.bottom()));
            p.drawRect(QRectF(full.left(), viewRect.top(), viewRect.left() - full.left(),
                              viewRect.height()));
            p.drawRect(QRectF(viewRect.right(), viewRect.top(), full.right() - viewRect.right(),
                              viewRect.height()));

            // Crop boundary: a light dashed rectangle so it stays
            // visible over both dark and light page content.
            QPen border(QColor(255, 255, 255, 230));
            border.setStyle(Qt::DashLine);
            border.setWidth(1);
            p.setPen(border);
            p.setBrush(Qt::NoBrush);
            p.drawRect(viewRect);

            // Rule-of-thirds guides — the familiar crop-tool affordance.
            QPen thirds(QColor(255, 255, 255, 90));
            thirds.setWidth(1);
            p.setPen(thirds);
            for (int i = 1; i <= 2; ++i) {
                const double x = viewRect.left() + viewRect.width() * i / 3.0;
                const double y = viewRect.top() + viewRect.height() * i / 3.0;
                p.drawLine(QPointF(x, viewRect.top()), QPointF(x, viewRect.bottom()));
                p.drawLine(QPointF(viewRect.left(), y), QPointF(viewRect.right(), y));
            }

            // Corner handles for adjustment, drawn only once the rect
            // has been laid down (not during the initial rubber-band,
            // where the moving corner IS the cursor).
            if (!m_cropDrawing && hasPendingCrop()) {
                QPen hpen(QColor(255, 255, 255, 230));
                hpen.setWidth(1);
                p.setPen(hpen);
                p.setBrush(QColor(60, 120, 220, 230));
                for (auto h : {ResizeHandle::TopLeft, ResizeHandle::TopRight,
                               ResizeHandle::BottomLeft, ResizeHandle::BottomRight}) {
                    p.drawRect(handleRect(viewRect, h));
                }
            }
        }
    }
}

bool AnnotationOverlay::eventFilter(QObject *obj, QEvent *event) {
    if (obj == parentWidget() && event->type() == QEvent::Resize) {
        if (auto *w = qobject_cast<QWidget *>(obj)) {
            setGeometry(w->rect());
        }
    }
    if (m_inlineEditor && m_inlineEditorAnnotationId != 0) {
        if (auto *edit = qobject_cast<QPlainTextEdit *>(obj)) {
            // Three terminations to handle:
            //   commit  — Ctrl+Enter or focus loss → write text back
            //   cancel  — Escape → discard the edit
            // For a freshly-placed annotation, both "cancel" and
            // "commit with empty text" remove the placeholder so the
            // user is not left with an invisible stamp on the page.
            auto finish = [this, edit](bool commitText) {
                if (!m_store)
                    return;
                const int id = m_inlineEditorAnnotationId;
                const Annotation *a = m_store->find(id);
                if (!a)
                    return;
                const QString typed = edit->toPlainText();
                if (m_inlineEditorIsNew && (typed.isEmpty() || !commitText)) {
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
                auto *key = static_cast<QKeyEvent *>(event);
                const bool commit = (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) &&
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

void AnnotationOverlay::mousePressEvent(QMouseEvent *event) {
    // Crop tool owns the pointer (owner ruling 2026-07-20): a press
    // starts / adjusts the crop rectangle and NEVER hit-tests or
    // selects an annotation underneath. Handled before the store guard
    // because the crop rect lives on the page, not in the annotation
    // store — it works even on a document with no annotations.
    if (m_tool == AnnotationTool::CropRect) {
        handleCropPress(event);
        return;
    }
    if (!m_store || m_tool == AnnotationTool::None) {
        event->ignore();
        return;
    }
    // SAM tools take a different press path entirely: clicks become
    // SAM prompts, not annotation drags. Kept in this dedicated branch
    // so the existing drawing-tool flow isn't strewn with isSamTool()
    // conditionals.
    if (isSamTool()) {
        // Allow LMB and RMB only; ignore other buttons. RMB acts as a
        // negative-prompt shortcut for Smart Lasso.
        if (event->button() != Qt::LeftButton && event->button() != Qt::RightButton) {
            event->ignore();
            return;
        }
        if (m_samPreparing) {
            // Encoder still working — swallow the click so we don't
            // queue a no-op decoder pass. The user sees a wait cursor
            // until prepare finishes.
            event->accept();
            return;
        }
        const int page = pageAt(event->position());
        const QPointF docF = toDoc(event->position(), page);
        const QPoint p(static_cast<int>(docF.x()), static_cast<int>(docF.y()));
        const bool negative = (event->button() == Qt::RightButton) ||
                              (event->modifiers() & Qt::ShiftModifier);
        if (m_tool == AnnotationTool::InstantAlpha) {
            // Instant Alpha is a single-positive workflow. The press
            // begins a drag that mouseMove keeps updating; the
            // release commits.
            if (negative) {
                // Negative prompts have no meaning for Instant Alpha;
                // silently swallow the click rather than confusing the
                // preview.
                event->accept();
                return;
            }
            m_samPositives = {p};
            m_samNegatives.clear();
            m_samDraggingInstant = true;
        } else {
            // Smart Lasso — accumulate prompts across clicks. Commit
            // is explicit (Enter or double-click) so the user can keep
            // refining.
            if (negative) {
                m_samNegatives.append(p);
            } else {
                m_samPositives.append(p);
            }
        }
        setFocus(Qt::MouseFocusReason); // accept Enter / Esc keys
        requestSamPreview();
        update();
        event->accept();
        return;
    }
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    // Resize-handle hit always wins — handles are drawn on top of
    // the annotation and overlap its body, so a corner click must
    // begin a resize rather than a select-and-move. Runs only when
    // the Select tool is active because handles are only drawn
    // (and therefore only meaningful as a hit target) during Select.
    if (m_tool == AnnotationTool::Select) {
        const ResizeHandle handle = handleAt(event->position());
        if (handle != ResizeHandle::None && m_store) {
            if (const Annotation *a = m_store->find(m_selectedAnnotationId)) {
                m_resizingHandle = handle;
                m_dragPage = a->page;
                m_resizeStartDoc = toDoc(event->position(), a->page);
                m_resizeOriginalBounds = a->bounds;
                // Coalesce per-frame update()s during the resize into
                // one undo step. mouseReleaseEvent (or abortInFlightDrag)
                // calls endCompound() to close it.
                m_store->beginCompound();
                update();
                return;
            }
        }
    }
    // Hit-test against existing annotations BEFORE the drawing-tool
    // path — but ONLY for the Select tool. Selection-and-move is a
    // Select-tool affordance, Preview/Acrobat-style: a press with a
    // drawing tool active always starts a NEW mark, regardless of what
    // is underneath.
    //
    // This is the "draw-first on press" half of DRAWING-TOOL PARITY
    // (owner ruling "parity", 2026-07-20; ADR
    // docs/decision-records/2026-07-20-drawing-tool-parity.md). The
    // bounded shape tools (Rectangle / Ellipse / Line / Arrow) now match
    // the free-form Ink tool, which already drew-first since Bug 3 (#91).
    // Previously this guard was `m_tool != Ink`, so the bounded tools
    // hijacked a press over an existing shape into select-and-move
    // (the old UAT-ANN-128). Under parity, a user aiming a drawing tool
    // at an existing shape draws a new overlapping one; to select the
    // underlying shape they switch to the Select tool.
    //
    // Every non-Select tool therefore falls straight through to the
    // draw / stroke-capture setup below. SAM tools (InstantAlpha /
    // SmartLasso) never reach here — they have their own press branch
    // above. Text / Note / SpeechBubble still open their inline editor
    // on release; the only thing they lose is the click-to-select-an-
    // existing-annotation shortcut, which now belongs to Select alone.
    //
    // The Select tool keeps its sticky multi-step semantics: first
    // click selects, second click on the same annotation begins the
    // move drag. The move only "commits" if the user actually drags,
    // since the compound is lazy-pushed (see AnnotationStore::pushHistory).
    if (m_tool == AnnotationTool::Select) {
        const int hitId = hitTest(event->position());
        if (hitId != 0) {
            const bool wasAlreadySelected = (m_selectedAnnotationId == hitId);
            if (!wasAlreadySelected) {
                m_selectedAnnotationId = hitId;
                emit selectionChanged(hitId);
            }
            m_extraSelectedIds.clear();
            m_pendingSelection.clear();
            // Prepare a move-drag. Select keeps the "click twice to
            // drag" affordance (UAT-ANN-120 pins single-click as a
            // pure-select gesture, not a move): the first click only
            // selects, and a second press on the already-selected
            // annotation begins the move.
            const bool readyToMove = wasAlreadySelected;
            if (readyToMove && m_store) {
                if (const Annotation *a = m_store->find(hitId)) {
                    m_movingSelected = true;
                    m_dragPage = a->page;
                    m_moveStartDoc = toDoc(event->position(), a->page);
                    m_moveOriginalBounds = a->bounds;
                    // Begin compound; pushHistory is lazy so a click-
                    // without-drag adds no undo frame.
                    m_store->beginCompound();
                }
            }
            setFocus(Qt::MouseFocusReason); // accept Delete / arrow keys
            update();
            return;
        }
    }
    // Empty-space click. For Select-tool we clear any annotation
    // selection (the user is starting a fresh text-selection drag),
    // then fall through to the text-selection drag setup below. For
    // drawing tools we leave the existing selection alone — the
    // user's intent is to draw a new shape; previously-selected
    // annotations stay selected so Delete / arrow keys still apply
    // to them.
    if (m_tool == AnnotationTool::Select) {
        if (m_selectedAnnotationId != 0 || !m_extraSelectedIds.empty()) {
            m_selectedAnnotationId = 0;
            m_extraSelectedIds.clear();
            emit selectionChanged(0);
            update();
        }
        m_pendingSelection.clear();
        // This is the start of a text-selection drag (see the Tool-
        // precedence rule note above setActiveTool()). Grab focus
        // explicitly — QApplication's synthetic-event delivery in tests
        // (and some native paths) doesn't run the implicit click-focus
        // pass ClickFocus normally gets from a real windowing system — so
        // that Ctrl+C reaches keyPressEvent() once the drag ends.
        setFocus(Qt::MouseFocusReason);
    }
    m_dragPage = pageAt(event->position());
    m_dragStartDoc = toDoc(event->position(), m_dragPage);
    m_dragCurrentDoc = m_dragStartDoc;
    m_dragging = true;
    m_inkPoints.clear();
    m_inkPressures.clear();
    if (m_tool == AnnotationTool::Ink) {
        m_inkPoints.push_back(m_dragStartDoc);
        // Force Touch trackpads on macOS surface NSEvent.pressure
        // through QPointerEvent::points().pressure(); plain mice
        // report 0. We emit pressure samples even when zero; the
        // commit step drops the parallel vector if no sample was
        // non-zero so the saved annotation stays compact.
        const float pr =
            event->points().isEmpty() ? 0.0f : float(event->points().first().pressure());
        m_inkPressures.push_back(pr);
    }
    update();
}

void AnnotationOverlay::mouseMoveEvent(QMouseEvent *event) {
    if (m_tool == AnnotationTool::CropRect) {
        handleCropMove(event);
        return;
    }
    // SAM drag tracking. For Instant Alpha the single positive prompt
    // follows the cursor while the button is down; the controller
    // throttles to ~30 Hz so a rapid drag does not saturate the
    // decoder. Smart Lasso uses discrete clicks — no drag tracking.
    if (isSamTool() && m_samDraggingInstant && m_tool == AnnotationTool::InstantAlpha) {
        const int page = pageAt(event->position());
        const QPointF docF = toDoc(event->position(), page);
        const QPoint p(static_cast<int>(docF.x()), static_cast<int>(docF.y()));
        m_samPositives = {p};
        requestSamPreview();
        update();
        return;
    }

    // Resize drag: shift the relevant corner of the original bounds
    // by the cursor delta in doc space. Use the original bounds as
    // the anchor so dragging a small distance resizes by exactly
    // that distance (no drift accumulation).
    if (m_resizingHandle != ResizeHandle::None && m_store && m_selectedAnnotationId != 0) {
        const QPointF here = toDoc(event->position(), m_dragPage);
        const QPointF delta = here - m_resizeStartDoc;
        QRectF nb = m_resizeOriginalBounds;
        switch (m_resizingHandle) {
        case ResizeHandle::TopLeft:
            nb.setTopLeft(nb.topLeft() + delta);
            break;
        case ResizeHandle::TopRight:
            nb.setTopRight(nb.topRight() + delta);
            break;
        case ResizeHandle::BottomLeft:
            nb.setBottomLeft(nb.bottomLeft() + delta);
            break;
        case ResizeHandle::BottomRight:
            nb.setBottomRight(nb.bottomRight() + delta);
            break;
        default:
            break;
        }
        // Disallow degenerate / inverted bounds — the user's drag
        // can't push a corner past its opposite. Clamp to a 1pt
        // minimum so the resize handle doesn't lock when zero-size.
        nb = nb.normalized();
        if (nb.width() < 1.0)
            nb.setWidth(1.0);
        if (nb.height() < 1.0)
            nb.setHeight(1.0);
        if (const Annotation *a = m_store->find(m_selectedAnnotationId)) {
            Annotation updated = *a;
            updated.bounds = nb;
            m_store->update(updated);
        }
        update();
        return;
    }

    // Move-drag for the selected annotation runs alongside the
    // shape-creation drag tracked by m_dragging. We translate the
    // annotation's bounds in document space and emit an in-place
    // update; the store's changed() signal repaints automatically.
    if (m_movingSelected && m_store && m_selectedAnnotationId != 0) {
        const QPointF here = toDoc(event->position(), m_dragPage);
        const QPointF delta = here - m_moveStartDoc;
        if (const Annotation *a = m_store->find(m_selectedAnnotationId)) {
            Annotation updated = *a;
            updated.bounds = m_moveOriginalBounds.translated(delta);
            // Translate auxiliary point lists too (Line/Arrow
            // endpoints, Ink polyline) so moving doesn't snap back
            // to the original anchors.
            for (QPointF &p : updated.points) {
                p += delta;
            }
            m_store->update(updated);
        }
        update();
        return;
    }
    if (!m_dragging) {
        // Hover-only I-beam cursor (Tool-precedence rule, case 2 — see
        // the note above setActiveTool()). Only ever an HONEST signal:
        // m_pointOverText is unset for documents with no text-selection
        // wiring (raster images), so the cursor stays the plain arrow
        // rather than promising an interaction that can't happen (G3).
        if (m_tool == AnnotationTool::Select && m_pointOverText) {
            const int page = pageAt(event->position());
            setCursor(m_pointOverText(event->position(), page) ? Qt::IBeamCursor
                                                                 : Qt::ArrowCursor);
        }
        return;
    }
    m_dragCurrentDoc = toDoc(event->position(), m_dragPage);
    if (m_tool == AnnotationTool::Select) {
        // Live-update the highlight while the drag is still in progress —
        // previously this only ran in mouseReleaseEvent, so the user saw
        // no feedback at all until they lifted the mouse button (owner
        // dogfood report, 2026-07-31). update() repaints immediately;
        // the highlight fill lives in paintEvent's Select-tool branch.
        if (m_textSelection) {
            m_pendingSelection = m_textSelection(m_dragStartDoc, m_dragCurrentDoc, m_dragPage);
        }
        update();
        return;
    }
    if (m_tool == AnnotationTool::Ink) {
        // Remember the live-stroke tail before appending so we can clip
        // the repaint to just the new segment below (Bug 2).
        const size_t prevCount = m_inkPoints.size();
        // Append this move's sample(s), carrying per-point pressure.
        // NOTE (pre-existing behaviour, not introduced here): a
        // QMouseEvent is a QSinglePointEvent, so event->points() always
        // holds exactly ONE point and pt.position() == event->position().
        // This loop therefore runs once for mouse input and recovers no
        // extra coalesced samples — it is not the mid-move coalescing
        // trick its shape suggests. It does still carry the correct
        // per-point pressure, and it is the right shape for any future
        // multi-point (tablet/touch) QPointerEvent that populates
        // points() with more than one entry. Left as-is intentionally.
        const auto &pts = event->points();
        if (!pts.isEmpty()) {
            for (const QEventPoint &pt : pts) {
                m_inkPoints.push_back(toDoc(pt.position(), m_dragPage));
                m_inkPressures.push_back(float(pt.pressure()));
            }
        } else {
            m_inkPoints.push_back(m_dragCurrentDoc);
            m_inkPressures.push_back(0.0f);
        }
        // Clip the mid-drag repaint to the newly-added segment. A bare
        // update() forces an unclipped full-widget paint, which
        // re-renders every committed annotation + search highlight + the
        // entire growing in-progress path on EVERY move — cost grows with
        // both the page's annotation count and the stroke length, so a
        // long stroke over a busy page crawls. The stroke is append-only,
        // so older segments already sit in the backing store; only the
        // new tail needs repainting. paintEvent still redraws the full
        // preview path, but QPainter clips it to this small region, so
        // the rendered pixels are identical — just far fewer of them.
        const size_t from = prevCount > 0 ? prevCount - 1 : 0;
        double minX = 0, minY = 0, maxX = 0, maxY = 0;
        bool has = false;
        for (size_t i = from; i < m_inkPoints.size(); ++i) {
            const QPointF v = m_docToView(m_inkPoints[i], m_dragPage);
            if (!has) {
                minX = maxX = v.x();
                minY = maxY = v.y();
                has = true;
            } else {
                minX = std::min(minX, v.x());
                maxX = std::max(maxX, v.x());
                minY = std::min(minY, v.y());
                maxY = std::max(maxY, v.y());
            }
        }
        if (has) {
            // Pad for the pen width (pressure can widen it up to base + 5
            // px, see the Ink renderer), round caps/joins, and AA, so no
            // rasterised pixel is clipped out of the dirty rect.
            const double pad = (m_style.strokeWidth > 0.0 ? m_style.strokeWidth : 1.5) + 8.0;
            update(QRectF(minX - pad, minY - pad, (maxX - minX) + 2 * pad, (maxY - minY) + 2 * pad)
                       .toAlignedRect());
        } else {
            // Defensive fallback: mid-drag we always have at least the
            // press sample plus this move's, so `has` is true in practice.
            // Keep a full update() for the degenerate empty case rather
            // than skipping the repaint entirely.
            update();
        }
        return;
    }
    update();
}

void AnnotationOverlay::mouseReleaseEvent(QMouseEvent *event) {
    if (m_tool == AnnotationTool::CropRect) {
        handleCropRelease(event);
        return;
    }
    // SAM release. Instant Alpha commits on release; Smart Lasso waits
    // for an explicit commit (Enter / double-click).
    if (isSamTool() && m_samDraggingInstant && event->button() == Qt::LeftButton &&
        m_tool == AnnotationTool::InstantAlpha) {
        m_samDraggingInstant = false;
        // The throttle may still have a pending decoder dispatch — if
        // so, the controller will flush it shortly. In practice the
        // mouseMove that landed at this release already drove the
        // latest decoder; commit against whatever we currently have.
        commitInstantAlpha();
        event->accept();
        return;
    }
    // Smart Lasso swallows the release (the click already added a
    // prompt in mousePressEvent).
    if (isSamTool() && m_tool == AnnotationTool::SmartLasso) {
        event->accept();
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;
    // End-of-resize bookkeeping: bounds were updated incrementally
    // every frame inside one compound gesture. Close the compound
    // so the whole drag is one undo step; the click-without-drag
    // edge case is handled by the lazy-push in pushHistory().
    if (m_resizingHandle != ResizeHandle::None) {
        m_resizingHandle = ResizeHandle::None;
        if (m_store) {
            m_store->endCompound();
        }
        update();
        return;
    }
    // End-of-move bookkeeping. Same shape as resize: close the
    // compound; the changed() signals emitted per-frame already
    // drove the repaint and Inspector refresh.
    if (m_movingSelected) {
        m_movingSelected = false;
        if (m_store) {
            m_store->endCompound();
        }
        update();
        return;
    }
    if (!m_dragging)
        return;
    m_dragging = false;
    const QPointF end = toDoc(event->position(), m_dragPage);

    if (m_tool == AnnotationTool::Select) {
        // Re-resolve against the exact release point. mouseMoveEvent
        // already live-updates m_pendingSelection on every drag sample
        // (see the Tool-precedence rule note above setActiveTool()); this
        // is the authoritative last word in case the final move landed
        // exactly on the release pixel without an intervening move event.
        m_pendingSelection.clear();
        if (m_textSelection) {
            m_pendingSelection = m_textSelection(m_dragStartDoc, end, m_dragPage);
        }
        // Keep m_dragCurrentDoc in lockstep with `end` — Ctrl+C
        // (keyPressEvent) reads m_dragCurrentDoc, not `end`, and a
        // release can in principle land a pixel or two away from the
        // preceding move sample. Without this, a copy immediately after
        // release could (in a rare case) resolve a hair short of what
        // was just painted.
        m_dragCurrentDoc = end;
        m_page = m_dragPage;
        update();
        return;
    }

    Annotation a;
    a.page = m_dragPage;
    a.style = m_style;
    a.bounds = QRectF(m_dragStartDoc, end).normalized();

    switch (m_tool) {
    case AnnotationTool::Rectangle:
        a.type = AnnotationType::Rectangle;
        break;
    case AnnotationTool::Ellipse:
        a.type = AnnotationType::Ellipse;
        break;
    case AnnotationTool::Line:
        a.type = AnnotationType::Line;
        a.points = {m_dragStartDoc, end};
        break;
    case AnnotationTool::Arrow:
        a.type = AnnotationType::Arrow;
        a.points = {m_dragStartDoc, end};
        break;
    case AnnotationTool::Ink: {
        if (m_inkPoints.size() < 2) {
            m_inkPoints.clear();
            m_inkPressures.clear();
            update();
            return;
        }
        a.type = AnnotationType::Ink;
        a.points = m_inkPoints;
        // Drop the parallel pressures vector if every sample was
        // 0 (plain mouse, no Force Touch / tablet). Saves bytes
        // in the AnnotationStore and signals "constant width" to
        // the renderer.
        bool anyPressure = false;
        for (float p : m_inkPressures) {
            if (p > 0.0f) {
                anyPressure = true;
                break;
            }
        }
        if (anyPressure && m_inkPressures.size() == m_inkPoints.size()) {
            a.pressures = m_inkPressures;
        }
        qreal minX = m_inkPoints.front().x(), maxX = minX;
        qreal minY = m_inkPoints.front().y(), maxY = minY;
        for (const auto &p : m_inkPoints) {
            minX = std::min(minX, p.x());
            maxX = std::max(maxX, p.x());
            minY = std::min(minY, p.y());
            maxY = std::max(maxY, p.y());
        }
        a.bounds = QRectF(QPointF(minX, minY), QPointF(maxX, maxY));
        m_inkPoints.clear();
        m_inkPressures.clear();
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
                rect = QRectF(m_dragStartDoc - QPointF(10.0, 10.0), QSizeF(24.0, 24.0));
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
            rect = QRectF(m_dragStartDoc - QPointF(side / 2, side / 2), QSizeF(side, side));
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
            rect = QRectF(m_dragStartDoc - QPointF(defSize.width() / 2.0, defSize.height() / 2.0),
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
        a.type = (m_tool == AnnotationTool::Highlight)   ? AnnotationType::Highlight
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
            for (const QRectF &r : a.quads)
                bbox = bbox.united(r);
            a.bounds = bbox;
        }
        break;
    }
    default:
        m_inkPoints.clear();
        m_inkPressures.clear();
        update();
        return;
    }

    if (a.type != AnnotationType::Ink && a.bounds.width() < 1.0 && a.bounds.height() < 1.0) {
        update();
        return;
    }

    const int id = m_store->add(std::move(a));
    emit annotationCommitted(id);
    update();
}

int AnnotationOverlay::hitTest(const QPointF &viewPt) const {
    if (!m_store)
        return 0;
    const int page = (m_pageAtView ? m_pageAtView(viewPt) : m_page);
    const auto &all = m_store->annotations();
    for (auto it = all.rbegin(); it != all.rend(); ++it) {
        if (page >= 0 && it->page != page)
            continue;
        if (docRectToView(it->bounds, it->page).contains(viewPt)) {
            return it->id;
        }
    }
    return 0;
}

void AnnotationOverlay::openInlineEditor(int annotationId) {
    if (!m_store)
        return;
    const Annotation *a = m_store->find(annotationId);
    if (!a)
        return;
    if (m_inlineEditor) {
        m_inlineEditor->deleteLater();
        m_inlineEditor = nullptr;
    }
    if (a->type != AnnotationType::Text && a->type != AnnotationType::Note &&
        a->type != AnnotationType::SpeechBubble) {
        return;
    }

    auto *frame = new QFrame(this);
    frame->setFrameShape(QFrame::Box);
    frame->setAutoFillBackground(true);
    auto *layout = new QVBoxLayout(frame);
    layout->setContentsMargins(2, 2, 2, 2);
    auto *edit = new QPlainTextEdit(frame);
    edit->setPlainText(a->text);
    QFont f = edit->font();
    if (!a->style.fontFamily.isEmpty())
        f.setFamily(a->style.fontFamily);
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

void AnnotationOverlay::mouseDoubleClickEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    // Smart Lasso commit gesture: a double-click on the canvas means
    // "I'm done refining — apply." A single-click already added a
    // prompt point in mousePressEvent; the double-click consumes
    // that same point twice (once for the first press, once for the
    // second) which we tolerate — the duplicate point doesn't change
    // the SAM output.
    if (m_tool == AnnotationTool::SmartLasso && !m_samPositives.isEmpty()) {
        commitSmartLasso();
        event->accept();
        return;
    }
    const int id = hitTest(event->position());
    if (id == 0) {
        event->ignore();
        return;
    }
    openInlineEditor(id);
}

void AnnotationOverlay::keyPressEvent(QKeyEvent *event) {
    // Crop keyboard handlers run first: they must fire regardless of
    // any annotation selection. Enter commits the pending crop; Esc
    // abandons it. Both are swallowed so the keystroke doesn't leak to
    // the viewer (e.g. Esc closing something else).
    if (m_tool == AnnotationTool::CropRect) {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            commitPendingCrop();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Escape) {
            clearPendingCrop();
            event->accept();
            return;
        }
    }
    // SAM keyboard handlers run before the annotation-selection
    // branch so they fire even when nothing is "selected" in the
    // annotation-store sense.
    if (isSamTool()) {
        if (event->key() == Qt::Key_Escape) {
            m_samPositives.clear();
            m_samNegatives.clear();
            m_samMask = QImage();
            m_samDraggingInstant = false;
            update();
            event->accept();
            return;
        }
        if (m_tool == AnnotationTool::SmartLasso &&
            (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)) {
            commitSmartLasso();
            event->accept();
            return;
        }
    }
    // Select-tool text selection (Tool-precedence rule, case 2 — see the
    // note above setActiveTool()). Runs before the m_selectedAnnotationId
    // guard below because a pending TEXT selection has no annotation id;
    // gating on that guard would silently swallow Ctrl+C / Esc here.
    if (m_tool == AnnotationTool::Select && !m_pendingSelection.empty()) {
        if (event->matches(QKeySequence::Copy)) {
            if (m_textSelectionText) {
                const QString text =
                    m_textSelectionText(m_dragStartDoc, m_dragCurrentDoc, m_dragPage);
                if (!text.isEmpty()) {
                    QApplication::clipboard()->setText(text);
                }
            }
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Escape) {
            m_pendingSelection.clear();
            update();
            event->accept();
            return;
        }
    }
    if (m_selectedAnnotationId == 0 || !m_store) {
        event->ignore();
        return;
    }
    // Delete / Backspace removes the selected annotation(s). When
    // selectAll() was used the extra ids are deleted together with
    // the primary in a single undo step via removeMultiple().
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        if (!m_extraSelectedIds.empty()) {
            // Batch delete: primary + all extras in one undo entry.
            std::vector<int> allIds = m_extraSelectedIds;
            allIds.push_back(m_selectedAnnotationId);
            m_extraSelectedIds.clear();
            m_store->removeMultiple(allIds);
        } else {
            m_store->remove(m_selectedAnnotationId);
        }
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
    case Qt::Key_Left:
        nudgeSelected(-step, 0.0);
        return;
    case Qt::Key_Right:
        nudgeSelected(+step, 0.0);
        return;
    case Qt::Key_Up:
        nudgeSelected(0.0, -step);
        return;
    case Qt::Key_Down:
        nudgeSelected(0.0, +step);
        return;
    default:
        break;
    }
    event->ignore();
}

std::vector<int> AnnotationOverlay::selectedAnnotationIds() const {
    if (m_selectedAnnotationId == 0) return {};
    std::vector<int> ids;
    ids.reserve(1 + m_extraSelectedIds.size());
    ids.push_back(m_selectedAnnotationId);
    ids.insert(ids.end(), m_extraSelectedIds.begin(), m_extraSelectedIds.end());
    return ids;
}

void AnnotationOverlay::selectAll() {
    if (!m_store || m_store->isEmpty()) return;
    m_extraSelectedIds.clear();
    int firstId = 0;
    for (const Annotation& a : m_store->annotations()) {
        if (firstId == 0) {
            firstId = a.id;
        } else {
            m_extraSelectedIds.push_back(a.id);
        }
    }
    if (firstId == 0) return;
    m_selectedAnnotationId = firstId;
    // Always emit selectionChanged so that Inspector and other
    // listeners are notified — even when m_selectedAnnotationId was
    // already pointing at firstId but m_extraSelectedIds has changed.
    emit selectionChanged(firstId);
    // Grab keyboard focus so Delete / arrow keys work immediately
    // after Cmd+A without requiring an extra click on the overlay.
    setFocus(Qt::OtherFocusReason);
    update();
}

QRectF AnnotationOverlay::selectedViewRectForTest() const {
    if (m_selectedAnnotationId == 0 || !m_store)
        return {};
    const Annotation *a = m_store->find(m_selectedAnnotationId);
    if (!a)
        return {};
    return docRectToView(a->bounds, a->page);
}

QRectF AnnotationOverlay::handleRect(const QRectF &viewBounds, ResizeHandle which) const {
    // The handle hit zone is the bounding box around each corner that the
    // user can press to begin a resize drag. We shrink this from 10×10 to
    // 6×6 because Line and Arrow annotations have endpoints that coincide
    // with the bbox corners — a 10×10 zone covers most of a short
    // line/arrow's body and steals body-clicks (which should begin a
    // move drag) away from the move path. 6×6 is small enough that even
    // short shapes have a graspable body, and large enough for keyboard-
    // and-mouse desktop users to land on with a normal pointer.
    // Shape-aware (endpoint-only) handles for Line/Arrow are a follow-up
    // PR (see TODO.md ## 2026-05-19 HITL pass).
    constexpr double kSize = 6.0; // view-space px per side
    constexpr double kHalf = kSize / 2.0;
    QPointF c;
    switch (which) {
    case ResizeHandle::TopLeft:
        c = viewBounds.topLeft();
        break;
    case ResizeHandle::TopRight:
        c = viewBounds.topRight();
        break;
    case ResizeHandle::BottomLeft:
        c = viewBounds.bottomLeft();
        break;
    case ResizeHandle::BottomRight:
        c = viewBounds.bottomRight();
        break;
    default:
        return {};
    }
    return QRectF(c.x() - kHalf, c.y() - kHalf, kSize, kSize);
}

AnnotationOverlay::ResizeHandle AnnotationOverlay::handleAt(const QPointF &viewPt) const {
    if (m_selectedAnnotationId == 0 || !m_store)
        return ResizeHandle::None;
    const Annotation *a = m_store->find(m_selectedAnnotationId);
    if (!a)
        return ResizeHandle::None;
    const QRectF view = docRectToView(a->bounds, a->page);
    for (auto h : {ResizeHandle::TopLeft, ResizeHandle::TopRight, ResizeHandle::BottomLeft,
                   ResizeHandle::BottomRight}) {
        if (handleRect(view, h).contains(viewPt))
            return h;
    }
    return ResizeHandle::None;
}

void AnnotationOverlay::nudgeSelected(double dx, double dy) {
    if (m_selectedAnnotationId == 0 || !m_store)
        return;
    const Annotation *a = m_store->find(m_selectedAnnotationId);
    if (!a)
        return;
    Annotation updated = *a;
    updated.bounds.translate(dx, dy);
    for (QPointF &p : updated.points) {
        p.rx() += dx;
        p.ry() += dy;
    }
    m_store->update(updated);
    update();
}

void AnnotationOverlay::tabletEvent(QTabletEvent *event) {
    // Stylus input drives the same Ink-stroke buffer as mouse moves
    // but uses the absolute device coordinates and per-sample
    // pressure. We synthesise mousePress/Move/Release semantics so
    // the rest of the overlay behaves identically — selection,
    // shape commit on release, etc.
    if (m_tool != AnnotationTool::Ink) {
        QWidget::tabletEvent(event);
        return;
    }
    const QPointF posDoc = toDoc(event->position(), pageAt(event->position()));
    const float pressure = float(event->pressure());
    switch (event->type()) {
    case QEvent::TabletPress: {
        m_dragPage = pageAt(event->position());
        m_dragStartDoc = posDoc;
        m_dragCurrentDoc = posDoc;
        m_dragging = true;
        m_inkPoints.clear();
        m_inkPressures.clear();
        m_inkPoints.push_back(posDoc);
        m_inkPressures.push_back(pressure);
        update();
        event->accept();
        return;
    }
    case QEvent::TabletMove: {
        if (!m_dragging) {
            event->ignore();
            return;
        }
        m_dragCurrentDoc = posDoc;
        m_inkPoints.push_back(posDoc);
        m_inkPressures.push_back(pressure);
        update();
        event->accept();
        return;
    }
    case QEvent::TabletRelease: {
        // Synthesise a left-button release so the existing Ink
        // commit path runs unchanged. The QMouseEvent ctor wants
        // a global pos; pass the same position the tablet
        // reported and let Qt translate.
        m_dragging = true; // mouseReleaseEvent guards on this
        QMouseEvent fake(QEvent::MouseButtonRelease, event->position(), event->globalPosition(),
                         Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        mouseReleaseEvent(&fake);
        event->accept();
        return;
    }
    default:
        break;
    }
    QWidget::tabletEvent(event);
}

void AnnotationOverlay::contextMenuEvent(QContextMenuEvent *event) {
    if (!m_store || m_tool != AnnotationTool::Select || m_pendingSelection.empty()) {
        event->ignore();
        return;
    }
    QMenu menu(this);
    QAction *hi = menu.addAction(tr("Highlight"));
    QAction *un = menu.addAction(tr("Underline"));
    QAction *st = menu.addAction(tr("Strikeout"));
    QAction *chosen = menu.exec(event->globalPos());
    if (!chosen)
        return;

    Annotation a;
    a.page = m_dragPage;
    a.style = m_style;
    a.quads = m_pendingSelection;
    QRectF bbox = a.quads.front();
    for (const QRectF &r : a.quads)
        bbox = bbox.united(r);
    a.bounds = bbox;
    if (chosen == hi)
        a.type = AnnotationType::Highlight;
    else if (chosen == un)
        a.type = AnnotationType::Underline;
    else if (chosen == st)
        a.type = AnnotationType::StrikeOut;
    else
        return;

    const int id = m_store->add(std::move(a));
    emit annotationCommitted(id);
    m_pendingSelection.clear();
    update();
}

// --- Direct-manipulation page crop (backlog
// 2026-07-15-crop-pages-direct-manipulation) -----------------------------

// Minimum crop side, in DOCUMENT units (PDF points / image pixels),
// below which a gesture is treated as a stray click rather than a crop.
// Range tried: 2–8; at 2 a jittery click could commit a sliver crop, at
// 8 a deliberate small crop on a zoomed-in page felt sticky. 4 rejects
// accidental clicks while still allowing a purposeful small selection.
// Symptom to change: stray clicks committing tiny crops (raise it) or a
// small intended crop being silently dropped (lower it).
static constexpr double kMinCropSideDoc = 4.0;

void AnnotationOverlay::handleCropPress(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    // Take keyboard focus so Enter (commit) / Esc (cancel) reach
    // keyPressEvent without a second click.
    setFocus(Qt::MouseFocusReason);

    // With a rect already down, a corner-handle press resizes it and a
    // body press moves it. A press outside the rect starts a fresh
    // rubber-band. Crop owns the pointer throughout: no annotation
    // hit-test happens on any of these paths (owner ruling).
    if (hasPendingCrop()) {
        const ResizeHandle h = cropHandleAt(event->position());
        if (h != ResizeHandle::None) {
            m_cropHandle = h;
            m_cropHandleOrigRect = m_cropRectDoc;
            update();
            return;
        }
        const QRectF viewRect = docRectToView(m_cropRectDoc, m_cropPage);
        if (viewRect.contains(event->position())) {
            m_cropMoving = true;
            m_cropMoveStartDoc = toDoc(event->position(), m_cropPage);
            m_cropMoveOrigRect = m_cropRectDoc;
            update();
            return;
        }
    }
    m_cropPage = pageAt(event->position());
    m_cropStartDoc = toDoc(event->position(), m_cropPage);
    m_cropRectDoc = QRectF(m_cropStartDoc, m_cropStartDoc);
    m_cropDrawing = true;
    update();
}

void AnnotationOverlay::handleCropMove(QMouseEvent *event) {
    // All three paths recompute the doc-space rect from an anchor
    // captured at press time, so there is no per-frame drift and the
    // stored rect is exactly what the cursor describes on the page.
    if (m_cropHandle != ResizeHandle::None) {
        const QPointF here = toDoc(event->position(), m_cropPage);
        QRectF nb = m_cropHandleOrigRect;
        switch (m_cropHandle) {
        case ResizeHandle::TopLeft:
            nb.setTopLeft(here);
            break;
        case ResizeHandle::TopRight:
            nb.setTopRight(here);
            break;
        case ResizeHandle::BottomLeft:
            nb.setBottomLeft(here);
            break;
        case ResizeHandle::BottomRight:
            nb.setBottomRight(here);
            break;
        default:
            break;
        }
        m_cropRectDoc = nb.normalized();
        update();
        return;
    }
    if (m_cropMoving) {
        const QPointF here = toDoc(event->position(), m_cropPage);
        m_cropRectDoc = m_cropMoveOrigRect.translated(here - m_cropMoveStartDoc);
        update();
        return;
    }
    if (m_cropDrawing) {
        const QPointF here = toDoc(event->position(), m_cropPage);
        m_cropRectDoc = QRectF(m_cropStartDoc, here).normalized();
        update();
        return;
    }
}

void AnnotationOverlay::handleCropRelease(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    m_cropDrawing = false;
    m_cropHandle = ResizeHandle::None;
    m_cropMoving = false;
    // Drop a degenerate rect (a click with no real drag) so a stray
    // click doesn't leave an invisible zero-area crop that would
    // swallow the next Enter.
    if (m_cropRectDoc.isNull() || m_cropRectDoc.width() < kMinCropSideDoc ||
        m_cropRectDoc.height() < kMinCropSideDoc) {
        m_cropRectDoc = QRectF();
    } else {
        m_cropRectDoc = m_cropRectDoc.normalized();
    }
    update();
}

void AnnotationOverlay::commitPendingCrop() {
    if (!hasPendingCrop())
        return;
    const QRectF rectDoc = m_cropRectDoc.normalized();
    if (rectDoc.width() < kMinCropSideDoc || rectDoc.height() < kMinCropSideDoc) {
        clearPendingCrop();
        return;
    }
    const int page = m_cropPage;
    clearPendingCrop();
    emit cropCommitted(rectDoc, page);
}

void AnnotationOverlay::clearPendingCrop() {
    m_cropRectDoc = QRectF();
    m_cropDrawing = false;
    m_cropHandle = ResizeHandle::None;
    m_cropMoving = false;
    update();
}

AnnotationOverlay::ResizeHandle
AnnotationOverlay::cropHandleAt(const QPointF &viewPt) const {
    if (!hasPendingCrop())
        return ResizeHandle::None;
    const QRectF view = docRectToView(m_cropRectDoc, m_cropPage);
    for (auto h : {ResizeHandle::TopLeft, ResizeHandle::TopRight, ResizeHandle::BottomLeft,
                   ResizeHandle::BottomRight}) {
        if (handleRect(view, h).contains(viewPt))
            return h;
    }
    return ResizeHandle::None;
}

} // namespace trailer
