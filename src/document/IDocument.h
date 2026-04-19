#pragma once

#include <QImage>
#include <QSize>
#include <QString>
#include <QWidget>

namespace trailer {

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

    virtual bool supportsAnimation() const { return false; }
    virtual int frameCount() const { return 0; }
    virtual int currentFrame() const { return 0; }
    virtual void setCurrentFrame(int /*frame*/) {}
    virtual bool isAnimationPlaying() const { return false; }
    virtual void setAnimationPlaying(bool /*playing*/) {}
};

}  // namespace trailer
