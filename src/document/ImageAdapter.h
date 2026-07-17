#pragma once

#include "IDocument.h"
#include "IFormatAdapter.h"
#include "SelectableTextStore.h"
#include "annotation/AnnotationStore.h"

#include <QImage>
#include <QObject>
#include <QPixmap>
#include <QPointer>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <memory>
#include <vector>

class QLabel;
class QMovie;
class QScrollArea;

namespace trailer {

class AnnotationOverlay;
class SelectableTextLayer;

class ImageDocument : public IDocument {
  public:
    explicit ImageDocument(QString path);
    ~ImageDocument() override;

    QString displayName() const override;
    QString filePath() const override;
    QWidget *createView(QWidget *parent) override;

    DocumentType documentType() const override { return DocumentType::Image; }

    bool supportsZoom() const override { return !m_animated && !m_image.isNull(); }
    void zoomIn() override;
    void zoomOut() override;
    void zoomActual() override;
    void zoomFitWidth() override;
    void zoomFitPage() override;
    QSize contentSizeHint() const override {
        // LOGICAL size (device px / devicePixelRatio) so the window opens
        // at the size the image occupies on screen at 100%. For a Retina
        // screenshot (device px stamped dpr=2) this is half the raw px —
        // avoiding the "open oversized then shrink to fit" behavior.
        return m_image.isNull() ? QSize{} : m_image.deviceIndependentSize().toSize();
    }

    ZoomMode zoomMode() const override { return m_zoomMode; }
    double zoomFactor() const override { return m_scale; }
    void applyZoomState(ZoomMode mode, double factor) override;
    int scrollY() const override;
    void applyScrollY(int y) override;

    bool supportsPrint() const override { return !m_image.isNull(); }
    void print(QWidget *dialogParent) override;

    // Search over the image's OCR results (Item A). Unlike PdfDocument —
    // which searches native PDF text via QPdfSearchModel — an image has no
    // native text, so search runs a case-insensitive substring scan over
    // the SelectableTextStore blocks that OCR produced for page 0. Match
    // rectangles come straight from block geometry and are pushed into the
    // AnnotationOverlay's search-highlight pass, exactly as PdfDocument
    // feeds its overlay. Works headlessly (no view) so callers can seed the
    // store and query without a widget.
    // Gate search on the same capability that lets the OCR store populate:
    // an animated GIF or a null image can never gain a searchable text
    // layer (maybeKickSearchOcr bails on !supportsSelectableText), so
    // lighting up Find for them would be a dead control.
    bool supportsSearch() const override { return supportsSelectableText(); }
    void setSearchQuery(const QString &query) override;
    void findNext() override;
    void findPrevious() override;
    void clearSearch() override;
    int searchMatchCount() const override {
        return static_cast<int>(m_searchMatches.size());
    }
    int currentSearchMatchIndex() const override {
        return m_currentMatch < 0 ? -1 : m_currentMatch + 1;
    }
    std::vector<int> pagesWithSearchMatches() const override {
        return m_searchMatches.empty() ? std::vector<int>{} : std::vector<int>{0};
    }

    bool supportsThumbnails() const override { return !m_image.isNull() && !m_animated; }
    QImage renderThumbnail(int pageIndex, QSize targetSize) override;
    // Natural pixel size of the single image page (page 0). Empty for a
    // null image or any other page index. No rendering — used by the
    // sidebar to size the thumbnail row by aspect.
    QSizeF pageSizeHint(int pageIndex) const override {
        return (pageIndex == 0 && !m_image.isNull()) ? QSizeF(m_image.size())
                                                     : QSizeF();
    }

    AnnotationStore *annotations() override { return &m_annotations; }
    SelectableTextStore *selectableText() override { return &m_selectableText; }
    bool supportsSelectableText() const override { return !m_image.isNull() && !m_animated; }
    QImage renderPageForOcr(int pageIndex) const override {
        return pageIndex == 0 ? m_image : QImage();
    }
    void setAnnotationTool(AnnotationTool tool) override;
    void setAnnotationStyle(const AnnotationStyle &style) override;
    void setPendingAnnotationText(const QString &text) override;
    void setPendingSignaturePath(const QString &path) override;

    bool supportsEditing() const override { return !m_image.isNull() && !m_animated; }
    bool isDirty() const override { return m_dirty || !m_annotations.annotations().empty(); }
    // Image-level undo runs across two parallel stacks: the
    // AnnotationStore for in-memory shape edits, and the pixel
    // snapshot stack for raster mutations (rotate / flip / resize /
    // crop / colour / replaceImage). Undo/redo pop a single
    // chronological log (m_undoLog) recording which stack each
    // committed op went to — same structure as PdfDocument — so the
    // most recent action is always undone first regardless of domain.
    bool canUndo() const override { return !m_undoLog.empty(); }
    bool canRedo() const override { return !m_redoLog.empty(); }
    bool undo() override;
    bool redo() override;
    void rotatePage(int pageIndex, int degreesClockwise) override;
    void flipHorizontal() override;
    void flipVertical() override;
    bool resizeImage(int width, int height, bool smoothScaling) override;
    bool cropToRect(int x, int y, int width, int height) override;
    QSize imagePixelSize() const override { return m_image.size(); }
    // Read-only access to the current raster buffer. Used by Phase 6
    // features (background removal, instant alpha, Smart Lasso) that
    // feed pixels into ONNX models. Returns a shallow copy — QImage is
    // copy-on-write, so this is cheap.
    QImage image() const { return m_image; }
    bool adjustColour(double brightness, double contrast, double saturation) override;
    void previewColour(double brightness, double contrast, double saturation);
    void clearColourPreview();
    bool replaceImage(const QImage &replacement) override;
    bool exportAs(const QString &destPath, const QString &format, int quality = -1,
                  const QString &filterId = {}) const override;
    bool save(const QString &newPath = {}) override;
    int pageCount() const override { return m_image.isNull() ? 0 : 1; }

    bool supportsAnimation() const override { return m_animated && m_frameCount > 1; }
    int frameCount() const override { return m_frameCount; }
    int currentFrame() const override;
    void setCurrentFrame(int frame) override;
    bool isAnimationPlaying() const override;
    void setAnimationPlaying(bool playing) override;

    // Test hook + resize callback. Reapplies the currently-active fit
    // mode (no-op for ZoomMode::Custom / Actual). Used internally by
    // the view's resize watcher; tests can call it directly to
    // simulate a resize without needing a real Qt widget hierarchy.
    void reapplyFitMode();
    double scaleFactor() const { return m_scale; }

    // Mark this document as originating from a screen capture
    // (screenshot or clipboard grab) taken at devicePixelRatio `dpr`.
    // Stamps that dpr onto the decoded image so the viewer treats it as
    // logical-size = device/dpr, and flips the initial zoom default to
    // Actual Size (1:1 pixel-exact) instead of fit-capped-at-100%. No-op
    // for a null / animated image or dpr <= 0; the dpr stamp itself is
    // applied only when dpr > 1, so ordinary (non-capture) file opens are
    // never touched. Called by the screenshot / clipboard capture paths
    // (see Application / MainWindow) which know the real screen dpr.
    void markCaptureOrigin(double dpr);

    // --- Test hooks (narrow, offscreen unit tests only) ---
    // Inject an already-decoded image (with its devicePixelRatio stamp)
    // directly, bypassing the file reader and the lossy PNG round-trip,
    // and optionally flag it as capture-origin. Lets HiDPI tests exercise
    // dpr > 1 without a real Retina display.
    void setImageForTest(const QImage &img, bool captureOrigin = false);
    // The pixmap currently handed to the display label (raw device pixels
    // + its devicePixelRatio stamp). Empty when no view / null pixmap.
    QPixmap labelPixmapForTest() const;
    // Force the one-shot initial-zoom decision synchronously (production
    // schedules it on the event loop after the viewport settles).
    void triggerInitialZoomForTest() { applyInitialFitZoom(); }
    // The exact doc<->view transforms the overlay / text layer use, exposed
    // so a test can assert they invert (docToView(viewToDoc(p)) == p) at
    // dpr > 1 and non-1.0 scale without needing a real widget/event loop.
    QPointF docToViewForTest(QPointF p) const { return mapDocToView(p); }
    QPointF viewToDocForTest(QPointF p) const { return mapViewToDoc(p); }

  private:
    // The image's effective devicePixelRatio, clamped to a positive
    // value. 1.0 for ordinary opens; the capture dpr (>1) for HiDPI
    // screenshots / clipboard grabs after markCaptureOrigin stamps it.
    qreal imageDpr() const {
        const qreal d = m_image.devicePixelRatio();
        return d > 0.0 ? d : 1.0;
    }
    // Doc<->view coordinate mapping used by the annotation overlay and the
    // selectable-text layer. Doc coordinates are image DEVICE pixels; the
    // view draws at the logical size (device / dpr) times the logical zoom,
    // so the doc->view factor is m_scale / dpr (== m_scale at dpr 1) and
    // view->doc is its exact inverse. Factored into members (rather than
    // inlined per-lambda) so both consumers — and the coordinate round-trip
    // test hooks — share one definition and can't drift apart.
    QPointF mapDocToView(QPointF p) const {
        const qreal d = imageDpr();
        return QPointF(p.x() * m_scale / d, p.y() * m_scale / d);
    }
    QPointF mapViewToDoc(QPointF p) const {
        if (m_scale <= 0.0)
            return p;
        const qreal d = imageDpr();
        return QPointF(p.x() * d / m_scale, p.y() * d / m_scale);
    }
    void applyScale(double factor);
    void refreshView();
    void pushUndoSnapshot();
    // AnnotationStore mirror hooks, connected to historyPushed /
    // historyEvicted in the constructor. The store owns the annotation
    // history depth; these keep the chronological log's Annotation
    // entries in lockstep with the store's undo stack so the log never
    // claims an undo the store cannot perform. Mirrors PdfDocument.
    void onAnnotationHistoryPushed();
    void onAnnotationHistoryEvicted();
    void connectAnnotationHistory();
    // Installed as an event filter on the QScrollArea's viewport so
    // we get notified when the user resizes the window. The viewport
    // is a child of the scroll area; QResizeEvents on it correspond
    // exactly to changes in the available drawing area for fit modes.
    void installResizeWatcher();
    // Rebuild m_searchMatches from the store for the current query, reset
    // the current-match cursor, and refresh overlay highlights. Connected
    // to the store's changed() signal so OCR results that land after a
    // query was typed (on-demand OCR) surface as matches automatically.
    void recomputeSearchMatches();
    // Push the current match rectangles into the AnnotationOverlay's
    // search-highlight pass (no-op without a view), flagging the current
    // match. Also scrolls the current match into view when a scroll area
    // exists. Mirrors PdfDocument::refreshSearchHighlights.
    void refreshSearchHighlights();
    // Fit the image into the scroll viewport on first show. Capped at
    // 100% so small icons don't blow up to fill the window. One-shot
    // — later opens / re-shows keep whatever scale the user picked.
    void applyInitialFitZoom();

    QString m_path;
    QImage m_image;
    QPointer<QScrollArea> m_scroll;
    QPointer<QLabel> m_label;
    // The most recent pixmap built for the display label (raw device
    // pixels + devicePixelRatio stamp). Retained for test introspection:
    // QLabel::pixmap() re-derives a logical-size, dpr=1 copy, so it
    // cannot report the dpr/raw-size we actually handed it.
    QPixmap m_lastBuiltPixmap;
    QPointer<QMovie> m_movie;
    QPointer<AnnotationOverlay> m_overlay;
    QPointer<SelectableTextLayer> m_textLayer;
    QPointer<QObject> m_resizeWatcher;
    // Sentinel shared with the resize watcher (which is a QObject
    // parented to a Qt widget and may outlive `this`). Flipped to
    // false in our destructor so the watcher's eventFilter stops
    // dereferencing this document.
    std::shared_ptr<bool> m_aliveFlag;
    AnnotationStore m_annotations;
    SelectableTextStore m_selectableText;
    std::vector<QImage> m_undoStack;
    std::vector<QImage> m_redoStack;
    // Unified chronological undo/redo log: one entry per committed op,
    // recording which stack it went to, so undo()/redo() pop the truly
    // most-recent op regardless of source. Same shape as PdfDocument's
    // log (whose second domain is PdfCommand instead of ImageOp).
    // Entries stay in lockstep with the owning stacks: pixel-snapshot
    // eviction (kMaxUndoSteps) drops the oldest ImageOp entry in
    // pushUndoSnapshot(); annotation eviction drops the oldest
    // Annotation entry via AnnotationStore::historyEvicted().
    enum class UndoSource { Annotation, ImageOp };
    std::vector<UndoSource> m_undoLog;
    std::vector<UndoSource> m_redoLog;
    // Item A image-search state. The query, the doc-space (pixel) match
    // rectangles in reading order, and the 0-based current-match cursor
    // (-1 when there is no match). Backed by the OCR store, not a parallel
    // text store.
    QString m_searchQuery;
    std::vector<QRectF> m_searchMatches;
    int m_currentMatch = -1;
    // The store->changed() → recomputeSearchMatches() connection. Its
    // lambda captures `this` and touches the search-state members above,
    // which are declared AFTER m_selectableText and therefore destroyed
    // BEFORE it — so an emission racing destruction could touch freed
    // members. We disconnect it explicitly in ~ImageDocument (see the
    // dtor) before any member teardown to make the ordering safe by
    // intent rather than by declaration accident.
    QMetaObject::Connection m_searchOcrConnection;
    double m_scale = 1.0;
    // Tracks the user's intent (Fit-page, Fit-width, Actual, custom
    // factor) so the persistence layer can round-trip the mode and
    // the resize watcher can re-fit on viewport changes. The image
    // adapter has no Qt-level mode — the scale is the source of truth
    // at render time — but storing the intent is what makes Ctrl+0
    // survive a window resize and per-file/type defaults work. Default
    // Custom (factor 1.0) for freshly-constructed docs that haven't
    // been shown yet; applyInitialFitZoom flips it to FitInView once
    // the view widget exists.
    ZoomMode m_zoomMode = ZoomMode::Custom;
    int m_frameCount = 0;
    bool m_animated = false;
    bool m_dirty = false;
    // One-shot guard for applyInitialFitZoom.
    bool m_initialZoomApplied = false;
    // True when this document came from a screen capture (screenshot or
    // clipboard grab). Such docs stamp the capture's devicePixelRatio on
    // the image and default the initial zoom to Actual Size (1:1) rather
    // than fit-capped-at-100%. Ordinary file opens leave this false.
    bool m_captureOrigin = false;
};

class ImageAdapter : public IFormatAdapter {
  public:
    QStringList mimeTypes() const override;
    QStringList extensions() const override;
    std::unique_ptr<IDocument> open(const QString &path) override;
};

} // namespace trailer
