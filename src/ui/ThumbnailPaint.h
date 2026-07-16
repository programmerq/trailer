#pragma once

// Device-pixel-ratio-correct scaling for sidebar page thumbnails.
//
// ThumbnailModel renders each thumbnail at native resolution for the
// primary screen and stamps devicePixelRatio() on the pixmap (see
// ThumbnailModel::data, DecorationRole): a dpr=2 pixmap has raw width
// 2*logical. The delegate must scale that pixmap to a LOGICAL column
// width. QPixmap::scaledToWidth() operates on raw device pixels and
// preserves the source devicePixelRatio, so scaledToWidth(logicalWidth)
// on a dpr=2 pixmap yields a pixmap whose logical width is
// logicalWidth/2 -- the thumbnail paints at half the column width on
// Retina while the row is sized for the full width (the empty-slack bug
// this header fixes). Scaling to logicalWidth*dpr raw pixels and
// re-stamping dpr keeps the drawn logical width == logicalWidth for any
// devicePixelRatio, and is a no-op at dpr=1.

#include <QPixmap>
#include <QSize>

#include <cmath>

namespace trailer {

// The pixmap's devicePixelRatio, clamped to a sane positive value.
// The clamp only guards dpr<=0 (a null / unstamped pixmap); dpr<1
// (downscaling below logical) is out of contract for this HiDPI helper.
inline qreal thumbnailEffectiveDpr(const QPixmap &pm) {
    const qreal dpr = pm.devicePixelRatio();
    return dpr > 0.0 ? dpr : 1.0;
}

// Scale `src` so that, when drawn, it occupies exactly `logicalWidth`
// logical pixels wide (aspect preserved, source dpr retained for
// crispness on HiDPI). Returns a null pixmap for a null source or a
// non-positive width.
inline QPixmap scaleToLogicalWidth(const QPixmap &src, int logicalWidth) {
    if (src.isNull() || logicalWidth <= 0) {
        return {};
    }
    const qreal dpr = thumbnailEffectiveDpr(src);
    QPixmap scaled = src.scaledToWidth(int(std::lround(logicalWidth * dpr)),
                                       Qt::SmoothTransformation);
    // Belt-and-suspenders re-stamp: scaledToWidth already preserves the
    // source dpr, but re-setting it keeps the drawn logical width exact
    // and independent of any Qt-internal rounding.
    scaled.setDevicePixelRatio(dpr);
    return scaled;
}

// Logical size (paint coordinates) a pixmap occupies when drawn -- its
// raw pixel size divided by its devicePixelRatio. Deliberately returns
// an integer QSize (consumed by imageRect) rather than Qt's floating
// QPixmap::deviceIndependentSize().
inline QSize logicalSize(const QPixmap &pm) {
    const qreal dpr = thumbnailEffectiveDpr(pm);
    return QSize(int(std::lround(pm.width() / dpr)),
                 int(std::lround(pm.height() / dpr)));
}

} // namespace trailer
