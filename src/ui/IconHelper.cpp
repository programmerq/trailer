#include "IconHelper.h"

#include <QApplication>
#include <QFile>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QSvgRenderer>
#include <QWidget>

namespace trailer {

namespace {

// Render one SVG resource into the QIcon for the given mode/state at
// every size we plausibly need. Pixmaps are tinted to `color` via
// CompositionMode_SourceIn so the original SVG paint colour doesn't
// matter — only its alpha channel does.
void addRenderedSvg(QIcon& icon, const QString& resource,
                    const QColor& color,
                    QIcon::Mode mode, QIcon::State state) {
    QSvgRenderer renderer(resource);
    if (!renderer.isValid()) {
        return;
    }
    for (int size : {16, 18, 24, 32, 36, 48, 64}) {
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        renderer.render(&p, QRectF(0, 0, size, size));
        p.setCompositionMode(QPainter::CompositionMode_SourceIn);
        p.fillRect(pm.rect(), color);
        p.end();
        icon.addPixmap(pm, mode, state);
    }
}

// Given "foo.svg" return "foo-filled.svg". Returns an empty string
// if the input doesn't end in .svg.
QString filledSiblingPath(const QString& resource) {
    if (!resource.endsWith(QLatin1String(".svg"))) {
        return QString();
    }
    QString sibling = resource;
    sibling.chop(4);
    sibling += QLatin1String("-filled.svg");
    return sibling;
}

}  // namespace

QIcon themedActionIcon(const QString& resource, const QColor& color) {
    QIcon icon;
    addRenderedSvg(icon, resource, color, QIcon::Normal, QIcon::Off);
    if (icon.isNull()) {
        // The base SVG didn't render — return early so we don't
        // attach a checked-state pixmap to an otherwise empty icon.
        return QIcon();
    }
    // Auto-pair "tool-rectangle.svg" with "tool-rectangle-filled.svg"
    // (and similar) for QIcon::On — the QAction checked-state pixmap
    // Qt swaps in when the tool is the armed selection. The pairing
    // is by filename convention so callers stay ignorant of state-
    // variant SVGs; toolbars just pass the base resource.
    const QString filled = filledSiblingPath(resource);
    if (!filled.isEmpty() && QFile::exists(filled)) {
        addRenderedSvg(icon, filled, color, QIcon::Normal, QIcon::On);
    }
    return icon;
}

QIcon themedActionIcon(const QString& resource, const QWidget* widget) {
    const QPalette pal = widget ? widget->palette() : QApplication::palette();
    return themedActionIcon(resource, pal.color(QPalette::WindowText));
}

}  // namespace trailer
