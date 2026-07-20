#pragma once

#include <QColor>
#include <QIcon>
#include <QPointer>
#include <QString>

#include <vector>

class QWidget;
class QAbstractButton;
class QAction;
class QMenu;

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
//
// If `disabledColor` is valid, an explicit `QIcon::Disabled` pixmap set
// is registered tinted to that colour. This is load-bearing for
// icon-only toolbar buttons: without an explicit Disabled pixmap Qt
// falls back to the style's generated fade, which does essentially
// nothing to these solid-tinted monochrome glyphs — under a dark
// palette a disabled magnifier/zoom/etc. button renders pixel-identical
// to its enabled self (audit: dark-mode disabled-contrast). An
// icon-only button has no greyed label to carry the disabled signal, so
// the glyph itself must dim. Passing the palette's
// QPalette::Disabled foreground keeps the dim consistent with the
// disabled text convention used everywhere else. The widget overload
// supplies this automatically; the colour overload defaults to invalid
// (no Disabled pixmap) so existing behaviour is unchanged unless asked.
QIcon themedActionIcon(const QString& resource, const QColor& color,
                       const QColor& disabledColor = QColor());
QIcon themedActionIcon(const QString& resource, const QWidget* widget = nullptr);

// Create a menu action for a surfaced-but-inert control and GUARANTEE its
// explanation can actually be seen. The action is added to `menu`, given
// `whyTooltip` as its tooltip, and started disabled; the caller wires
// triggers and may later re-enable it (e.g. from updateActionStates).
//
// The load-bearing part is the coupling: setting the tooltip and calling
// menu->setToolTipsVisible(true) happen together, in one place, so it is
// impossible to attach a "here's why this is greyed out / where to go
// instead" tooltip to a menu that would silently never render it. This is
// Gate G3 (no lying controls): a disabled control's explanation is either
// visible on hover or the control shouldn't claim to have one.
//
// Deliberately does NOT set a statusTip — the existing disabled+tooltip
// call sites don't, and adding one would newly surface the text in the
// status bar. Returns the QAction* so callers keep full control.
QAction* makeDisabledAction(QMenu* menu, const QString& text,
                            const QString& whyTooltip);

// Records the (target, resource, tint-source) triples that themedActionIcon
// bakes into fixed-colour pixmaps, so they can be re-tinted after a *live*
// theme (colour-scheme) change. themedActionIcon reads the palette once at
// build time and stamps the glyph pixmaps to that colour; a later palette
// swap (light↔dark) propagates to widgets automatically but does NOT touch
// those already-baked icons — an icon tinted for light stays black on a
// now-dark toolbar. Each themed-icon owner (MainWindow, the toolbars) holds
// one binder, calls apply() at build time in place of a bare setIcon, and
// calls refresh() from the theme-change path to re-tint every binding from
// each target's current palette. Bindings hold QPointers, so a target (or
// its tint source) destroyed before refresh() is skipped, not dereferenced.
class ThemedIconBinder {
  public:
    // Build a themed icon for `resource` tinted to `tintSource`'s palette,
    // assign it to `target`, and remember the binding for refresh(). Returns
    // the icon so call sites that also need it (e.g. to hand to addAction)
    // can reuse it. A null target is recorded harmlessly and skipped later.
    QIcon apply(QAction* target, const QString& resource, const QWidget* tintSource);
    QIcon apply(QAbstractButton* target, const QString& resource, const QWidget* tintSource);

    // Re-tint every live binding from its tint source's current palette.
    // Idempotent and cheap (a fillRect per cached size); safe to call for
    // an unchanged theme.
    void refresh();

  private:
    struct Binding {
        QPointer<QAction> action;
        QPointer<QAbstractButton> button;
        QString resource;
        QPointer<const QWidget> tintSource;
    };
    std::vector<Binding> m_bindings;
};

}  // namespace trailer
