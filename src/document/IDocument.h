#pragma once

#include "annotation/Annotation.h"
#include "document/ExternalChangeState.h"
#include "document/PdfEditor.h"

#include <QImage>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <QWidget>

#include <vector>

class QAbstractItemModel;
class QModelIndex;

namespace trailer {

class AnnotationStore;
class SelectableTextStore;
class CapabilityNotifier;

// API: session-only today — `ViewMode` is *not* persisted in
// `recent.json`, `settings.toml`, or `DocumentTypeDefaults` (only
// `ZoomMode` is, via string keys). If view-mode persistence is ever
// added, follow the existing `ZoomMode` pattern in
// `src/settings/DocumentTypeDefaults.cpp` (string keys round-tripped
// via a `viewModeKey()` helper) rather than persisting the ordinal,
// so the C++ enum stays renumber-safe.
enum class ViewMode {
    SinglePage,
    TwoPages,
    Continuous,
};

// Coarse classification used by the state-persistence layer to key
// per-type defaults (e.g. "all PDFs open at fit-to-page, all images
// open at actual size"). Adapters return their type from
// IDocument::documentType(). Kept tight on purpose — adding subtypes
// (RasterImage vs Vector, scanned PDF vs born-digital) is a follow-up
// once the per-type defaults UX is validated.
enum class DocumentType {
    Unknown,
    Pdf,
    Image,
};

// Zoom modes that the persistence layer can record and restore.
// Mirrors QPdfView::ZoomMode (Custom / FitInView / FitToWidth) plus
// an Actual entry — the latter is `Custom` with factor 1.0, but
// adapters that don't expose the factor (image at native size) still
// have a meaningful state to round-trip.
enum class ZoomMode {
    Custom,
    FitInView,
    FitToWidth,
    Actual,
};

class IDocument {
  public:
    virtual ~IDocument() = default;

    virtual QString displayName() const = 0;
    virtual QString filePath() const = 0;
    virtual QWidget *createView(QWidget *parent) = 0;

    // Coarse type classification used by DocumentTypeDefaults. Defaults
    // to Unknown so adapters that don't override (StubAdapter) are
    // simply ignored by the persistence layer.
    virtual DocumentType documentType() const { return DocumentType::Unknown; }

    // Current zoom mode + factor. The mode tells the persistence
    // layer how the user is viewing the document (Fit, Width, Custom);
    // the factor is only meaningful when mode == Custom but is always
    // returned for round-trip simplicity. Adapters without a viewer
    // attached fall back to Custom / 1.0.
    virtual ZoomMode zoomMode() const { return ZoomMode::Custom; }
    virtual double zoomFactor() const { return 1.0; }
    // Apply a previously-saved zoom state. `factor` is honoured only
    // when `mode == ZoomMode::Custom`; the fit modes ignore it.
    virtual void applyZoomState(ZoomMode /*mode*/, double /*factor*/) {}

    // Vertical scroll position in viewport pixels. Captured at
    // close-time and applied on reopen so the user lands at the same
    // spot in a long document. Adapters that don't expose a scroll
    // area report 0 and ignore applyScrollY().
    virtual int scrollY() const { return 0; }
    virtual void applyScrollY(int /*y*/) {}

    virtual bool supportsZoom() const { return false; }
    virtual void zoomIn() {}
    virtual void zoomOut() {}
    virtual void zoomActual() {}
    virtual void zoomFitWidth() {}
    // Fit the entire current page/image in the viewport. Distinct from
    // zoomFitWidth, which only constrains horizontally. Bound to ⌘0
    // following Adobe Acrobat's PDF-reader convention.
    virtual void zoomFitPage() {}
    // Natural display size of the document's primary content (page 0
    // for PDF, the image for raster docs) in logical pixels at 100%
    // zoom. Used by MainWindow to size the window to fit on first
    // open. Default empty — non-display docs (stub adapter) opt out.
    virtual QSize contentSizeHint() const { return {}; }

    virtual bool supportsViewModes() const { return false; }
    virtual ViewMode viewMode() const { return ViewMode::SinglePage; }
    virtual void setViewMode(ViewMode /*mode*/) {}

    virtual bool supportsThumbnails() const { return false; }
    virtual int pageCount() const { return 0; }
    virtual QImage renderThumbnail(int /*pageIndex*/, QSize /*targetSize*/) { return {}; }
    // Natural size of a page WITHOUT rendering it — points for PDF pages,
    // pixels for raster documents. Used by the sidebar to compute each
    // thumbnail row's aspect ratio (and thus its height) cheaply, before
    // the pixmap is rendered. Default empty: adapters that don't support
    // thumbnails opt out and the sidebar falls back to a legacy aspect.
    virtual QSizeF pageSizeHint(int /*pageIndex*/) const { return {}; }
    virtual int currentPage() const { return 0; }
    virtual void goToPage(int /*pageIndex*/) {}

    virtual bool supportsSearch() const { return false; }
    virtual void setSearchQuery(const QString & /*query*/) {}
    virtual void findNext() {}
    virtual void findPrevious() {}
    virtual void clearSearch() {}

    virtual bool supportsPrint() const { return false; }
    virtual void print(QWidget * /*dialogParent*/) {}

    // True when the document has selectable text content (a PDF text
    // layer, or an image that has had OCR applied with results stored
    // back into the document). Drives the gating of text-aware markup
    // tools (Underline, Highlight, Strikeout) on the toolbar so they
    // are not offered on bare images where they would do nothing
    // meaningful.
    virtual bool hasTextLayer() const { return false; }

    // True iff `page` carries a real, extractable native text layer
    // (born-digital PDF text, not a bare scan). Distinct from the
    // coarse hasTextLayer() capability stub — this performs a per-page
    // check so callers can tell a text page from an image-only page.
    // Drives native-text ingestion into SelectableTextStore and the
    // suppression of the "Recognize text" notice on pages that already
    // have selectable text. Default false — adapters without a native
    // text layer (images, stub) opt out.
    virtual bool pageHasText(int /*page*/) const { return false; }

    // Outline / Table of Contents access. Documents that ship with a
    // /Outlines tree (most authored PDFs do) expose it through this
    // model so the Sidebar's "Table of Contents" mode can render a
    // navigable tree. Returns nullptr when the document has none;
    // the model is owned by the document and must outlive any view
    // attached to it. `hasOutline()` is the cheap pre-check used by
    // MainWindow to gate the TOC picker entry without instantiating
    // a QTreeView.
    virtual QAbstractItemModel* outlineModel() { return nullptr; }
    virtual bool hasOutline() const { return false; }
    // Navigate to whatever the outline entry at `index` points to —
    // typically goToPage() with the destination page from the model.
    // Keeps the role-id lookup encapsulated inside the document so
    // the Sidebar doesn't have to know about QPdfBookmarkModel's
    // internal enum values.
    virtual void goToOutlineEntry(const QModelIndex& /*index*/) {}

    virtual bool supportsEditing() const { return false; }
    virtual bool isDirty() const { return false; }
    virtual bool canUndo() const { return false; }
    virtual bool canRedo() const { return false; }
    // Return true iff an operation was actually reverted / reapplied.
    // A false return with canUndo()/canRedo() still true indicates an
    // internal history desync — adapters must degrade to a warning +
    // no-op rather than mutate state they cannot account for.
    virtual bool undo() { return false; }
    virtual bool redo() { return false; }
    virtual void rotatePage(int /*pageIndex*/, int /*degreesClockwise*/) {}
    virtual void deletePages(const std::vector<int> & /*pageIndices*/) {}
    virtual void movePage(int /*from*/, int /*to*/) {}
    virtual void flipHorizontal() {}
    virtual void flipVertical() {}
    virtual bool resizeImage(int /*width*/, int /*height*/, bool /*smoothScaling*/) {
        return false;
    }
    virtual bool cropToRect(int /*x*/, int /*y*/, int /*width*/, int /*height*/) { return false; }
    virtual QSize imagePixelSize() const { return {}; }
    virtual bool adjustColour(double /*brightness*/, double /*contrast*/, double /*saturation*/) {
        return false;
    }
    // Swap the entire image pixels with the provided replacement. Used
    // by Phase 6 ML features (background removal, instant alpha, etc.)
    // to apply their output as a single undo step. The replacement
    // must have the same dimensions as the current image, or the call
    // fails with false. Implemented only by raster adapters.
    virtual bool replaceImage(const QImage & /*replacement*/) { return false; }
    // exportAs writes a copy in the requested format. `filterId` selects
    // a colour filter from filters/ImageFilter.h (empty / "none" = no
    // filter). Images use this to implement DESIGN §6.3.7 Quartz-
    // equivalent filters at export time; other adapters ignore it.
    virtual bool exportAs(const QString & /*destPath*/, const QString & /*format*/,
                          int /*quality*/ = -1, const QString & /*filterId*/ = {}) const {
        return false;
    }
    // Write a size-reduced copy of the document (linearize, compress
    // streams, generate object streams). Currently PDF-only. Returns
    // false for formats without a reducer.
    virtual bool supportsFileSizeReduction() const { return false; }
    virtual bool reduceFileSize(const QString & /*destPath*/) { return false; }
    // Write the document to destPath encrypted with `password`.
    // Currently PDF-only (Phase 5). Non-const because the
    // implementation may flush unsaved annotations into the underlying
    // PDF before writing.
    virtual bool supportsPasswordExport() const { return false; }
    virtual bool exportWithPassword(const QString & /*destPath*/, const QString & /*password*/) {
        return false;
    }

    // Number of search matches the document currently has cached for
    // its active query, plus the index of the "current" match
    // (1-based; -1 when there is no current selection). Defaults
    // are zero — non-text documents return them unconditionally.
    // The values may change asynchronously while the search is
    // running; MainWindow's polling timer reads them periodically.
    virtual int searchMatchCount() const { return 0; }
    virtual int currentSearchMatchIndex() const { return -1; }
    // Pages that contain at least one match for the current query.
    // Used by the sidebar's "search results" filter mode. Empty
    // for documents that don't support text search.
    virtual std::vector<int> pagesWithSearchMatches() const { return {}; }

    // AcroForm filling (Phase 5). supportsFormFilling() is the cheap
    // capability check; formFields() enumerates all leaf fields once;
    // setFormFieldValue(id, value) writes to the in-memory QPDF graph
    // (persisted on the next save()).
    virtual bool supportsFormFilling() const { return false; }
    virtual std::vector<FormField> formFields() const { return {}; }
    // Emitter for "a capability that resolves asynchronously is now known".
    // Returns nullptr for documents whose capabilities are all settled at
    // open. PdfDocument returns one and fires its capabilitiesChanged()
    // signal once the background load (qpdf parse + AcroForm detection)
    // completes, so MainWindow can re-run the forms-toolbar setup a moment
    // after open instead of blocking the GUI thread on the parse at open.
    virtual CapabilityNotifier *capabilityNotifier() { return nullptr; }
    virtual bool setFormFieldValue(int /*id*/, const QString & /*value*/) { return false; }
    // Show or hide the interactive form overlay. Callers toggle this
    // when the user enters / leaves form-filling mode.
    virtual void setFormFillingActive(bool /*active*/) {}
    // Re-read field values from the underlying editor and push them
    // into any visible form overlay. Use this after a bulk write (e.g.
    // AutoFill) so widgets reflect the new values without having to
    // toggle form-filling mode off and back on.
    virtual void refreshFormView() {}
    virtual bool insertPagesFrom(const QString & /*sourcePath*/, int /*insertAtIndex*/) {
        return false;
    }
    virtual bool extractPages(const std::vector<int> & /*pageIndices*/,
                              const QString & /*destPath*/) const {
        return false;
    }
    virtual bool cropPage(int /*pageIndex*/, double /*leftPts*/, double /*topPts*/,
                          double /*rightPts*/, double /*bottomPts*/) {
        return false;
    }
    virtual bool cropPages(const std::vector<int> & /*pageIndices*/, double /*leftPts*/,
                           double /*topPts*/, double /*rightPts*/, double /*bottomPts*/) {
        return false;
    }
    virtual bool save(const QString & /*newPath*/ = {}) { return false; }

    // --- External file-change tracking (ADR 2026-07-19) ------------------
    // Baseline captured at load time (and refreshed after each successful
    // save) so the save-time conflict guard and the ExternalChangeMonitor
    // both classify the on-disk state against the same reference. Adapters
    // call captureFileBaseline() once the file is loaded / written.
    const FileBaseline &fileBaseline() const { return m_fileBaseline; }
    void captureFileBaseline() { m_fileBaseline = FileBaseline::fromPath(filePath()); }
    // Classify the current on-disk state against the baseline + dirty flag.
    // Pure decision delegated to classifyExternalChange (ExternalChangeState.h).
    ExternalChangeState externalChangeState() const {
        return classifyExternalChangeFor(m_fileBaseline, filePath(), isDirty());
    }
    // True iff closing the document now would lose content the user cannot get
    // back — the predicate the close-time save prompt is gated on. This is
    // isDirty() PLUS one non-edit case (CF-7): the backing file was DELETED on
    // disk while the doc was open, which makes the in-memory buffer the only
    // remaining copy even when no edit was ever made. Without this a clean doc
    // whose file vanished would close with no prompt and drop its buffer
    // silently — the ADR-0004 no-silent-loss floor extended to a vanished file.
    // See docs/decision-records/2026-07-20-conflict-banner-keep-mine-semantics.md.
    bool hasUnsavedWork() const {
        return isDirty() || externalChangeState() == ExternalChangeState::Deleted;
    }
    // Set by the "Keep mine" force-save path so the next same-file save skips
    // the conflict guard and clobbers on purpose. Consumed by the adapter's
    // guard check.
    void setForceSaveOverExternalChange(bool f) { m_forceSaveOverExternalChange = f; }

    // Re-read the document's content from disk in place, discarding the
    // in-memory buffer (used for the clean-doc silent auto-reload and the
    // banner's Reload action). Returns true on a successful reload; false for
    // adapters that don't support it (StubAdapter) or when the file cannot be
    // re-read. Refreshes the baseline on success.
    virtual bool reloadFromDisk() { return false; }

    virtual AnnotationStore *annotations() { return nullptr; }

    // In-document OCR cache. Returns nullptr for adapters that
    // cannot host OCR results (StubAdapter, raw text views, etc.).
    // Image and PDF adapters return a per-document store; callers
    // listen on its `changed()` signal to know when fresh blocks
    // have landed for the current page. The store is owned by the
    // document and outlives any UI overlay attached to it.
    virtual SelectableTextStore *selectableText() { return nullptr; }
    // Convenience pre-check: true iff selectableText() returns a non-
    // null store. Default false so adapters that don't override are
    // skipped by the auto-OCR pump.
    virtual bool supportsSelectableText() const { return false; }
    // Render `pageIndex` to a raster suitable for OCR. PdfDocument
    // does this through Qt PDF; ImageDocument returns its source
    // image directly (page 0 only). Returns a null image when the
    // document cannot satisfy the request. The default empty image
    // signals "this adapter does not support OCR submission" so the
    // scheduler will simply not enqueue work.
    virtual QImage renderPageForOcr(int /*pageIndex*/) const { return {}; }
    // Uniform scale mapping a renderPageForOcr() source-pixel coordinate
    // into the document coordinate space that SelectableTextLayer's
    // docToView expects. PDFs render OCR sources at a fixed DPI while
    // docToView works in PDF points (72/inch), so PdfDocument returns
    // points-per-pixel (< 1); image documents render OCR in native pixel
    // space and docToView is pixel-based, so the default 1.0 is correct.
    // OcrController multiplies recognized block geometry by this before
    // storing, so forced OCR on a PDF page lands aligned with selection.
    virtual double ocrSourceToDocScale(int /*pageIndex*/) const { return 1.0; }

    virtual void setAnnotationTool(AnnotationTool /*tool*/) {}
    virtual void setAnnotationStyle(const AnnotationStyle & /*style*/) {}
    // Preset for the next Text annotation — used by FormToolbar's
    // Checkmark / X Mark tools to stamp ✓ / ✗ glyphs instead of
    // opening the multi-line input dialog. Empty clears the preset.
    virtual void setPendingAnnotationText(const QString & /*text*/) {}
    // Preset for the next Signature annotation — points at the PNG
    // file the Sign tool should stamp. Empty clears the preset.
    virtual void setPendingSignaturePath(const QString & /*path*/) {}

    virtual bool supportsAnimation() const { return false; }
    virtual int frameCount() const { return 0; }
    virtual int currentFrame() const { return 0; }
    virtual void setCurrentFrame(int /*frame*/) {}
    virtual bool isAnimationPlaying() const { return false; }
    virtual void setAnimationPlaying(bool /*playing*/) {}

  protected:
    // True iff overwriting `targetPath` right now would clobber an uncaused
    // external change. Consulted by each adapter's save() BEFORE it writes so
    // no code path can silently overwrite a newer on-disk copy (the ADR-0004
    // silent-clobber hole). Only guards a same-file overwrite of the
    // baselined original — a Save-As to a new path is never a clobber. The
    // one-shot force flag ("Keep mine") is consumed here so a deliberate
    // clobber goes through exactly once.
    bool saveWouldClobberExternalChange(const QString &targetPath) {
        // Save-As / first save to a different path: not an overwrite of the
        // file we baselined. Checked BEFORE the force flag is read so a
        // Save-As does not consume the one-shot "Keep mine" flag armed for a
        // same-file clobber (N2).
        if (targetPath != filePath())
            return false;
        const bool force = m_forceSaveOverExternalChange;
        m_forceSaveOverExternalChange = false;
        if (force)
            return false;
        const ExternalChangeState st = externalChangeState();
        return st == ExternalChangeState::CleanExternalChange ||
               st == ExternalChangeState::DirtyConflict;
    }

    // Load-time (or last-save) identity of filePath(); see captureFileBaseline.
    FileBaseline m_fileBaseline;
    // One-shot "clobber the external change on purpose" flag (Keep mine).
    bool m_forceSaveOverExternalChange = false;
};

} // namespace trailer
