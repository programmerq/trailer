#include "ScreenCaptureBackend.h"

namespace trailer {

// Platform-agnostic, pure helpers only — the string mapping and the
// backend-selection policy. The native probe (screenCaptureKitAvailable)
// and the picker capture (captureViaPickerToPng) live in the Apple-only
// MacScreenCapture.mm and the non-Apple MacScreenCapture_stub.cpp. No
// #ifdef and no native calls belong in this file.

CaptureBackend captureBackendFromString(const QString &value) {
    if (value == QLatin1String("screencapturekit") || value == QLatin1String("sck") ||
        value == QLatin1String("picker")) {
        return CaptureBackend::ScreenCaptureKit;
    }
    // "screencapture", the empty string, and any unrecognised token all
    // fall through to the safe default so a typo can't opt a user into the
    // unvalidated picker path.
    return CaptureBackend::Screencapture;
}

QString captureBackendToString(CaptureBackend backend) {
    switch (backend) {
    case CaptureBackend::Screencapture:
        return QStringLiteral("screencapture");
    case CaptureBackend::ScreenCaptureKit:
        return QStringLiteral("screencapturekit");
    }
    return QStringLiteral("screencapture");
}

CaptureBackend effectiveCaptureBackend(CaptureBackend configured, bool screenCaptureKitAvailable,
                                       bool freeformRegion) {
    if (configured == CaptureBackend::ScreenCaptureKit && screenCaptureKitAvailable &&
        !freeformRegion) {
        return CaptureBackend::ScreenCaptureKit;
    }
    return CaptureBackend::Screencapture;
}

LinuxScreenshotBackend chooseLinuxScreenshotBackend(bool isWaylandSession, bool portalAvailable) {
    // X11 / xcb / offscreen: a client-side root grab works, so keep the
    // long-standing QScreen::grabWindow path and ignore the portal entirely.
    if (!isWaylandSession)
        return LinuxScreenshotBackend::QScreenGrab;
    // Wayland: grabWindow returns null (client-side grabs are blocked), so a
    // real capture requires the portal. Without it, signal honest-degrade
    // rather than letting the caller silently produce nothing.
    return portalAvailable ? LinuxScreenshotBackend::Portal
                           : LinuxScreenshotBackend::Unavailable;
}

} // namespace trailer
