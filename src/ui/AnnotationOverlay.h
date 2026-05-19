#pragma once

#include "annotation/Annotation.h"

#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QPointer>
#include <QRectF>
#include <QString>
#include <QVector>
#include <QWidget>

#include <functional>
#include <unordered_map>
#include <vector>

class QTabletEvent;

namespace trailer {

class AnnotationStore;
class SamController;

class AnnotationOverlay : public QWidget {
    Q_OBJECT

  public:
    explicit AnnotationOverlay(QWidget *parent = nullptr);

    void setStore(AnnotationStore *store);
    void setActiveTool(AnnotationTool tool);
    AnnotationTool activeTool() const { return m_tool; }
    // When non-empty, Text annotations drop with this preset content
    // and skip the multi-line input dialog. Used by FormToolbar's
    // Checkmark / X Mark tools to stamp ✓/✗ glyphs on the page. The
    // preset persists until tool changes or this is cleared.
    void setPendingTextPreset(const QString &text) { m_pendingTextPreset = text; }
    QString pendingTextPreset() const { return m_pendingTextPreset; }
    // When the Signature tool is active, the next drag creates a
    // Signature annotation referring to this PNG. Cleared (along with
    // the tool) when the active tool moves away from Signature.
    void setPendingSignaturePath(const QString &path) { m_pendingSignaturePath = path; }
    QString pendingSignaturePath() const { return m_pendingSignaturePath; }
    void setStyle(const AnnotationStyle &style);
    const AnnotationStyle &style() const { return m_style; }
    void setPage(int page);

    // Mapping between document-native coordinates and overlay (view) pixels.
    // The page parameter lets multi-page viewers (e.g. QPdfView in
    // Continuous mode) resolve per-page offsets. Callers must keep the
    // callbacks in sync with the underlying view when the zoom changes.
    using DocToView = std::function<QPointF(QPointF docPt, int page)>;
    using ViewToDoc = std::function<QPointF(QPointF viewPt, int page)>;
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
    using TextSelectionProvider =
        std::function<std::vector<QRectF>(QPointF startDoc, QPointF endDoc, int page)>;
    void setTextSelectionProvider(TextSelectionProvider fn);

    // Samples the underlying document at (docRect, page) and returns an
    // image of the requested pixel size. Used by ZoomLens to draw a
    // magnified view. If unset, ZoomLens renders as an empty circle.
    using SourceSampler = std::function<QImage(QRectF docRect, QSize outPixels, int page)>;
    void setSourceSampler(SourceSampler fn);

    // Wire in the SAM controller + a callback that returns the current
    // raster image in document-coordinate pixels. Used by the
    // InstantAlpha / SmartLasso tools to drive the encoder prepare on
    // activation and live segmentation during drag. `imageProvider`
    // returns a null QImage when the active doc is not a raster image
    // (PDFs etc) — the overlay's tool branches gate on that to no-op.
    // The commit callbacks (`onInstantAlphaCommit`, `onSmartLassoCommit`)
    // are invoked with the final result on a successful gesture; they
    // are wired by MainWindow to push the result through ImageDocument.
    using ImageProvider = std::function<QImage()>;
    using InstantAlphaCommit = std::function<void(const QImage &alphaImage)>;
    using SmartLassoCommit = std::function<void(const QPolygon &polygon)>;
    void setSamController(SamController *controller);
    SamController *samController() const { return m_samController; }
    void setSamImageProvider(ImageProvider fn);
    void setInstantAlphaCommitHandler(InstantAlphaCommit fn);
    void setSmartLassoCommitHandler(SmartLassoCommit fn);
    // True iff a SAM tool is the active tool. Test convenience.
    bool isSamToolActiveForTest() const;
    // The current preview mask (Grayscale8, same size as the prepared
    // image). Empty when no preview has been computed. Test seam.
    QImage samPreviewMaskForTest() const { return m_samMask; }
    // Test seam: synthesise a primary positive prompt at the given
    // doc-space point (Instant Alpha workflow). Drives the same code
    // path as a mousePressEvent inside the overlay, minus the QMouseEvent
    // construction. Returns true if the request was forwarded to the
    // controller.
    bool simulateSamPromptForTest(QPointF docPoint, bool positive);

    // Per-match rectangle for the search-highlight pass. The overlay
    // paints siblings in low-opacity yellow and the current match in
    // high-opacity yellow with a thin outline — the "highlighter
    // marker" look. Driven by the document adapter (PdfDocument
    // queries QPdfSearchModel and pushes the list here on every
    // model/index change).
    struct SearchHighlight {
        int page;
        QRectF rect;
        bool isCurrent;
    };
    void setSearchHighlights(std::vector<SearchHighlight> highlights);
    // Convenience for tests: how many match rects the overlay is
    // currently holding.
    int searchHighlightCountForTest() const {
        return static_cast<int>(m_searchHighlights.size());
    }

    // The id of the currently-selected annotation (0 = none). Public
    // so MainWindow can wire keyboard shortcuts (Delete, arrows) and
    // future Inspector restyle. Tests use this to verify selection
    // happened without needing access to private state.
    int selectedAnnotationId() const { return m_selectedAnnotationId; }
    // Returns all currently-selected annotation ids (including the
    // primary). Empty when nothing is selected.
    std::vector<int> selectedAnnotationIds() const;
    // Select all annotations in the store. Switches focus to the overlay
    // so Delete / arrow keys work immediately. No-op when the store is
    // empty or unset.
    void selectAll();
    // True when the user is currently dragging a resize handle. Used
    // by tests to confirm the handle hit-test fired; in production
    // it has no other consumer.
    bool isResizingForTest() const { return m_resizingHandle != ResizeHandle::None; }
    // View-space rect of the currently-selected annotation. Empty
    // QRectF when nothing is selected. Used by tests to compute
    // handle positions without depending on internal docRectToView
    // arithmetic.
    QRectF selectedViewRectForTest() const;

  signals:
    void annotationCommitted(int id);
    // Fires whenever m_selectedAnnotationId changes (including on
    // clear-to-zero). MainWindow uses this to drive the Inspector
    // pane's "Selection" section so the user gets per-annotation
    // colour / stroke / font controls without an extra click.
    void selectionChanged(int id);

  protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void tabletEvent(QTabletEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

  private:
    QRectF docRectToView(const QRectF &r, int page) const;
    QPointF toDoc(const QPointF &viewPt, int page) const;
    int pageAt(const QPointF &viewPt) const;
    int hitTest(const QPointF &viewPt) const;

    // SAM workflow helpers.
    bool isSamTool() const {
        return m_tool == AnnotationTool::InstantAlpha || m_tool == AnnotationTool::SmartLasso;
    }
    // Push the current SAM prompt set to the controller for a fresh
    // decoder pass. The callback stashes the mask on the overlay and
    // triggers a repaint.
    void requestSamPreview();
    // Restart the SAM workflow for the current tool — drop the
    // accumulated prompts, clear the preview mask, kick off a
    // prepare() if not already cached.
    void resetSamState();
    // Commit Instant Alpha — apply the last mask to the source image
    // as alpha and hand it back through the commit handler. Drops the
    // tool state afterward so the next gesture starts fresh.
    void commitInstantAlpha();
    // Commit Smart Lasso — emit the polygon from the last mask. Drops
    // the tool state afterward.
    void commitSmartLasso();
    void openInlineEditor(int annotationId);
    // Cancel any in-flight drag (shape-creation, selection move,
    // resize handle) without committing it to the AnnotationStore.
    // Wired to QGuiApplication::applicationStateChanged so a
    // Cmd-Tab away mid-drag doesn't leave a half-placed shape that
    // (a) is invisible because focus is gone and (b) bypasses the
    // mouseRelease commit so undo has nothing to pop.
    void abortInFlightDrag();
    // Translate the bounds of the selected annotation by `dx`, `dy`
    // doc-space points. No-op when nothing is selected. Emits the
    // store's changed() signal so undo / dirty propagate normally.
    void nudgeSelected(double dx, double dy);

    QPointer<AnnotationStore> m_store;
    AnnotationTool m_tool = AnnotationTool::None;
    AnnotationStyle m_style;
    int m_page = 0;

    bool m_dragging = false;
    int m_dragPage = 0;
    QPointF m_dragStartDoc;
    QPointF m_dragCurrentDoc;
    std::vector<QPointF> m_inkPoints;
    // Per-sample pressure (0..1) parallel to m_inkPoints. Captured
    // from QPointerEvent::points().pressure() for mouse / trackpad
    // and from QTabletEvent::pressure() for stylus input. Stays
    // empty when no sample reported a non-zero pressure, in which
    // case the resulting Ink annotation drops back to a constant
    // stroke width at render time.
    std::vector<float> m_inkPressures;
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
    // Search-match rectangles to paint underneath annotations on
    // every repaint. Refreshed from the document adapter whenever
    // the search model or current-match index changes. Empty when
    // no search is active.
    std::vector<SearchHighlight> m_searchHighlights;

    DocToView m_docToView;
    ViewToDoc m_viewToDoc;
    PageAtView m_pageAtView;
    TextSelectionProvider m_textSelection;
    SourceSampler m_sourceSampler;

    // SAM tool plumbing. The overlay does not own the controller —
    // MainWindow does — and merely forwards drag events into it.
    SamController *m_samController = nullptr;
    ImageProvider m_samImageProvider;
    InstantAlphaCommit m_instantAlphaCommit;
    SmartLassoCommit m_smartLassoCommit;
    // Latest preview mask emitted by the controller, in source-image
    // pixel coordinates. Repainted on every overlay paint when a SAM
    // tool is active.
    QImage m_samMask;
    // Prompt points in source-image pixel coordinates. Instant Alpha
    // ever uses a single positive (refreshed on every drag-update);
    // Smart Lasso accumulates points across clicks until commit.
    QVector<QPoint> m_samPositives;
    QVector<QPoint> m_samNegatives;
    // True while the encoder is preparing — the cursor briefly turns
    // into a wait shape so the user knows the first click won't fire
    // until prepare lands.
    bool m_samPreparing = false;
    // True while the user is mid-drag with Instant Alpha (button down).
    // mouseMove updates the single positive point; mouseRelease commits
    // the alpha cut to the document.
    bool m_samDraggingInstant = false;

    QPointer<QWidget> m_inlineEditor;
    int m_inlineEditorAnnotationId = 0;
    // True when openInlineEditor was triggered for a freshly-placed
    // annotation (Text, Note, SpeechBubble drop). On cancel (Esc) we
    // remove the placeholder rather than leaving an empty stamp on
    // the page; on commit-with-empty-text we likewise delete it.
    bool m_inlineEditorIsNew = false;

    // Annotation-editing selection state. m_selectedAnnotationId is
    // the persistent "this is the active shape" pointer (0 = none).
    // m_movingSelected and m_moveStartDoc cover the drag-to-move
    // gesture: clicking on an already-selected annotation begins a
    // move; subsequent mouse-move events translate its bounds; the
    // mouse-release commits the new position to the store.
    int m_selectedAnnotationId = 0;
    // Additional selected annotation ids accumulated by selectAll().
    // These are rendered with a selection outline but do not receive
    // move / resize handles — the primary (m_selectedAnnotationId)
    // is the interactive one.  Cleared on any single-click selection
    // change.
    std::vector<int> m_extraSelectedIds;
    bool m_movingSelected = false;
    QPointF m_moveStartDoc;
    QRectF m_moveOriginalBounds;

    // Resize drag state. m_resizingHandle != None when the user is
    // dragging one of the four corner handles; the bounds shift in
    // doc-space according to which corner is being moved.
    enum class ResizeHandle { None, TopLeft, TopRight, BottomLeft, BottomRight };
    ResizeHandle m_resizingHandle = ResizeHandle::None;
    QPointF m_resizeStartDoc;
    QRectF m_resizeOriginalBounds;

    // Helper: which corner-handle (if any) lives at this view-space
    // point for the currently-selected annotation. Returns None
    // when nothing's selected or the click missed the ~6 px handle
    // hit zone (shrunk from 10 to keep body-clicks on short
    // Line/Arrow annotations from being eaten by the corner).
    ResizeHandle handleAt(const QPointF &viewPt) const;
    // Compute the four handle rects in view space for the given
    // annotation bounds.
    QRectF handleRect(const QRectF &viewBounds, ResizeHandle which) const;
};

} // namespace trailer
