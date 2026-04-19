#pragma once

#include "annotation/Annotation.h"

#include <QPointF>
#include <QPointer>
#include <QRectF>
#include <QWidget>

#include <functional>

namespace trailer {

class AnnotationStore;

class AnnotationOverlay : public QWidget {
    Q_OBJECT

public:
    explicit AnnotationOverlay(QWidget* parent = nullptr);

    void setStore(AnnotationStore* store);
    void setActiveTool(AnnotationTool tool);
    AnnotationTool activeTool() const { return m_tool; }
    void setStyle(const AnnotationStyle& style);
    const AnnotationStyle& style() const { return m_style; }
    void setPage(int page);

    // Mapping between document-native coordinates and overlay (view) pixels.
    // For images: overlay coord = doc coord * scale. Callers must keep the
    // callbacks in sync with the underlying view when the zoom changes.
    void setDocumentToView(std::function<QPointF(QPointF)> fn);
    void setViewToDocument(std::function<QPointF(QPointF)> fn);

    // Supplies per-run text rects (in doc coords) for a selection between two
    // points on a page. Used by the Highlight/Underline/StrikeOut tools. If
    // unset or it returns empty, the markup falls back to the drag bbox.
    using TextSelectionProvider = std::function<std::vector<QRectF>(
        QPointF startDoc, QPointF endDoc, int page)>;
    void setTextSelectionProvider(TextSelectionProvider fn);

    // Samples the underlying document at (docRect, page) and returns an
    // image of the requested pixel size. Used by ZoomLens to draw a
    // magnified view. If unset, ZoomLens renders as an empty circle.
    using SourceSampler = std::function<QImage(
        QRectF docRect, QSize outPixels, int page)>;
    void setSourceSampler(SourceSampler fn);

signals:
    void annotationCommitted(int id);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    QRectF docRectToView(const QRectF& r) const;
    QPointF toDoc(const QPointF& viewPt) const;
    int hitTest(const QPointF& viewPt) const;
    void openInlineEditor(int annotationId);

    QPointer<AnnotationStore> m_store;
    AnnotationTool m_tool = AnnotationTool::None;
    AnnotationStyle m_style;
    int m_page = 0;

    bool m_dragging = false;
    QPointF m_dragStartDoc;
    QPointF m_dragCurrentDoc;
    std::vector<QPointF> m_inkPoints;
    // Pending text selection while Select tool is active; becomes the
    // source range for the right-click markup menu.
    std::vector<QRectF> m_pendingSelection;

    std::function<QPointF(QPointF)> m_docToView;
    std::function<QPointF(QPointF)> m_viewToDoc;
    TextSelectionProvider m_textSelection;
    SourceSampler m_sourceSampler;

    QPointer<QWidget> m_inlineEditor;
    int m_inlineEditorAnnotationId = 0;
};

}  // namespace trailer
