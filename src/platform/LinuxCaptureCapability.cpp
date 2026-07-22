#include "LinuxCaptureCapability.h"

#include <QCoreApplication>

namespace trailer {

// Platform-agnostic pure policy only — no #ifdef, no native calls, no Qt GUI
// objects. Mirrors ScreenCaptureBackend.cpp so it builds and tests everywhere.

LinuxCaptureCapability linuxCaptureCapability(const QString &platformName, bool underWaylandSession,
                                              bool portalUsable) {
    // The native Wayland plugin departs from the historical grabWindow path:
    // startsWith so "wayland", "wayland-egl", etc. all match.
    if (platformName.startsWith(QLatin1String("wayland"))) {
        return portalUsable ? LinuxCaptureCapability::WaylandPortal
                            : LinuxCaptureCapability::WaylandNoCapture;
    }
    // XWayland: platformName()=="xcb" but we are inside a Wayland session
    // (WAYLAND_DISPLAY set), so grabWindow(0) yields a BLACK pixmap on
    // Mutter/KWin — degrade honestly rather than save a black screenshot.
    if (underWaylandSession) {
        return portalUsable ? LinuxCaptureCapability::WaylandPortal
                            : LinuxCaptureCapability::WaylandNoCapture;
    }
    // Genuine X11 (WAYLAND_DISPLAY unset), plus offscreen / minimal / anything
    // unrecognised: the QScreen grabWindow(0) path is selected unchanged. This
    // clause keeps X11 (and the offscreen test plugin) byte-identical to today.
    return LinuxCaptureCapability::X11Grab;
}

QString waylandCaptureUnavailableMessage() {
    // G3: the "why" (needs the desktop portal, not yet wired on Wayland) and
    // the "where to go" (a native X11 session works today). Softened so it is
    // also honest for an XWayland user, who is on a Wayland session even though
    // Qt reports platformName()=="xcb".
    return QCoreApplication::translate(
        "LinuxCaptureCapability",
        "Screenshot capture isn't available on this Wayland session yet — it needs the "
        "desktop portal. It works when running Trailer under a native X11 session.");
}

} // namespace trailer
