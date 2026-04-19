#include "ImageAdapter.h"

#include <QFileInfo>
#include <QImageReader>
#include <QLabel>
#include <QMovie>
#include <QPageLayout>
#include <QPainter>
#include <QPixmap>
#include <QPrintDialog>
#include <QPrinter>
#include <QRect>
#include <QScrollArea>

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
