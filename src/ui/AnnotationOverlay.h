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

signals:
    void annotationCommitted(int id);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    QRectF docRectToView(const QRectF& r) const;
    QPointF toDoc(const QPointF& viewPt) const;

    QPointer<AnnotationStore> m_store;
    AnnotationTool m_tool = AnnotationTool::None;
    AnnotationStyle m_style;
    int m_page = 0;

    bool m_dragging = false;
    QPointF m_dragStartDoc;
    QPointF m_dragCurrentDoc;
    std::vector<QPointF> m_inkPoints;

    std::function<QPointF(QPointF)> m_docToView;
    std::function<QPointF(QPointF)> m_viewToDoc;
};

}  // namespace trailer
