#pragma once

#include <QString>

namespace trailer {

// Which still-capture path Trailer can honestly offer on a non-macOS
// (Linux/BSD) session. Pure policy — no Qt GUI calls — so it is testable
// off-display like the ScreenCaptureBackend seam next door.
//
//   X11Grab          — a genuine X11 (xcb) session or the offscreen/minimal
//                      test plugins: the QScreen::grabWindow(0) path works,
//                      whole-screen only. This selection is byte-identical to
//                      Trailer's historical Linux behaviour.
//   WaylandPortal    — a Wayland session WITH a usable XDG desktop-portal
//                      backend. Reserved for the portal follow-up: the
//                      capture would route through
//                      org.freedesktop.portal.Screenshot. NOT yet wired —
//                      no call site passes portalUsable=true today.
//   WaylandNoCapture — a Wayland session (native OR XWayland) with no usable
//                      capture path. grabWindow(0) returns a null pixmap under
//                      native Wayland and a BLACK (non-null) pixmap under
//                      XWayland on Mutter/KWin, so the honest outcome is to
//                      disable the control with a tooltip (G3) and, at the
//                      capture choke point, surface a message instead of
//                      saving a null/black screenshot.
enum class LinuxCaptureCapability {
    X11Grab,
    WaylandPortal,
    WaylandNoCapture,
};

// Pure decision. `platformName` is QGuiApplication::platformName()
// ("xcb", "wayland", "wayland-egl", "offscreen", "minimal", …).
//
// `underWaylandSession` reports whether the process is running inside a
// Wayland session — computed at the live call sites as
// qEnvironmentVariableIsSet("WAYLAND_DISPLAY"). This matters because on the
// most common Wayland desktop (GNOME) Qt loads the xcb (XWayland) plugin by
// default, so platformName()=="xcb" even though grabWindow(0) yields a BLACK
// pixmap under Mutter/KWin. WAYLAND_DISPLAY is set for XWayland clients and
// UNSET on a genuine X11 session (so no false positives on real X11) and unset
// under the offscreen plugin in CI (so test parity is preserved).
//
// `portalUsable` reports whether an XDG Screenshot portal backend is
// usable; it is hardcoded false at every call site today (the portal
// backend is not implemented — see docs/backlog/2026-07-20-wayland-
// screenshot-portal-dbus.md). The parameter exists so the follow-up portal
// PR flips ONE call site rather than editing this table.
//
// Mapping:
//   platformName starts with "wayland"  -> portalUsable ? WaylandPortal
//                                                        : WaylandNoCapture
//   else if underWaylandSession          -> portalUsable ? WaylandPortal
//                                                        : WaylandNoCapture
//                                          // XWayland: grab is black — degrade
//   else                                 -> X11Grab
//                                          // native X11 (and offscreen/CI)
// The final clause is the guarantee that the existing grabWindow path is
// selected unchanged for genuine X11 and the offscreen test plugin.
LinuxCaptureCapability linuxCaptureCapability(const QString &platformName, bool underWaylandSession,
                                              bool portalUsable);

// The single honest, G3-compliant message shared by BOTH the capture-time
// degrade and the disabled-menu tooltip (states the why and the where-to-go).
QString waylandCaptureUnavailableMessage();

} // namespace trailer
