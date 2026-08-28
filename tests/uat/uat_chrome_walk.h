// Shared widget-tree chrome walker for the chrome census
// (test_uat_chrome_census.cpp, UAT-XCT-090) and the generalized
// constancy sweep (test_uat_constancy_sweep.cpp, UAT-XCT-093).
//
// Why a tree walk and not a registry: chrome can bypass any registration
// point — the transient zoom readout is a floating QLabel parented
// straight to the MainWindow and positioned by move() (see the
// m_zoomIndicator construction comment in src/ui/MainWindow.cpp). Only a
// walk over the realized widget tree sees what is actually on screen.
//
// Identity scheme (stable across platforms, fonts, dpr and geometry):
//   1. objectName, when set and not a Qt-internal "qt_*" name.
//   2. QToolButton with a defaultAction: "<class>:<action objectName or
//      text>" — main-toolbar/markup-toolbar buttons are created from
//      QActions and carry no objectName of their own.
//   3. Other buttons with text: "<class>:<text>" (mnemonic '&' stripped).
//   4. Bare class name (with the trailer:: namespace stripped), plus a
//      "#<n>" ordinal when siblings share the same base id (n counts
//      same-id siblings in children() order, which is construction order
//      and therefore deterministic; the first carries no suffix).
// An element's full id is the '/'-joined chain of these from the window
// down. QLabel text is deliberately NOT part of the identity — labels
// like the zoom readout change text at runtime.
//
// Walk rules:
//   - Only visible widgets are walked (hidden subtrees are not entered);
//     the census is a census of the *at-rest, on-screen* surface.
//   - DocumentView is recorded as one element and NOT descended into:
//     everything below it is the document being viewed — the subject,
//     not chrome. (An overlay smuggled in as a floating child of the
//     MainWindow — the escape hatch this walker exists to catch — is
//     outside DocumentView and is seen.)
//   - QMenuBar is skipped in the tree walk (it is a native, non-widget
//     surface on macOS, so its widget visibility is platform-dependent);
//     the menu SURFACE is censused separately from menuBar()->actions(),
//     which exist on every platform. Top-level menu titles only — menu
//     *contents* include dynamic entries (Open Recent, the Window list)
//     and are covered by their own UAT slots.
//   - QSizeGrip is skipped: it is platform-style furniture Qt adds or
//     omits per-QPA, not app chrome.
//   - Widgets with a "qt_*" objectName (Qt internals such as the toolbar
//     overflow chevron) are not recorded, but their subtree is still
//     entered — their name joins the path so descendants stay unique.

#pragma once

#include "ui/DocumentView.h"

#include <QAbstractButton>
#include <QHash>
#include <QList>
#include <QMenuBar>
#include <QPoint>
#include <QSizeGrip>
#include <QString>
#include <QStringList>
#include <QToolButton>
#include <QWidget>

namespace trailer_uat {

inline QString censusBaseId(QWidget *w) {
    const QString name = w->objectName();
    if (!name.isEmpty() && !name.startsWith(QLatin1String("qt_")))
        return name;
    QString cls = QString::fromLatin1(w->metaObject()->className());
    cls.remove(QStringLiteral("trailer::"));
    auto stripped = [](QString t) {
        t.remove(QLatin1Char('&'));
        return t;
    };
    if (auto *tb = qobject_cast<QToolButton *>(w)) {
        if (QAction *a = tb->defaultAction()) {
            const QString an = a->objectName().isEmpty() ? a->text() : a->objectName();
            if (!an.isEmpty())
                return cls + QLatin1Char(':') + stripped(an);
        }
        if (!tb->text().isEmpty())
            return cls + QLatin1Char(':') + stripped(tb->text());
        return cls;
    }
    if (auto *b = qobject_cast<QAbstractButton *>(w)) {
        if (!b->text().isEmpty())
            return cls + QLatin1Char(':') + stripped(b->text());
    }
    return cls;
}

struct ChromeElement {
    QString id; // full '/'-joined path from (exclusive) the window down
    QWidget *widget = nullptr;
};

inline void walkChromeChildren(QWidget *parent, const QString &prefix,
                               QList<ChromeElement> &out) {
    // Base ids of the visible direct children, in children() order, so
    // duplicate ids can carry a deterministic ordinal.
    QHash<QString, int> seen;
    const auto children = parent->children();
    for (QObject *o : children) {
        auto *c = qobject_cast<QWidget *>(o);
        if (!c || c->isWindow())
            continue;
        if (!c->isVisible())
            continue;
        if (qobject_cast<QSizeGrip *>(c))
            continue;
        if (qobject_cast<QMenuBar *>(c))
            continue; // censused via menuTitles() instead — see header note
        QString base = censusBaseId(c);
        const int ordinal = seen.value(base, 0);
        seen[base] = ordinal + 1;
        if (ordinal > 0)
            base += QStringLiteral("#%1").arg(ordinal);
        const QString path = prefix.isEmpty() ? base : prefix + QLatin1Char('/') + base;
        const bool record = !c->objectName().startsWith(QLatin1String("qt_"));
        if (record)
            out.append({path, c});
        if (qobject_cast<trailer::DocumentView *>(c))
            continue; // the document is the subject, not chrome
        walkChromeChildren(c, path, out);
    }
}

// All visible chrome elements under `window` (the window itself is not an
// entry). Deterministic given a settled widget tree.
inline QList<ChromeElement> walkChrome(QWidget *window) {
    QList<ChromeElement> out;
    walkChromeChildren(window, QString(), out);
    return out;
}

// Top-level menu titles, mnemonics stripped. Platform-independent (the
// QActions exist even where the menu bar itself is native).
inline QStringList menuTitles(QMenuBar *bar) {
    QStringList out;
    if (!bar)
        return out;
    const auto actions = bar->actions();
    for (QAction *a : actions) {
        if (a->isSeparator())
            continue;
        QString t = a->text();
        t.remove(QLatin1Char('&'));
        out << t;
    }
    return out;
}

// id -> top-left mapped into `window` coordinates, for every censused
// element. mapTo(window) (not pos()) so a shifted ancestor is charged to
// every control inside it, exactly as the user experiences the shift.
inline QHash<QString, QPoint> chromePositions(QWidget *window) {
    QHash<QString, QPoint> out;
    const auto elements = walkChrome(window);
    for (const ChromeElement &e : elements)
        out.insert(e.id, e.widget->mapTo(window, QPoint(0, 0)));
    return out;
}

} // namespace trailer_uat
