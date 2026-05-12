#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

class QWidget;

namespace trailer {

// Build a multi-resolution QIcon from a monochrome SVG resource,
// tinted to the widget's palette text colour. The SVG must render as
// black-on-transparent (any fill / stroke colour is fine — the
// SourceIn composite recolours everything to `color`).
//
// Pass the widget that will host the icon so the tint follows the
// active palette (light vs. dark Qt theme). If `widget` is null, the
// application palette is used.
//
// Built pixmaps cover 16 / 18 / 24 / 32 / 36 / 48 / 64 px so Qt can
// pick an appropriately sharp source for both 1x and 2x displays.
//
// If a sibling resource exists with the same name but a `-filled.svg`
// suffix (e.g. `tool-rectangle.svg` → `tool-rectangle-filled.svg`),
// it is automatically attached as the icon's `QIcon::On` pixmap. Qt
// uses that pixmap whenever the hosting QAction is checked / armed,
// which gives the markup-toolbar tools their "this is the active
// tool" outline-to-fill swap without callers having to know about
// state pairs.
QIcon themedActionIcon(const QString& resource, const QColor& color);
QIcon themedActionIcon(const QString& resource, const QWidget* widget = nullptr);

}  // namespace trailer
