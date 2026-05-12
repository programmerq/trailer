#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

class QWidget;

namespace trailer {

// Build a multi-resolution QIcon from a monochrome SVG resource,
// tinted to the widget's palette text colour. The SVG must render as
// black-on-transparent (any fill / stroke colour is fine — the
// SourceIn composite below recolours everything to `color`).
//
// Pass the widget that will host the icon so the tint follows the
// active palette (light vs. dark Qt theme). If `widget` is null, the
// application palette is used.
//
// Built pixmaps cover 16 / 18 / 24 / 32 / 36 / 48 px so Qt can pick
// an appropriately sharp source for both 1x and 2x displays.
QIcon themedActionIcon(const QString& resource, const QColor& color);
QIcon themedActionIcon(const QString& resource, const QWidget* widget = nullptr);

}  // namespace trailer
