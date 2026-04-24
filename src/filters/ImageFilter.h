#pragma once

#include <QImage>
#include <QString>
#include <QStringList>

// Quartz-equivalent filters (DESIGN §6.3.7). A filter is a pure
// function `QImage -> QImage` selected by a stable id. The UI shows
// the display name; exporters save and load the id so a file's
// filter choice round-trips without being pinned to a translation.
//
// The built-ins are pure colour transforms with no external state —
// no LUT files, no ICC conversions — so they run cheaply on top of
// whatever the exporter already produced. Custom LUT-backed filters
// are left for a follow-up (noted in DESIGN §6.3.7 "Custom…" entry).

namespace trailer {

enum class ImageFilter {
    None,
    BlackAndWhite,
    Greyscale,
    Sepia,
    Lighten,
    BlueTone,
    GreyTone,
};

// Stable, lowercase ids for persistence. Never translated.
QString filterId(ImageFilter f);
ImageFilter filterFromId(const QString& id);

// Localisable display name for menu rows / combo entries.
QString filterDisplayName(ImageFilter f);

// Enumerate every built-in, in the order they should appear in the UI.
QList<ImageFilter> allFilters();

// Apply the filter in-place-style: returns a new image. Pass
// ImageFilter::None (or an empty/unknown id) to get the input back
// untouched — exporters call this unconditionally so the "no filter"
// path is the identity.
QImage applyFilter(ImageFilter f, const QImage& src);
QImage applyFilter(const QString& id, const QImage& src);

}  // namespace trailer
