#include "ImageAdapter.h"

#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QMovie>
#include <QPixmap>
#include <QScrollArea>
#include <QVBoxLayout>

namespace trailer {

namespace {

constexpr const char* kExtensions[] = {
    "png", "jpg", "jpeg", "bmp", "gif", "tiff", "tif", "webp",
    "ppm", "pgm", "pbm", "xbm", "xpm", "ico",
};

}  // namespace

ImageDocument::ImageDocument(QString path) : m_path(std::move(path)) {
    QImageReader reader(m_path);
    m_animated = reader.supportsAnimation() && reader.imageCount() > 1;
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
        label->setMovie(movie);
        if (movie->isValid()) {
            movie->start();
        }
    } else {
        QImageReader reader(m_path);
        reader.setAutoTransform(true);
        const QImage image = reader.read();
        if (image.isNull()) {
            label->setText(QObject::tr("Could not decode image:\n%1").arg(m_path));
        } else {
            label->setPixmap(QPixmap::fromImage(image));
            label->adjustSize();
        }
    }

    scroll->setWidget(label);
    return scroll;
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
