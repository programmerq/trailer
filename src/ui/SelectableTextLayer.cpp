#include "SelectableTextLayer.h"

#include <QApplication>
#include <QClipboard>
#include <QEvent>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QResizeEvent>
#include <QtGui/QPolygon>

#include <algorithm>

namespace trailer {

SelectableTextLayer::SelectableTextLayer(QWidget *parent) : QWidget(parent) {
    // Transparent painter overlay. Mouse events come through; tab
    // focus does not (we want clicks to be able to pass through the
    // tab order without trapping focus here).
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setMouseTracking(true);
    setFocusPolicy(Qt::ClickFocus);
    m_docToView = [](QPointF p, int /*page*/) { return p; };
    m_pageAtView = [this](QPointF) { return m_currentPage; };
    // Resize-with-parent. The adapters install us as a child of the
    // viewport (QPdfView's viewport / the image label) but don't
    // call setGeometry on every viewport resize — installing as an
    // event filter on the parent gives us a hook to follow.
    if (parent) {
        parent->installEventFilter(this);
    }
}

bool SelectableTextLayer::eventFilter(QObject *obj, QEvent *event) {
    if (obj == parentWidget() && event->type() == QEvent::Resize) {
        if (auto *w = qobject_cast<QWidget *>(obj)) {
            setGeometry(w->rect());
            m_cacheDirty = true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

void SelectableTextLayer::setStore(SelectableTextStore *store) {
    if (m_store.data() == store)
        return;
    if (m_store) {
        disconnect(m_store, nullptr, this, nullptr);
    }
    m_store = store;
    m_cacheDirty = true;
    m_selectedBlockIds.clear();
    if (m_store) {
        connect(m_store, &SelectableTextStore::pageChanged, this, [this](int page) {
            if (page == m_currentPage) {
                m_cacheDirty = true;
                update();
            }
        });
        connect(m_store, &SelectableTextStore::changed, this, [this]() {
            m_cacheDirty = true;
            update();
        });
    }
    update();
}

void SelectableTextLayer::setDocToView(DocToView fn) {
    m_docToView = fn ? std::move(fn) : [](QPointF p, int /*page*/) { return p; };
    m_cacheDirty = true;
    update();
}

void SelectableTextLayer::setPageAtView(PageAtView fn) {
    m_pageAtView = fn ? std::move(fn)
                      : [this](QPointF) { return m_currentPage; };
}

void SelectableTextLayer::setCurrentPage(int page) {
    if (m_currentPage == page)
        return;
    m_currentPage = page;
    m_cacheDirty = true;
    // Selection is per-page — flip pages and the user's selection on
    // the previous page goes away. This matches every PDF viewer
    // we've seen and avoids cross-page reading-order surprises.
    m_selectedBlockIds.clear();
    emit selectionChanged();
    update();
}

bool SelectableTextLayer::isPointOverText(QPointF viewPt) const {
    rebuildViewBlocks();
    for (const auto &vb : m_viewBlocks) {
        if (vb.bounds.contains(viewPt) && vb.polygon.containsPoint(viewPt, Qt::OddEvenFill)) {
            return true;
        }
    }
    return false;
}

Qt::CursorShape SelectableTextLayer::cursorShapeFor(QPointF viewPt) const {
    return isPointOverText(viewPt) ? Qt::IBeamCursor : Qt::ArrowCursor;
}

QString SelectableTextLayer::selectedText() const {
    if (!m_store || m_selectedBlockIds.empty())
        return {};
    const auto &blocks = m_store->blocks(m_currentPage);
    if (blocks.empty())
        return {};
    // Re-sort the selection in reading order — top to bottom, then
    // left to right. Block centroids are stable so this gives the
    // same order as a left-to-right reader would dictate.
    std::vector<int> ordered = m_selectedBlockIds;
    std::sort(ordered.begin(), ordered.end(), [&](int a, int b) {
        const auto &ba = blocks[static_cast<size_t>(a)].polygon.boundingRect();
        const auto &bb = blocks[static_cast<size_t>(b)].polygon.boundingRect();
        const int yAxis = ba.center().y();
        const int yBxis = bb.center().y();
        // Tolerance: lines on roughly the same horizontal sit within
        // half a block height of each other.
        const int tol = std::max(ba.height(), bb.height()) / 2;
        if (std::abs(yAxis - yBxis) > tol) {
            return yAxis < yBxis;
        }
        return ba.center().x() < bb.center().x();
    });
    QStringList parts;
    parts.reserve(static_cast<int>(ordered.size()));
    for (int idx : ordered) {
        if (idx < 0 || static_cast<size_t>(idx) >= blocks.size())
            continue;
        parts << blocks[static_cast<size_t>(idx)].text;
    }
    return parts.join(QLatin1Char('\n'));
}

void SelectableTextLayer::rebuildViewBlocks() const {
    if (!m_cacheDirty && m_cachePage == m_currentPage)
        return;
    m_viewBlocks.clear();
    m_cachePage = m_currentPage;
    m_cacheDirty = false;
    if (!m_store)
        return;
    const auto &blocks = m_store->blocks(m_currentPage);
    m_viewBlocks.reserve(blocks.size());
    for (size_t i = 0; i < blocks.size(); ++i) {
        const auto &b = blocks[i];
        QPolygonF view;
        view.reserve(b.polygon.size());
        for (const QPoint &p : b.polygon) {
            view << m_docToView(QPointF(p), m_currentPage);
        }
        if (view.isEmpty())
            continue;
        ViewBlock vb;
        vb.blockIndex = static_cast<int>(i);
        vb.polygon = view;
        vb.bounds = view.boundingRect();
        m_viewBlocks.push_back(std::move(vb));
    }
}

std::vector<int> SelectableTextLayer::hitBlocksForDrag(QPointF startView, QPointF endView) const {
    rebuildViewBlocks();
    // Drag-rect bounds (inclusive of either endpoint). Blocks whose
    // bounding-box centroid sits within the rect are picked.
    const qreal x0 = std::min(startView.x(), endView.x());
    const qreal x1 = std::max(startView.x(), endView.x());
    const qreal y0 = std::min(startView.y(), endView.y());
    const qreal y1 = std::max(startView.y(), endView.y());
    const QRectF dragRect(QPointF(x0, y0), QPointF(x1, y1));
    std::vector<int> hit;
    for (const auto &vb : m_viewBlocks) {
        const QPointF c = vb.bounds.center();
        const bool inDrag = dragRect.contains(c);
        // Also include blocks that the endpoints landed on, even
        // if their centroid is outside the drag rect — a tap-and-
        // tiny-drag inside one block should still pick it up.
        const bool startHit = vb.polygon.containsPoint(startView, Qt::OddEvenFill);
        const bool endHit = vb.polygon.containsPoint(endView, Qt::OddEvenFill);
        if (inDrag || startHit || endHit) {
            hit.push_back(vb.blockIndex);
        }
    }
    return hit;
}

void SelectableTextLayer::setSelection(std::vector<int> ids) {
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    if (ids == m_selectedBlockIds)
        return;
    m_selectedBlockIds = std::move(ids);
    emit selectionChanged();
    update();
}

void SelectableTextLayer::copySelectionToClipboard() const {
    const QString text = selectedText();
    if (text.isEmpty())
        return;
    QApplication::clipboard()->setText(text);
}

QString SelectableTextLayer::simulateDragForTest(QPointF start, QPointF end) {
    // Press
    m_dragging = true;
    m_dragStartView = start;
    m_dragCurrentView = start;
    // Move (which also covers release behaviour for selection
    // purposes — see mouseReleaseEvent below).
    m_dragCurrentView = end;
    setSelection(hitBlocksForDrag(m_dragStartView, m_dragCurrentView));
    // Release
    m_dragging = false;
    return selectedText();
}

void SelectableTextLayer::paintEvent(QPaintEvent * /*event*/) {
    rebuildViewBlocks();
    if (m_viewBlocks.empty() && !m_dragging)
        return;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QColor base = palette().color(QPalette::Highlight);
    QColor selFill = base;
    selFill.setAlpha(80);
    QColor selOutline = base;
    selOutline.setAlpha(160);
    painter.setPen(QPen(selOutline, 1.0));
    painter.setBrush(selFill);
    for (int idx : m_selectedBlockIds) {
        auto it = std::find_if(m_viewBlocks.begin(), m_viewBlocks.end(),
                               [idx](const ViewBlock &vb) { return vb.blockIndex == idx; });
        if (it == m_viewBlocks.end())
            continue;
        painter.drawPolygon(it->polygon);
    }
}

void SelectableTextLayer::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    const QPointF pos = event->position();
    if (!isPointOverText(pos)) {
        // Click on empty space clears the selection (matches the
        // standard text-selection idiom).
        if (!m_selectedBlockIds.empty()) {
            setSelection({});
        }
        // Fall through — we don't accept(), so the click can
        // propagate to whatever sits beneath us.
        event->ignore();
        return;
    }
    setFocus(Qt::MouseFocusReason);
    m_dragging = true;
    m_dragStartView = pos;
    m_dragCurrentView = pos;
    setSelection(hitBlocksForDrag(m_dragStartView, m_dragCurrentView));
    event->accept();
}

void SelectableTextLayer::mouseMoveEvent(QMouseEvent *event) {
    const QPointF pos = event->position();
    if (m_dragging) {
        m_dragCurrentView = pos;
        setSelection(hitBlocksForDrag(m_dragStartView, m_dragCurrentView));
        event->accept();
        return;
    }
    setCursor(cursorShapeFor(pos));
    event->ignore();
}

void SelectableTextLayer::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    if (!m_dragging) {
        event->ignore();
        return;
    }
    m_dragCurrentView = event->position();
    setSelection(hitBlocksForDrag(m_dragStartView, m_dragCurrentView));
    m_dragging = false;
    event->accept();
}

void SelectableTextLayer::keyPressEvent(QKeyEvent *event) {
    if (event->matches(QKeySequence::Copy)) {
        copySelectionToClipboard();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && !m_selectedBlockIds.empty()) {
        setSelection({});
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void SelectableTextLayer::focusOutEvent(QFocusEvent *event) {
    // Keep the selection painted but stop the drag. The user might be
    // tabbing to a sidebar to inspect what they copied; preserving
    // the visible highlight is the friendly default.
    m_dragging = false;
    QWidget::focusOutEvent(event);
}

} // namespace trailer
