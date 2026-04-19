#include "ImageAdapter.h"

#include <QColor>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QLabel>
#include <QMovie>
#include <QPageLayout>
#include <QPainter>
#include <QPixmap>
#include <QPrintDialog>
#include <QPrinter>
#include <QRect>
#include <QScrollArea>
#include <QTransform>

#include <cmath>

#include <algorithm>

namespace trailer {

namespace {

constexpr const char* kExtensions[] = {
    "png", "jpg", "jpeg", "bmp", "gif", "tiff", "tif", "webp",
    "ppm", "pgm", "pbm", "xbm", "xpm", "ico",
};

constexpr double kZoomStep = 1.1;
constexpr double kZoomMin = 0.05;
constexpr double kZoomMax = 32.0;

}  // namespace

ImageDocument::ImageDocument(QString path) : m_path(std::move(path)) {
    QImageReader reader(m_path);
    reader.setAutoTransform(true);
    m_animated = reader.supportsAnimation() && reader.imageCount() > 1;
    if (m_animated) {
        m_frameCount = reader.imageCount();
        m_image = reader.read();
    } else {
        m_image = reader.read();
    }
}

QString ImageDocument::displayName() const {
    return QFileInfo(m_path).fileName();
}

QString ImageDocument::filePath() const {
    return m_path;
}

QWidget* ImageDocument::createView(QWidget* parent) {
    auto* scroll = new QScrollArea(parent);
    scroll->setAlignment(Qt::AlignCenter);
    scroll->setBackgroundRole(QPalette::Base);

    auto* label = new QLabel(scroll);
    label->setAlignment(Qt::AlignCenter);
    label->setBackgroundRole(QPalette::Base);

    if (m_animated) {
        auto* movie = new QMovie(m_path, QByteArray(), label);
        m_movie = movie;
        label->setMovie(movie);
        if (movie->isValid()) {
            movie->start();
        }
    } else if (!m_image.isNull()) {
        label->setPixmap(QPixmap::fromImage(m_image));
        label->adjustSize();
    } else {
        label->setText(QObject::tr("Could not decode image:\n%1").arg(m_path));
    }

    scroll->setWidget(label);
    m_scroll = scroll;
    m_label = label;
    return scroll;
}

void ImageDocument::applyScale(double factor) {
    if (!m_label || m_image.isNull()) {
        return;
    }
    m_scale = std::clamp(factor, kZoomMin, kZoomMax);
    const QSize target = m_image.size() * m_scale;
    const QPixmap scaled = QPixmap::fromImage(m_image).scaled(
        target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_label->setPixmap(scaled);
    m_label->adjustSize();
}

void ImageDocument::zoomIn() {
    applyScale(m_scale * kZoomStep);
}

void ImageDocument::zoomOut() {
    applyScale(m_scale / kZoomStep);
}

void ImageDocument::zoomActual() {
    applyScale(1.0);
}

void ImageDocument::zoomFitWidth() {
    if (!m_scroll || !m_label || m_image.isNull()) {
        return;
    }
    const int available = m_scroll->viewport()->width();
    if (available <= 0 || m_image.width() <= 0) {
        return;
    }
    applyScale(static_cast<double>(available) / static_cast<double>(m_image.width()));
}

void ImageDocument::refreshView() {
    if (!m_label || m_image.isNull()) return;
    applyScale(m_scale);
}

void ImageDocument::rotatePage(int /*pageIndex*/, int degreesClockwise) {
    if (m_image.isNull() || m_animated) return;
    QTransform t;
    t.rotate(degreesClockwise);
    m_image = m_image.transformed(t, Qt::SmoothTransformation);
    m_dirty = true;
    refreshView();
}

void ImageDocument::flipHorizontal() {
    if (m_image.isNull() || m_animated) return;
    m_image = m_image.mirrored(/*horizontally=*/true, /*vertically=*/false);
    m_dirty = true;
    refreshView();
}

void ImageDocument::flipVertical() {
    if (m_image.isNull() || m_animated) return;
    m_image = m_image.mirrored(/*horizontally=*/false, /*vertically=*/true);
    m_dirty = true;
    refreshView();
}

bool ImageDocument::resizeImage(int width, int height, bool smoothScaling) {
    if (m_image.isNull() || m_animated || width <= 0 || height <= 0) {
        return false;
    }
    const Qt::TransformationMode mode =
        smoothScaling ? Qt::SmoothTransformation : Qt::FastTransformation;
    m_image = m_image.scaled(width, height, Qt::IgnoreAspectRatio, mode);
    m_dirty = true;
    refreshView();
    return true;
}

bool ImageDocument::cropToRect(int x, int y, int width, int height) {
    if (m_image.isNull() || m_animated) return false;
    const QRect bounds(0, 0, m_image.width(), m_image.height());
    const QRect rect = QRect(x, y, width, height).intersected(bounds);
    if (rect.isEmpty()) return false;
    m_image = m_image.copy(rect);
    m_dirty = true;
    refreshView();
    return true;
}

namespace {

QImage applyColourTransform(const QImage& src, double brightness,
                            double contrast, double saturation) {
    const double bAdd = std::clamp(brightness, -1.0, 1.0) * 255.0;
    const double cFactor = (1.0 + std::clamp(contrast, -1.0, 1.0));
    const double sFactor = (1.0 + std::clamp(saturation, -1.0, 1.0));
    const double kLumR = 0.299;
    const double kLumG = 0.587;
    const double kLumB = 0.114;

    QImage work = src.convertToFormat(QImage::Format_ARGB32);
    const int h = work.height();
    const int w = work.width();
    for (int y = 0; y < h; ++y) {
        auto* scanline = reinterpret_cast<QRgb*>(work.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb px = scanline[x];
            double r = qRed(px);
            double g = qGreen(px);
            double b = qBlue(px);
            const int a = qAlpha(px);

            r += bAdd; g += bAdd; b += bAdd;
            r = (r - 128.0) * cFactor + 128.0;
            g = (g - 128.0) * cFactor + 128.0;
            b = (b - 128.0) * cFactor + 128.0;
            const double lum = r * kLumR + g * kLumG + b * kLumB;
            r = lum + (r - lum) * sFactor;
            g = lum + (g - lum) * sFactor;
            b = lum + (b - lum) * sFactor;

            scanline[x] = qRgba(
                std::clamp(static_cast<int>(std::lround(r)), 0, 255),
                std::clamp(static_cast<int>(std::lround(g)), 0, 255),
                std::clamp(static_cast<int>(std::lround(b)), 0, 255),
                a);
        }
    }
    return work;
}

}  // namespace

bool ImageDocument::adjustColour(double brightness, double contrast,
                                 double saturation) {
    if (m_image.isNull() || m_animated) return false;
    m_image = applyColourTransform(m_image, brightness, contrast, saturation);
    m_dirty = true;
    refreshView();
    return true;
}

void ImageDocument::previewColour(double brightness, double contrast,
                                  double saturation) {
    if (!m_label || m_image.isNull() || m_animated) return;
    const QImage preview = applyColourTransform(
        m_image, brightness, contrast, saturation);
    const QSize target = preview.size() * m_scale;
    m_label->setPixmap(QPixmap::fromImage(preview).scaled(
        target, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void ImageDocument::clearColourPreview() {
    refreshView();
}

bool ImageDocument::exportAs(const QString& destPath, const QString& format,
                             int quality) const {
    if (m_image.isNull()) return false;
    QImageWriter writer(destPath, format.toLatin1());
    if (quality >= 0) writer.setQuality(quality);
    return writer.write(m_image);
}

bool ImageDocument::save(const QString& newPath) {
    if (m_image.isNull() || m_animated) return false;
    const QString target = newPath.isEmpty() ? m_path : newPath;
    if (target.isEmpty()) return false;
    const QByteArray format = QFileInfo(target).suffix().toLatin1().toLower();
    QImageWriter writer(target, format.isEmpty() ? QByteArray("png") : format);
    if (!writer.write(m_image)) return false;
    m_path = target;
    m_dirty = false;
    return true;
}

int ImageDocument::currentFrame() const {
    return m_movie ? m_movie->currentFrameNumber() : 0;
}

void ImageDocument::setCurrentFrame(int frame) {
    if (!m_movie || frame < 0 || (m_frameCount > 0 && frame >= m_frameCount)) {
        return;
    }
    if (m_movie->state() == QMovie::Running) {
        m_movie->setPaused(true);
    }
    m_movie->jumpToFrame(frame);
}

bool ImageDocument::isAnimationPlaying() const {
    return m_movie && m_movie->state() == QMovie::Running;
}

void ImageDocument::setAnimationPlaying(bool playing) {
    if (!m_movie) {
        return;
    }
    if (playing) {
        if (m_movie->state() == QMovie::Paused) {
            m_movie->setPaused(false);
        } else if (m_movie->state() == QMovie::NotRunning) {
            m_movie->start();
        }
    } else {
        if (m_movie->state() == QMovie::Running) {
            m_movie->setPaused(true);
        }
    }
}

QImage ImageDocument::renderThumbnail(int pageIndex, QSize targetSize) {
    if (pageIndex != 0 || m_image.isNull() || !targetSize.isValid()) {
        return {};
    }
    return m_image.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

void ImageDocument::print(QWidget* dialogParent) {
    if (m_image.isNull()) {
        return;
    }
    QPrinter printer(QPrinter::HighResolution);
    printer.setDocName(displayName());
    QPrintDialog dialog(&printer, dialogParent);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    QPainter painter;
    if (!painter.begin(&printer)) {
        return;
    }
    const QRect target = printer.pageLayout().paintRectPixels(printer.resolution());
    const QImage scaled = m_image.scaled(
        target.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const int x = target.x() + (target.width() - scaled.width()) / 2;
    const int y = target.y() + (target.height() - scaled.height()) / 2;
    painter.drawImage(QPoint(x, y), scaled);
    painter.end();
}

QStringList ImageAdapter::mimeTypes() const {
    return {
        QStringLiteral("image/png"),
        QStringLiteral("image/jpeg"),
        QStringLiteral("image/bmp"),
        QStringLiteral("image/gif"),
        QStringLiteral("image/tiff"),
        QStringLiteral("image/webp"),
        QStringLiteral("image/x-portable-pixmap"),
        QStringLiteral("image/x-portable-graymap"),
        QStringLiteral("image/x-portable-bitmap"),
        QStringLiteral("image/x-xbitmap"),
        QStringLiteral("image/x-xpixmap"),
        QStringLiteral("image/vnd.microsoft.icon"),
    };
}

QStringList ImageAdapter::extensions() const {
    QStringList out;
    out.reserve(static_cast<int>(std::size(kExtensions)));
    for (const char* ext : kExtensions) {
        out.append(QString::fromLatin1(ext));
    }
    return out;
}

std::unique_ptr<IDocument> ImageAdapter::open(const QString& path) {
    return std::make_unique<ImageDocument>(path);
}

}  // namespace trailer
