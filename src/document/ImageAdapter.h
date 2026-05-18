#pragma once

#include "IDocument.h"
#include "IFormatAdapter.h"
#include "annotation/AnnotationStore.h"

#include <QImage>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <memory>
#include <vector>

class QLabel;
class QMovie;
class QScrollArea;

namespace trailer {

class AnnotationOverlay;

class ImageDocument : public IDocument {
  public:
    explicit ImageDocument(QString path);

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

    ZoomMode zoomMode() const override { return m_zoomMode; }
    double zoomFactor() const override { return m_scale; }
    void applyZoomState(ZoomMode mode, double factor) override;
    int scrollY() const override;
    void applyScrollY(int y) override;

    bool supportsPrint() const override { return !m_image.isNull(); }
    void print(QWidget *dialogParent) override;

    bool supportsThumbnails() const override { return !m_image.isNull() && !m_animated; }
    QImage renderThumbnail(int pageIndex, QSize targetSize) override;

    AnnotationStore *annotations() override { return &m_annotations; }
    void setAnnotationTool(AnnotationTool tool) override;
    void setAnnotationStyle(const AnnotationStyle &style) override;
    void setPendingAnnotationText(const QString &text) override;
    void setPendingSignaturePath(const QString &path) override;

    bool supportsEditing() const override { return !m_image.isNull() && !m_animated; }
    bool isDirty() const override { return m_dirty || !m_annotations.annotations().empty(); }
    bool canUndo() const override { return !m_undoStack.empty() || m_annotations.canUndo(); }
    bool canRedo() const override { return !m_redoStack.empty() || m_annotations.canRedo(); }
    void undo() override;
    void redo() override;
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

  private:
    void applyScale(double factor);
    void refreshView();
    void pushUndoSnapshot();

    QString m_path;
    QImage m_image;
    QPointer<QScrollArea> m_scroll;
    QPointer<QLabel> m_label;
    QPointer<QMovie> m_movie;
    QPointer<AnnotationOverlay> m_overlay;
    AnnotationStore m_annotations;
    std::vector<QImage> m_undoStack;
    std::vector<QImage> m_redoStack;
    double m_scale = 1.0;
    // Tracks the user's intent (Fit-page, Fit-width, Actual, custom
    // factor) so the persistence layer can round-trip the mode rather
    // than rebuilding it from the scale alone. Image adapter has no
    // explicit Qt mode — the scale is the source of truth at render
    // time — but the saved state needs the original intent so a
    // window resize after restore re-fits correctly.
    ZoomMode m_zoomMode = ZoomMode::Actual;
    int m_frameCount = 0;
    bool m_animated = false;
    bool m_dirty = false;
};

class ImageAdapter : public IFormatAdapter {
  public:
    QStringList mimeTypes() const override;
    QStringList extensions() const override;
    std::unique_ptr<IDocument> open(const QString &path) override;
};

} // namespace trailer
