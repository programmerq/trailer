#include "IconHelper.h"

#include <QApplication>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QSvgRenderer>
#include <QWidget>

namespace trailer {

QIcon themedActionIcon(const QString& resource, const QColor& color) {
    QSvgRenderer renderer(resource);
    if (!renderer.isValid()) {
        return QIcon();
    }
    QIcon icon;
    for (int size : {16, 18, 24, 32, 36, 48, 64}) {
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        renderer.render(&p, QRectF(0, 0, size, size));
        p.setCompositionMode(QPainter::CompositionMode_SourceIn);
        p.fillRect(pm.rect(), color);
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

QIcon themedActionIcon(const QString& resource, const QWidget* widget) {
    const QPalette pal = widget ? widget->palette() : QApplication::palette();
    return themedActionIcon(resource, pal.color(QPalette::WindowText));
}

}  // namespace trailer
