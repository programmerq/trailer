#include "ScreenCaptureBackend.h"

namespace trailer {

// Non-Apple stub: ScreenCaptureKit is macOS-only, so the picker path is
// never available here. These definitions provide the same symbols the
// Apple-only MacScreenCapture.mm exports, keeping every non-Apple build
// link-clean. effectiveCaptureBackend already collapses to Screencapture
// whenever screenCaptureKitAvailable() is false, so these are belt-and-
// braces for any direct caller.

bool screenCaptureKitAvailable() {
    return false;
}

PickerCaptureResult captureViaPickerToPng(const QString &, bool, QString *err) {
    if (err) {
        *err = QStringLiteral("ScreenCaptureKit is macOS-only");
    }
    return PickerCaptureResult::Unavailable;
}

} // namespace trailer
