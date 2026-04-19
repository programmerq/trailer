#pragma once

#include "IDocument.h"
#include "IFormatAdapter.h"

#include <QImage>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <memory>

class QLabel;
class QScrollArea;

namespace trailer {

class ImageDocument : public IDocument {
public:
    explicit ImageDocument(QString path);

    QString displayName() const override;
    QString filePath() const override;
    QWidget* createView(QWidget* parent) override;

    bool supportsZoom() const override { return !m_animated && !m_image.isNull(); }
    void zoomIn() override;
    void zoomOut() override;
    void zoomActual() override;
    void zoomFitWidth() override;

    bool supportsPrint() const override { return !m_image.isNull(); }
    void print(QWidget* dialogParent) override;

private:
    void applyScale(double factor);

    QString m_path;
    QImage m_image;
    QPointer<QScrollArea> m_scroll;
    QPointer<QLabel> m_label;
    double m_scale = 1.0;
    bool m_animated = false;
};

class ImageAdapter : public IFormatAdapter {
public:
    QStringList mimeTypes() const override;
    QStringList extensions() const override;
    std::unique_ptr<IDocument> open(const QString& path) override;
};

}  // namespace trailer
