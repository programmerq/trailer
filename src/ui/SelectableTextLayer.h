#pragma once

#include "document/SelectableTextStore.h"

#include <QPointer>
#include <QPolygonF>
#include <QRectF>
#include <QString>
#include <QWidget>

#include <functional>
#include <vector>

namespace trailer {

// Transparent overlay that sits between the document viewport and the
// AnnotationOverlay. Reads OCR results from the document's
// SelectableTextStore and provides:
//
//   - An honest I-beam cursor — only shown when the cursor is inside
//     one of the cached text-block polygons for the current page.
//     (The unconditional I-beam previously set by
//     AnnotationOverlay::setActiveTool went away with this layer.)
//   - Drag-to-select between two view points; selection snaps to
//     block boundaries (word-level granularity is a follow-up,
//     PP-OCRv3 emits per-region only).
//   - Ctrl+C / Cmd+C copies the joined selected text to the
//     clipboard in reading order.
//
// Coordinate mapping is provided by the adapter via two callbacks:
//
//   - docToView(p, page) — translate document-coordinate point on
//     `page` to a view-pixel point (used to paint cached blocks).
//   - pageAtView(p) — resolve which page the view-pixel point falls
//     on (used to hit-test the active page).
//
// Documents with text layers (PDFs) can still install this layer; the
// store is populated either by an explicit Recognize Text run or by
// future OCR-against-vector-text passes. For now QPdfView's native
// selection is unaffected — we only paint on top of the viewport for
// pages that have OCR results.
class SelectableTextLayer : public QWidget {
    Q_OBJECT
  public:
    explicit SelectableTextLayer(QWidget *parent = nullptr);

    // Attach the document's store. Disconnects from any previously-
    // attached store. Triggers a repaint when the store emits
    // pageChanged().
    void setStore(SelectableTextStore *store);
    SelectableTextStore *store() const { return m_store.data(); }

    // Coordinate-mapping hooks. The adapter owns the maths; this
    // layer just turns the polygons in `store` into view-pixel
    // shapes.
    using DocToView = std::function<QPointF(QPointF docPt, int page)>;
    using PageAtView = std::function<int(QPointF viewPt)>;
    void setDocToView(DocToView fn);
    void setPageAtView(PageAtView fn);

    // The page that is "current" for cursor/selection purposes. For
    // single-page viewers this is the only visible page; for
    // continuous PDF views the adapter pushes the page-at-current-
    // scroll-position. Defaults to 0.
    void setCurrentPage(int page);
    int currentPage() const { return m_currentPage; }

    // True iff the cursor at `viewPt` lands inside a cached text
    // block on whatever page the view-resolver returns. Exposed for
    // tests; the production path inspects this in mouseMoveEvent.
    bool isPointOverText(QPointF viewPt) const;

    // The joined text of the current selection, in reading order
    // (top-to-bottom, then left-to-right). Empty when no blocks are
    // selected. Exposed for tests; Ctrl+C reads this for the
    // clipboard.
    QString selectedText() const;

    // The number of blocks the current drag has selected. Exposed
    // for tests so a unit can assert "drag covered two blocks".
    int selectedBlockCount() const { return static_cast<int>(m_selectedBlockIds.size()); }

    // Synthesise the same logic mouseMoveEvent uses, without needing
    // a real QMouseEvent — useful in tests that don't want to spin
    // up a QApplication-level event loop. Returns the cursor shape
    // that the real event handler would have ended up with.
    Qt::CursorShape cursorShapeFor(QPointF viewPt) const;

    // Test seam: synthesise a left-button drag from `start` to
    // `end` in view-pixel coordinates. Mirrors the press → move →
    // release sequence and updates selection state the same way a
    // real event sequence does. Returns selectedText() after
    // release. Tests can call this without instantiating QMouseEvent.
    QString simulateDragForTest(QPointF start, QPointF end);

  signals:
    void selectionChanged();

  protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

  private:
    // Per-cached-block view-space polygon, computed lazily from
    // store + maps when the page or zoom changes.
    struct ViewBlock {
        int blockIndex;
        QPolygonF polygon;
        QRectF bounds;
    };

    // Refresh the m_viewBlocks cache from the store for
    // m_currentPage. Cheap-ish; called on every paint and hit-test.
    // Without a cache-bust signal we'd be recomputing on every
    // mouse-move, so the cache key is (page, store-generation,
    // widget-size) — when any changes we re-build.
    void rebuildViewBlocks() const;

    // Pick the indices of blocks whose centroid lies between the
    // drag start and current point, sorted in reading order.
    std::vector<int> hitBlocksForDrag(QPointF startView, QPointF endView) const;

    // Apply the drag-selection to m_selectedBlockIds and emit.
    void setSelection(std::vector<int> ids);

    void copySelectionToClipboard() const;

    QPointer<SelectableTextStore> m_store;
    DocToView m_docToView;
    PageAtView m_pageAtView;
    int m_currentPage = 0;

    bool m_dragging = false;
    QPointF m_dragStartView;
    QPointF m_dragCurrentView;
    std::vector<int> m_selectedBlockIds;

    // Lazy cache: m_viewBlocks is keyed by m_cachePage / the store's
    // current contents. We don't have a fine-grained "store
    // generation" counter, so cache invalidation is on every
    // pageChanged() / on every setStore(). For Phase 6 this is
    // good enough; if it shows up in profiling, swap for a real
    // monotonic counter.
    mutable std::vector<ViewBlock> m_viewBlocks;
    mutable int m_cachePage = -1;
    mutable bool m_cacheDirty = true;
};

} // namespace trailer
