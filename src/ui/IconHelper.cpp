#include "IconHelper.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QFile>
#include <QHBoxLayout>
#include <QMenu>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QStyle>
#include <QSvgRenderer>
#include <QToolButton>
#include <QWidget>

namespace trailer {

namespace {

// Recolour a copy of `base` to `color` via CompositionMode_SourceIn
// (keeps base's alpha, replaces its RGB) and return it.
QPixmap tinted(const QPixmap& base, const QColor& color) {
    QPixmap pm = base;
    QPainter p(&pm);
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(pm.rect(), color);
    p.end();
    return pm;
}

// Render one SVG resource into the QIcon at every size we plausibly
// need. Pixmaps are tinted to `color` via SourceIn so the original SVG
// paint colour doesn't matter — only its alpha channel does. If
// `disabledColor` is valid, a QIcon::Disabled pixmap is added at the
// same `state` from the SAME rendered base — a cheap recolour, not a
// second SVG render, so honest disabled contrast (see the header note)
// costs one extra fillRect per size, not a doubled render pass.
void addRenderedSvg(QIcon& icon, const QString& resource,
                    const QColor& color,
                    QIcon::Mode mode, QIcon::State state,
                    const QColor& disabledColor = QColor()) {
    QSvgRenderer renderer(resource);
    if (!renderer.isValid()) {
        return;
    }
    for (int size : {16, 18, 24, 32, 36, 48, 64}) {
        QPixmap base(size, size);
        base.fill(Qt::transparent);
        QPainter p(&base);
        renderer.render(&p, QRectF(0, 0, size, size));
        p.end();
        icon.addPixmap(tinted(base, color), mode, state);
        if (disabledColor.isValid()) {
            icon.addPixmap(tinted(base, disabledColor), QIcon::Disabled, state);
        }
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

QIcon themedActionIcon(const QString& resource, const QColor& color,
                       const QColor& disabledColor) {
    QIcon icon;
    // Explicit Disabled pixmaps are load-bearing: without them Qt asks
    // the style to synthesise the disabled look from the Normal pixmap,
    // and for these solid-tinted alpha glyphs the synthesised fade is
    // imperceptible — under a dark palette a disabled icon-only button
    // is pixel-identical to its enabled self, so the greyed state lies
    // (Gate G3 spirit: a control's state must read honestly). Rendering
    // the glyph in the palette's disabled foreground makes the dim
    // explicit and matches the disabled-text convention everywhere else.
    addRenderedSvg(icon, resource, color, QIcon::Normal, QIcon::Off, disabledColor);
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
        addRenderedSvg(icon, filled, color, QIcon::Normal, QIcon::On, disabledColor);
    }
    return icon;
}

QIcon themedActionIcon(const QString& resource, const QWidget* widget) {
    const QPalette pal = widget ? widget->palette() : QApplication::palette();
    return themedActionIcon(resource, pal.color(QPalette::WindowText),
                            pal.color(QPalette::Disabled, QPalette::WindowText));
}

QIcon ThemedIconBinder::apply(QAction* target, const QString& resource,
                              const QWidget* tintSource) {
    const QIcon icon = themedActionIcon(resource, tintSource);
    if (target)
        target->setIcon(icon);
    m_bindings.push_back({target, nullptr, resource, tintSource});
    return icon;
}

QIcon ThemedIconBinder::apply(QAbstractButton* target, const QString& resource,
                              const QWidget* tintSource) {
    const QIcon icon = themedActionIcon(resource, tintSource);
    if (target)
        target->setIcon(icon);
    m_bindings.push_back({nullptr, target, resource, tintSource});
    return icon;
}

void ThemedIconBinder::refresh() {
    for (const Binding& b : m_bindings) {
        // Skip a binding whose target died before this refresh — the
        // QPointer guards make a stale entry a no-op rather than a crash.
        const bool haveTarget = b.action || b.button;
        if (!haveTarget)
            continue;
        // Re-tint from the target's own palette when the recorded tint
        // source is gone; the target widget/action host carries the live
        // (post-swap) palette either way.
        const QWidget* tint = b.tintSource ? b.tintSource.data() : nullptr;
        const QIcon icon = themedActionIcon(b.resource, tint);
        if (b.action)
            b.action->setIcon(icon);
        else if (b.button)
            b.button->setIcon(icon);
    }
}

QAction* makeDisabledAction(QMenu* menu, const QString& text,
                            const QString& whyTooltip) {
    QAction* action = menu->addAction(text);
    action->setToolTip(whyTooltip);
    action->setEnabled(false);
    // Couple the tooltip to the one thing that makes it visible. Setting
    // this per-menu is idempotent, so calling it once per disabled action
    // is harmless — but it means no disabled+explained action can ever be
    // attached to a menu that would swallow its tooltip.
    menu->setToolTipsVisible(true);
    return action;
}

QWidget* buildTextlessDockTitleBar(QDockWidget* dock) {
    auto* bar = new QWidget(dock);
    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->addStretch();
    // Native ordering: float toggle (if the dock supports it) left of close,
    // both right-aligned — matches the stock QDockWidget title-bar layout.
    if (dock->features().testFlag(QDockWidget::DockWidgetFloatable)) {
        auto* floatButton = new QToolButton(bar);
        floatButton->setAutoRaise(true);
        floatButton->setIcon(bar->style()->standardIcon(QStyle::SP_TitleBarNormalButton));
        floatButton->setToolTip(QDockWidget::tr("Float"));
        floatButton->setAccessibleName(QDockWidget::tr("Float %1").arg(dock->windowTitle()));
        QObject::connect(floatButton, &QToolButton::clicked, dock,
                         [dock]() { dock->setFloating(!dock->isFloating()); });
        layout->addWidget(floatButton);
    }
    if (dock->features().testFlag(QDockWidget::DockWidgetClosable)) {
        auto* closeButton = new QToolButton(bar);
        closeButton->setAutoRaise(true);
        closeButton->setIcon(bar->style()->standardIcon(QStyle::SP_TitleBarCloseButton));
        closeButton->setToolTip(QDockWidget::tr("Close"));
        closeButton->setAccessibleName(QDockWidget::tr("Close %1").arg(dock->windowTitle()));
        QObject::connect(closeButton, &QToolButton::clicked, dock, &QDockWidget::close);
        layout->addWidget(closeButton);
    }
    return bar;
}

}  // namespace trailer
