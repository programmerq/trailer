#pragma once

#include "annotation/Annotation.h"

#include <QImage>
#include <QPointF>
#include <QPointer>
#include <QRectF>
#include <QString>
#include <QWidget>

#include <functional>
#include <unordered_map>

namespace trailer {

class AnnotationStore;

class AnnotationOverlay : public QWidget {
    Q_OBJECT

public:
    explicit AnnotationOverlay(QWidget* parent = nullptr);

    void setStore(AnnotationStore* store);
    void setActiveTool(AnnotationTool tool);
    AnnotationTool activeTool() const { return m_tool; }
    // When non-empty, Text annotations drop with this preset content
    // and skip the multi-line input dialog. Used by FormToolbar's
    // Checkmark / X Mark tools to stamp ✓/✗ glyphs on the page. The
    // preset persists until tool changes or this is cleared.
    void setPendingTextPreset(const QString& text) { m_pendingTextPreset = text; }
    QString pendingTextPreset() const { return m_pendingTextPreset; }
    // When the Signature tool is active, the next drag creates a
    // Signature annotation referring to this PNG. Cleared (along with
    // the tool) when the active tool moves away from Signature.
    void setPendingSignaturePath(const QString& path) { m_pendingSignaturePath = path; }
    QString pendingSignaturePath() const { return m_pendingSignaturePath; }
    void setStyle(const AnnotationStyle& style);
    const AnnotationStyle& style() const { return m_style; }
    void setPage(int page);

    // Mapping between document-native coordinates and overlay (view) pixels.
    // The page parameter lets multi-page viewers (e.g. QPdfView in
    // Continuous mode) resolve per-page offsets. Callers must keep the
    // callbacks in sync with the underlying view when the zoom changes.
    using DocToView  = std::function<QPointF(QPointF docPt, int page)>;
    using ViewToDoc  = std::function<QPointF(QPointF viewPt, int page)>;
    using PageAtView = std::function<int(QPointF viewPt)>;
    void setDocumentToView(DocToView fn);
    void setViewToDocument(ViewToDoc fn);
    // Resolves the page under a given view point. Used when the user starts
    // a drag so new annotations land on the right page. If unset, the
    // current m_page is used.
    void setPageAtViewPoint(PageAtView fn);

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
    QRectF docRectToView(const QRectF& r, int page) const;
    QPointF toDoc(const QPointF& viewPt, int page) const;
    int pageAt(const QPointF& viewPt) const;
    int hitTest(const QPointF& viewPt) const;
    void openInlineEditor(int annotationId);

    QPointer<AnnotationStore> m_store;
    AnnotationTool m_tool = AnnotationTool::None;
    AnnotationStyle m_style;
    int m_page = 0;

    bool m_dragging = false;
    int m_dragPage = 0;
    QPointF m_dragStartDoc;
    QPointF m_dragCurrentDoc;
    std::vector<QPointF> m_inkPoints;
    // Pending text selection while Select tool is active; becomes the
    // source range for the right-click markup menu.
    std::vector<QRectF> m_pendingSelection;
    // Preset for the next Text annotation — bypasses the input dialog
    // when FormToolbar's Checkmark/X tools are active.
    QString m_pendingTextPreset;
    // PNG path the Signature tool will use for the next placement.
    QString m_pendingSignaturePath;
    // Cache of decoded signature PNGs keyed by absolute path, so the
    // overlay doesn't re-decode on every repaint. Mutable because
    // paintEvent() is const-ish in spirit.
    mutable std::unordered_map<std::string, QImage> m_signatureCache;

    DocToView m_docToView;
    ViewToDoc m_viewToDoc;
    PageAtView m_pageAtView;
    TextSelectionProvider m_textSelection;
    SourceSampler m_sourceSampler;

    QPointer<QWidget> m_inlineEditor;
    int m_inlineEditorAnnotationId = 0;
};

}  // namespace trailer
