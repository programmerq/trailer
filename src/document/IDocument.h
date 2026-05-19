#pragma once

#include "annotation/Annotation.h"
#include "document/PdfEditor.h"

#include <QImage>
#include <QSize>
#include <QString>
#include <QWidget>

#include <vector>

class QAbstractItemModel;
class QModelIndex;

namespace trailer {

class AnnotationStore;

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
    virtual void undo() {}
    virtual void redo() {}
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

    virtual AnnotationStore *annotations() { return nullptr; }
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
};

} // namespace trailer
