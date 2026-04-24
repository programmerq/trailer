#include "ImageFilter.h"

#include <algorithm>

namespace trailer {

namespace {

// Rec.709 luma weights. Good enough for every built-in that collapses
// the RGB channels to luminance (Greyscale, Sepia, tinted tones).
int luma709(int r, int g, int b) {
    return static_cast<int>(0.2126 * r + 0.7152 * g + 0.0722 * b);
}

int clamp8(int v) { return std::clamp(v, 0, 255); }

// Shared helper: walk every pixel, call `fn(r, g, b, a)` to produce
// the replacement. Format is forced to ARGB32 so plane math is
// predictable regardless of the source.
template <typename Fn>
QImage transformPixels(const QImage& src, Fn fn) {
    if (src.isNull()) return src;
    QImage img = src.convertToFormat(QImage::Format_ARGB32);
    const int w = img.width();
    const int h = img.height();
    for (int y = 0; y < h; ++y) {
        auto* scan = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb px = scan[x];
            const int r = qRed(px);
            const int g = qGreen(px);
            const int b = qBlue(px);
            const int a = qAlpha(px);
            scan[x] = fn(r, g, b, a);
        }
    }
    return img;
}

QImage blackAndWhite(const QImage& src) {
    // Mid-grey threshold. Fine for the "high-contrast photocopy" look
    // Preview users associate with this filter — anything more
    // sophisticated (Otsu, adaptive) is overkill at the export stage.
    return transformPixels(src, [](int r, int g, int b, int a) {
        const int l = luma709(r, g, b);
        const int v = l >= 128 ? 255 : 0;
        return qRgba(v, v, v, a);
    });
}

QImage greyscale(const QImage& src) {
    return transformPixels(src, [](int r, int g, int b, int a) {
        const int l = luma709(r, g, b);
        return qRgba(l, l, l, a);
    });
}

QImage sepia(const QImage& src) {
    // Classic sepia matrix from every "Intro to Image Processing"
    // slide deck. Values clamped per channel.
    return transformPixels(src, [](int r, int g, int b, int a) {
        const int nr = clamp8(static_cast<int>(0.393 * r + 0.769 * g + 0.189 * b));
        const int ng = clamp8(static_cast<int>(0.349 * r + 0.686 * g + 0.168 * b));
        const int nb = clamp8(static_cast<int>(0.272 * r + 0.534 * g + 0.131 * b));
        return qRgba(nr, ng, nb, a);
    });
}

QImage lighten(const QImage& src) {
    // Add a flat 25% of white. Matches Preview's "Lighten" which is a
    // mild exposure bump rather than a true gamma curve.
    constexpr int kBoost = 64;
    return transformPixels(src, [](int r, int g, int b, int a) {
        return qRgba(clamp8(r + kBoost),
                     clamp8(g + kBoost),
                     clamp8(b + kBoost),
                     a);
    });
}

QImage blueTone(const QImage& src) {
    // Greyscale, then push the blue channel above the others. Hits
    // the cyanotype / blueprint look without overthinking it.
    return transformPixels(src, [](int r, int g, int b, int a) {
        const int l = luma709(r, g, b);
        const int nr = clamp8(static_cast<int>(l * 0.60));
        const int ng = clamp8(static_cast<int>(l * 0.75));
        const int nb = clamp8(static_cast<int>(l * 1.00 + 30));
        return qRgba(nr, ng, nb, a);
    });
}

QImage greyTone(const QImage& src) {
    // Softer variant of Greyscale — a small warm-grey bias so
    // scanned newspaper prints don't feel clinical.
    return transformPixels(src, [](int r, int g, int b, int a) {
        const int l = luma709(r, g, b);
        const int nr = clamp8(static_cast<int>(l * 0.98));
        const int ng = clamp8(static_cast<int>(l * 0.96));
        const int nb = clamp8(static_cast<int>(l * 0.92));
        return qRgba(nr, ng, nb, a);
    });
}

}  // namespace

QString filterId(ImageFilter f) {
    switch (f) {
        case ImageFilter::None:          return QStringLiteral("none");
        case ImageFilter::BlackAndWhite: return QStringLiteral("black_and_white");
        case ImageFilter::Greyscale:     return QStringLiteral("greyscale");
        case ImageFilter::Sepia:         return QStringLiteral("sepia");
        case ImageFilter::Lighten:       return QStringLiteral("lighten");
        case ImageFilter::BlueTone:      return QStringLiteral("blue_tone");
        case ImageFilter::GreyTone:      return QStringLiteral("grey_tone");
    }
    return QStringLiteral("none");
}

ImageFilter filterFromId(const QString& id) {
    if (id == QLatin1String("black_and_white")) return ImageFilter::BlackAndWhite;
    if (id == QLatin1String("greyscale"))       return ImageFilter::Greyscale;
    if (id == QLatin1String("sepia"))           return ImageFilter::Sepia;
    if (id == QLatin1String("lighten"))         return ImageFilter::Lighten;
    if (id == QLatin1String("blue_tone"))       return ImageFilter::BlueTone;
    if (id == QLatin1String("grey_tone"))       return ImageFilter::GreyTone;
    return ImageFilter::None;
}

QString filterDisplayName(ImageFilter f) {
    // Not wrapped in tr() here because this module must stay free of
    // Qt Widgets. Callers in the UI layer tr-wrap as needed.
    switch (f) {
        case ImageFilter::None:          return QStringLiteral("None");
        case ImageFilter::BlackAndWhite: return QStringLiteral("Black && White");
        case ImageFilter::Greyscale:     return QStringLiteral("Greyscale");
        case ImageFilter::Sepia:         return QStringLiteral("Sepia");
        case ImageFilter::Lighten:       return QStringLiteral("Lighten");
        case ImageFilter::BlueTone:      return QStringLiteral("Blue Tone");
        case ImageFilter::GreyTone:      return QStringLiteral("Grey Tone");
    }
    return QStringLiteral("None");
}

QList<ImageFilter> allFilters() {
    return {
        ImageFilter::None,
        ImageFilter::BlackAndWhite,
        ImageFilter::Greyscale,
        ImageFilter::Sepia,
        ImageFilter::Lighten,
        ImageFilter::BlueTone,
        ImageFilter::GreyTone,
    };
}

QImage applyFilter(ImageFilter f, const QImage& src) {
    if (src.isNull()) return src;
    switch (f) {
        case ImageFilter::None:          return src;
        case ImageFilter::BlackAndWhite: return blackAndWhite(src);
        case ImageFilter::Greyscale:     return greyscale(src);
        case ImageFilter::Sepia:         return sepia(src);
        case ImageFilter::Lighten:       return lighten(src);
        case ImageFilter::BlueTone:      return blueTone(src);
        case ImageFilter::GreyTone:      return greyTone(src);
    }
    return src;
}

QImage applyFilter(const QString& id, const QImage& src) {
    return applyFilter(filterFromId(id), src);
}

}  // namespace trailer
