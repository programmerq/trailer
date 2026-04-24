#pragma once

#include "annotation/Annotation.h"

#include <QImage>
#include <QSize>
#include <QString>
#include <QWidget>

#include <vector>

// Forward-declared so IDocument.h doesn't pull in PdfEditor.h.
// Callers that need the full type include PdfEditor.h themselves.
namespace trailer { struct FormField; }

namespace trailer {

class AnnotationStore;

enum class ViewMode {
    SinglePage,
    TwoPages,
    Continuous,
};

class IDocument {
public:
    virtual ~IDocument() = default;

    virtual QString displayName() const = 0;
    virtual QString filePath() const = 0;
    virtual QWidget* createView(QWidget* parent) = 0;

    virtual bool supportsZoom() const { return false; }
    virtual void zoomIn() {}
    virtual void zoomOut() {}
    virtual void zoomActual() {}
    virtual void zoomFitWidth() {}

    virtual bool supportsViewModes() const { return false; }
    virtual ViewMode viewMode() const { return ViewMode::SinglePage; }
    virtual void setViewMode(ViewMode /*mode*/) {}

    virtual bool supportsThumbnails() const { return false; }
    virtual int pageCount() const { return 0; }
    virtual QImage renderThumbnail(int /*pageIndex*/, QSize /*targetSize*/) { return {}; }
    virtual int currentPage() const { return 0; }
    virtual void goToPage(int /*pageIndex*/) {}

    virtual bool supportsSearch() const { return false; }
    virtual void setSearchQuery(const QString& /*query*/) {}
    virtual void findNext() {}
    virtual void findPrevious() {}
    virtual void clearSearch() {}

    virtual bool supportsPrint() const { return false; }
    virtual void print(QWidget* /*dialogParent*/) {}

    virtual bool supportsEditing() const { return false; }
    virtual bool isDirty() const { return false; }
    virtual bool canUndo() const { return false; }
    virtual bool canRedo() const { return false; }
    virtual void undo() {}
    virtual void redo() {}
    virtual void rotatePage(int /*pageIndex*/, int /*degreesClockwise*/) {}
    virtual void deletePages(const std::vector<int>& /*pageIndices*/) {}
    virtual void movePage(int /*from*/, int /*to*/) {}
    virtual void flipHorizontal() {}
    virtual void flipVertical() {}
    virtual bool resizeImage(int /*width*/, int /*height*/, bool /*smoothScaling*/) { return false; }
    virtual bool cropToRect(int /*x*/, int /*y*/, int /*width*/, int /*height*/) { return false; }
    virtual QSize imagePixelSize() const { return {}; }
    virtual bool adjustColour(double /*brightness*/, double /*contrast*/,
                              double /*saturation*/) { return false; }
    // Swap the entire image pixels with the provided replacement. Used
    // by Phase 6 ML features (background removal, instant alpha, etc.)
    // to apply their output as a single undo step. The replacement
    // must have the same dimensions as the current image, or the call
    // fails with false. Implemented only by raster adapters.
    virtual bool replaceImage(const QImage& /*replacement*/) { return false; }
    // exportAs writes a copy in the requested format. `filterId` selects
    // a colour filter from filters/ImageFilter.h (empty / "none" = no
    // filter). Images use this to implement DESIGN §6.3.7 Quartz-
    // equivalent filters at export time; other adapters ignore it.
    virtual bool exportAs(const QString& /*destPath*/, const QString& /*format*/,
                          int /*quality*/ = -1,
                          const QString& /*filterId*/ = {}) const { return false; }
    // Write a size-reduced copy of the document (linearize, compress
    // streams, generate object streams). Currently PDF-only. Returns
    // false for formats without a reducer.
    virtual bool supportsFileSizeReduction() const { return false; }
    virtual bool reduceFileSize(const QString& /*destPath*/) { return false; }
    // Write the document to destPath encrypted with `password`.
    // Currently PDF-only (Phase 5). Non-const because the
    // implementation may flush unsaved annotations into the underlying
    // PDF before writing.
    virtual bool supportsPasswordExport() const { return false; }
    virtual bool exportWithPassword(const QString& /*destPath*/,
                                    const QString& /*password*/) { return false; }

    // AcroForm filling (Phase 5). supportsFormFilling() is the cheap
    // capability check; formFields() enumerates all leaf fields once;
    // setFormFieldValue(id, value) writes to the in-memory QPDF graph
    // (persisted on the next save()).
    virtual bool supportsFormFilling() const { return false; }
    virtual std::vector<FormField> formFields() const { return {}; }
    virtual bool setFormFieldValue(int /*id*/, const QString& /*value*/) {
        return false;
    }
    // Show or hide the interactive form overlay. Callers toggle this
    // when the user enters / leaves form-filling mode.
    virtual void setFormFillingActive(bool /*active*/) {}
    virtual bool insertPagesFrom(const QString& /*sourcePath*/, int /*insertAtIndex*/) { return false; }
    virtual bool extractPages(const std::vector<int>& /*pageIndices*/, const QString& /*destPath*/) const { return false; }
    virtual bool cropPage(int /*pageIndex*/, double /*leftPts*/, double /*topPts*/,
                          double /*rightPts*/, double /*bottomPts*/) { return false; }
    virtual bool cropPages(const std::vector<int>& /*pageIndices*/,
                           double /*leftPts*/, double /*topPts*/,
                           double /*rightPts*/, double /*bottomPts*/) { return false; }
    virtual bool save(const QString& /*newPath*/ = {}) { return false; }

    virtual AnnotationStore* annotations() { return nullptr; }
    virtual void setAnnotationTool(AnnotationTool /*tool*/) {}
    virtual void setAnnotationStyle(const AnnotationStyle& /*style*/) {}
    // Preset for the next Text annotation — used by FormToolbar's
    // Checkmark / X Mark tools to stamp ✓ / ✗ glyphs instead of
    // opening the multi-line input dialog. Empty clears the preset.
    virtual void setPendingAnnotationText(const QString& /*text*/) {}
    // Preset for the next Signature annotation — points at the PNG
    // file the Sign tool should stamp. Empty clears the preset.
    virtual void setPendingSignaturePath(const QString& /*path*/) {}

    virtual bool supportsAnimation() const { return false; }
    virtual int frameCount() const { return 0; }
    virtual int currentFrame() const { return 0; }
    virtual void setCurrentFrame(int /*frame*/) {}
    virtual bool isAnimationPlaying() const { return false; }
    virtual void setAnimationPlaying(bool /*playing*/) {}
};

}  // namespace trailer
