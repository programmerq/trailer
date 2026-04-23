#pragma once

#include "annotation/Annotation.h"

#include <QImage>
#include <QSize>
#include <QString>
#include <QWidget>

#include <vector>

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
    virtual bool exportAs(const QString& /*destPath*/, const QString& /*format*/,
                          int /*quality*/ = -1) const { return false; }
    // Write the document to destPath encrypted with `password`.
    // Currently PDF-only (Phase 5). Non-const because the
    // implementation may flush unsaved annotations into the underlying
    // PDF before writing.
    virtual bool supportsPasswordExport() const { return false; }
    virtual bool exportWithPassword(const QString& /*destPath*/,
                                    const QString& /*password*/) { return false; }
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

    virtual bool supportsAnimation() const { return false; }
    virtual int frameCount() const { return 0; }
    virtual int currentFrame() const { return 0; }
    virtual void setCurrentFrame(int /*frame*/) {}
    virtual bool isAnimationPlaying() const { return false; }
    virtual void setAnimationPlaying(bool /*playing*/) {}
};

}  // namespace trailer
