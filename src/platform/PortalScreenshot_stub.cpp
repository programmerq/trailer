#include "PortalScreenshot.h"

#include "ScreenCaptureBackend.h"

#include <QGuiApplication>

// Non-Linux stub: the XDG desktop portal is a freedesktop/Wayland concept, so
// macOS and Windows never route through it. Keeping these symbols here means
// the shared non-macOS capture code in Application::captureScreenshot links
// cleanly on Windows without pulling in Qt6::DBus, and the selection policy
// (chooseLinuxScreenshotBackend) always sees the portal as unavailable there,
// so it resolves to QScreenGrab — the platform's existing behaviour.

namespace trailer {

bool isWaylandSession() {
    // Honest even off-Linux: a Windows/macOS Qt session is never "wayland" and
    // sets neither WAYLAND_DISPLAY nor XDG_SESSION_TYPE=wayland, so this stays
    // false there. Shares the same display-server-signal detection as the real
    // backend so the two can't drift.
    return isWaylandSessionFromSignals(QGuiApplication::platformName(),
                                       qEnvironmentVariable("WAYLAND_DISPLAY"),
                                       qEnvironmentVariable("XDG_SESSION_TYPE"));
}

bool portalScreenshotAvailable() {
    return false;
}

PortalCaptureResult capturePortalScreenshotToPng(const QString &, bool, QString *errorOut) {
    if (errorOut)
        *errorOut = QStringLiteral("the XDG screenshot portal is not available on this platform");
    return PortalCaptureResult::Unavailable;
}

} // namespace trailer
