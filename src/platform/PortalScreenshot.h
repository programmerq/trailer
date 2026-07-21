#pragma once

#include <QString>

// XDG desktop-portal still-capture backend (Wayland).
//
// Under Wayland a client may not grab the screen directly — QScreen::grabWindow
// returns a null pixmap — so a whole-screen capture must go through
// org.freedesktop.portal.Screenshot, the compositor-mediated screenshot portal
// (backed by e.g. xdg-desktop-portal-wlr / -gnome / -kde). This is local D-Bus
// IPC to the session portal service, NOT an outbound network call, so it stays
// within Trailer's no-telemetry / no-network constraint.
//
// The real implementation (QtDBus) is compiled only on Linux/BSD; every other
// platform links the stub in PortalScreenshot_stub.cpp, which reports the
// portal as unavailable so the selection policy in ScreenCaptureBackend
// (chooseLinuxScreenshotBackend) never routes to it off-platform.

namespace trailer {

// True when the current Qt session is Wayland (platformName() == "wayland" or a
// "wayland-*" variant). Thin wrapper over QGuiApplication::platformName() so the
// call site reads intent, not a string test. Safe to call on any platform.
bool isWaylandSession();

// True when org.freedesktop.portal.Screenshot is reachable on the session bus
// (the portal frontend owns org.freedesktop.portal.Desktop and exports the
// Screenshot interface). A cheap, side-effect-free probe used both to gate the
// menu affordance (disabled + tooltip when false, per G3) and to select the
// capture backend. Always false on the non-Linux stub.
bool portalScreenshotAvailable();

// Outcome of a portal screenshot request.
//
//   Ok          — the portal captured and a PNG was written to outPngPath.
//   Cancelled   — the user dismissed the portal's interactive prompt.
//   Unavailable — no screenshot portal is reachable on this session bus.
//   Failed      — the portal was called but returned an error, or the result
//                 file could not be read/copied; errorOut carries the reason.
enum class PortalCaptureResult {
    Ok,
    Cancelled,
    Unavailable,
    Failed,
};

// Drive org.freedesktop.portal.Screenshot.Screenshot and copy the resulting
// image to outPngPath. Blocks the calling (GUI) thread on a nested event loop
// until the portal's async Request.Response arrives or a timeout elapses.
//
// interactive=false requests a non-interactive grab (the automated path);
// interactive=true lets the portal show its own selection/permission UI where
// the backend supports it. On failure, *errorOut (when non-null) is set to a
// human-readable reason. The non-Linux stub always returns Unavailable.
PortalCaptureResult capturePortalScreenshotToPng(const QString &outPngPath, bool interactive,
                                                 QString *errorOut);

} // namespace trailer
