#pragma once

#include "CapabilityNotifier.h"
#include "IDocument.h"
#include "IFormatAdapter.h"
#include "SelectableTextStore.h"
#include "annotation/AnnotationStore.h"

#include <QFuture>
#include <QFutureWatcher>
#include <QImage>
#include <QObject>
#include <QPixmap>
#include <QPointer>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QStringList>
#include <cmath>
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

    // Fires once the staged-open (ADR 0008) async decode + initial fit have
    // landed, so MainWindow can refresh the zoom readout against the settled
    // scale (reuses the same wiring PdfDocument uses for its async form probe).
    CapabilityNotifier *capabilityNotifier() override { return &m_capabilityNotifier; }

    bool supportsZoom() const override { return !m_animated && imageAvailableOrPending(); }
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
        //
        // Staged open (ADR 0008, image path): before the async full decode
        // lands, m_image is null but the header-only size read at
        // construction gives the immediate hint. Ordinary file opens are
        // dpr 1, so header px == logical px; a capture stamps its dpr via
        // markCaptureOrigin BEFORE the window is sized, so divide by that
        // known dpr to land the same logical size the decoded image would.
        if (!m_image.isNull())
            return m_image.deviceIndependentSize().toSize();
        if (m_animated || m_headerSize.isEmpty())
            return {};
        const qreal d = m_pendingCaptureDpr > 1.0 ? m_pendingCaptureDpr : 1.0;
        return QSize(static_cast<int>(std::lround(m_headerSize.width() / d)),
                     static_cast<int>(std::lround(m_headerSize.height() / d)));
    }

    ZoomMode zoomMode() const override { return m_zoomMode; }
    double zoomFactor() const override { return m_scale; }
    void applyZoomState(ZoomMode mode, double factor) override;
    int scrollY() const override;
    void applyScrollY(int y) override;

    bool supportsPrint() const override { return imageAvailableOrPending(); }
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

    bool supportsThumbnails() const override { return !m_animated && imageAvailableOrPending(); }
    QImage renderThumbnail(int pageIndex, QSize targetSize) override;
    // Natural pixel size of the single image page (page 0). Empty for a
    // null image or any other page index. No rendering — used by the
    // sidebar to size the thumbnail row by aspect. Uses the header size
    // read at open while the async decode is still pending, so the sidebar
    // can size rows before the pixels land (staged open, ADR 0008).
    QSizeF pageSizeHint(int pageIndex) const override {
        if (pageIndex != 0)
            return {};
        const QSize s = deviceSize();
        return s.isEmpty() ? QSizeF() : QSizeF(s);
    }

    AnnotationStore *annotations() override { return &m_annotations; }
    SelectableTextStore *selectableText() override { return &m_selectableText; }
    bool supportsSelectableText() const override { return !m_animated && imageAvailableOrPending(); }
    QImage renderPageForOcr(int pageIndex) const override {
        if (pageIndex != 0)
            return QImage();
        ensureDecoded(); // OCR needs the real pixels — block on the decode
        return m_image;
    }
    void setAnnotationTool(AnnotationTool tool) override;
    void setAnnotationStyle(const AnnotationStyle &style) override;
    void setPendingAnnotationText(const QString &text) override;
    void setPendingSignaturePath(const QString &path) override;

    bool supportsEditing() const override { return !m_animated && imageAvailableOrPending(); }
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
    QSize imagePixelSize() const override { return deviceSize(); }
    // Read-only access to the current raster buffer. Used by Phase 6
    // features (background removal, instant alpha, Smart Lasso) that
    // feed pixels into ONNX models. Returns a shallow copy — QImage is
    // copy-on-write, so this is cheap.
    QImage image() const {
        ensureDecoded(); // ML/pixel consumers need the real decoded buffer
        return m_image;
    }
    bool adjustColour(double brightness, double contrast, double saturation) override;
    void previewColour(double brightness, double contrast, double saturation);
    void clearColourPreview();
    bool replaceImage(const QImage &replacement) override;
    bool exportAs(const QString &destPath, const QString &format, int quality = -1,
                  const QString &filterId = {}) const override;
    bool save(const QString &newPath = {}) override;
    bool writeRecoverySnapshot(const QString &sidecarPath) override;
    bool recoverFrom(const QString &sidecarPath) override;
    bool reloadFromDisk() override;
    int pageCount() const override { return imageAvailableOrPending() ? 1 : 0; }

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
    // Staged-open (ADR 0008) test hooks.
    // True while the off-GUI-thread full decode kicked at open has NOT yet
    // completed — i.e. the full-pixel decode did not run synchronously on
    // the calling thread. The structural proxy in test_perf_gui_thread_io
    // asserts this immediately after open.
    bool isDecodePendingForTest() const { return m_decodeStarted && !m_decoded; }
    // Spin the event loop until the async decode has swapped in the real
    // pixmap (or the timeout elapses). Returns true once decoded. Lets the
    // view/zoom tests deterministically await the placeholder→pixmap swap
    // before reading label->pixmap() / scaleFactor().
    bool awaitDecodeForTest(int timeoutMs = 5000);
    // True once no in-flight decode watcher is retained — i.e. a
    // reload/recovery that superseded the open decode dropped it (so the
    // superseded worker's buffer isn't pinned and its stale callback can't
    // fire). Also true when no decode was ever started.
    bool decodeWatcherClearedForTest() const { return m_decodeWatcher.isNull(); }

  private:
    // The image's effective devicePixelRatio, clamped to a positive
    // value. 1.0 for ordinary opens; the capture dpr (>1) for HiDPI
    // screenshots / clipboard grabs after markCaptureOrigin stamps it.
    qreal imageDpr() const {
        const qreal d = m_image.devicePixelRatio();
        return d > 0.0 ? d : 1.0;
    }
    // True once the file is known to be a decodable still image — from the
    // header read at open, before the async full decode lands, and after a
    // SUCCESSFUL decode. Keyed to available-OR-in-flight so a still image's
    // capabilities never falsely read as unsupported during the brief
    // off-thread decode window (ADR 0008, image path). Crucially, once the
    // decode has FINISHED (m_decoded) the header-size "pending" no longer
    // counts: a decode that produced a null image (valid header, corrupt
    // body) is genuinely unavailable, so capabilities go false and the
    // controls disable rather than stay enabled-but-inert (G3).
    bool imageAvailableOrPending() const {
        if (!m_image.isNull())
            return true;
        // Only "pending" while a decode is actually still in flight.
        return !m_animated && m_decodeStarted && !m_decoded && !m_headerSize.isEmpty();
    }
    // Device-pixel dimensions of the still image: the decoded size once
    // available, otherwise the header size read at open. Empty for an
    // animated image or an unreadable file.
    QSize deviceSize() const {
        return !m_image.isNull() ? m_image.size()
                                 : (m_animated ? QSize() : m_headerSize);
    }
    // Kick the full-resolution decode on a worker thread (ADR 0008 Option
    // B, image path). Records a new decode generation so a superseding
    // reload/recovery can make the stale finished callback a no-op.
    void startDecode();
    // Block on the in-flight worker decode and adopt its pixels. A no-op
    // once decoded, or when no decode is in flight (empty path / injected
    // test image / already superseded). Does NOT spin the event loop
    // (waitForFinished via QFuture::result), so it is re-entrancy-safe;
    // always called on the GUI thread, so it never races onDecodeFinished.
    void ensureDecoded() const;
    // GUI-thread QFutureWatcher::finished slot: adopt the decoded pixels
    // and, if a view exists and is still showing the placeholder, swap in
    // the real content. Ignores results from a superseded generation.
    void onDecodeFinished(int generation);
    // Create the selectable-text layer + annotation overlay over the view
    // label, up front at createView time (before the staged-open decode
    // lands), so MainWindow's open-time overlay wiring finds them. Idempotent.
    void wireViewLayers();
    // Build the real pixmap into the view label, refresh the layer geometry,
    // and schedule the initial fit-zoom. Shared by the synchronous createView
    // path (already-decoded / injected image) and the async swap. Idempotent
    // via m_viewPopulated.
    void installDecodedContent();
    // Mark any in-flight open decode as superseded (reload / recovery took
    // authoritative pixels): bump the generation so the stale finished
    // callback no-ops, and stop ensureDecoded from blocking on the old
    // future.
    void supersedeDecode();
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
    // Mutable so the const ensureDecoded() can adopt the worker's decoded
    // pixels the first time a const pixel-accessor (image(), exportAs, …)
    // is reached before the async decode has landed.
    mutable QImage m_image;
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

    // --- Staged (async) open state (ADR 0008, image path = Option B) ---
    // Header-only size read at construction for a still image, used for
    // the immediate contentSizeHint / placeholder sizing before the
    // full-pixel decode lands. Empty for animated / unreadable files.
    QSize m_headerSize;
    // The off-GUI-thread full-resolution decode and its GUI-thread watcher.
    QFuture<QImage> m_decodeFuture;
    QPointer<QFutureWatcher<QImage>> m_decodeWatcher;
    // Monotonic decode generation. A reload / recovery bumps it so the
    // stale worker's finished callback no-ops instead of clobbering the
    // authoritative pixels (#89 supersede-not-race).
    int m_decodeGeneration = 0;
    // True once startDecode() has kicked a worker (a still-image file open).
    bool m_decodeStarted = false;
    // True once the decoded pixels have been adopted into m_image
    // (mutable-adopted by ensureDecoded, or by the finished slot, or set
    // directly for injected/animated/recovered images).
    mutable bool m_decoded = false;
    // True once installDecodedContent() has swapped the real pixmap +
    // overlay/text layer into the view, so a late finished callback no-ops.
    bool m_viewPopulated = false;
    // A capture dpr (> 1) recorded by markCaptureOrigin before the decode
    // landed; stamped onto m_image when the pixels are adopted. 0 for
    // ordinary opens (dpr 1, no stamp).
    qreal m_pendingCaptureDpr = 0.0;
    // Fires when the async initial fit lands so MainWindow refreshes the zoom
    // readout (staged open, ADR 0008). Declared last so it outlives nothing
    // that needs it during teardown.
    CapabilityNotifier m_capabilityNotifier;
};

class ImageAdapter : public IFormatAdapter {
  public:
    QStringList mimeTypes() const override;
    QStringList extensions() const override;
    std::unique_ptr<IDocument> open(const QString &path) override;
};

} // namespace trailer
