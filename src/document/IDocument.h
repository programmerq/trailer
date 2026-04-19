#pragma once

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
};

}  // namespace trailer
