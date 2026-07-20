#pragma once

#include <QString>

namespace trailer {

// Which native still-capture path Trailer drives on macOS.
//
//   Screencapture    — the long-standing path: shell out to
//                      /usr/sbin/screencapture. Requires the Screen
//                      Recording TCC grant and pops the OS permission
//                      prompt on first use. This is the safe default and
//                      the only path exercised in CI.
//   ScreenCaptureKit — present SCContentSharingPicker and capture the
//                      user's pick via SCScreenshotManager. The picker is
//                      a system-trusted surface, so the user's explicit
//                      pick authorises the capture without a separate TCC
//                      grant. Apple-only; gated behind the capture_backend
//                      setting and only selected once validated on-device
//                      (see ADR 0015 + the GPSC.2 smoke test).
//
// API: this enum is serialised by its *string* mapping in settings.toml
// (captureBackendToString / captureBackendFromString), never by ordinal —
// the enum may be renumbered freely, but the persisted strings must not
// change.
enum class CaptureBackend {
    Screencapture,
    ScreenCaptureKit,
};

// Parse the persisted capture_backend string. "screencapturekit" / "sck" /
// "picker" select ScreenCaptureKit; everything else — "screencapture", the
// empty string, or any unknown token — maps to Screencapture, the safe
// default, so a typo in settings.toml never silently opts a user into the
// unvalidated path.
CaptureBackend captureBackendFromString(const QString &value);

// Serialise a CaptureBackend to its canonical settings.toml string
// (Screencapture -> "screencapture", ScreenCaptureKit -> "screencapturekit").
QString captureBackendToString(CaptureBackend backend);

// Pure policy: resolve the backend actually used for one capture. Returns
// ScreenCaptureKit iff the user configured it AND ScreenCaptureKit is
// available on this machine AND the requested capture is not a freeform
// region (the picker offers display/window picks, not a drag-to-select
// rectangle, so region requests fall back to screencapture). Otherwise
// returns Screencapture. No platform calls — testable off-Mac.
CaptureBackend effectiveCaptureBackend(CaptureBackend configured, bool screenCaptureKitAvailable,
                                       bool freeformRegion);

// Native probe: true only on macOS 14+ with ScreenCaptureKit's
// SCContentSharingPicker present; false on every other platform (the
// non-Apple stub always returns false).
bool screenCaptureKitAvailable();

// Outcome of a ScreenCaptureKit picker capture.
//
//   Ok          — the user picked a source and a PNG was written to the
//                 requested path.
//   Cancelled   — the user dismissed the picker without picking.
//   Unavailable — ScreenCaptureKit is not usable on this machine (non-Apple
//                 platform, or macOS < 14). Callers should fall back.
//   Failed      — the picker started but capture or PNG encoding failed;
//                 errorOut carries a human-readable reason.
enum class PickerCaptureResult {
    Ok,
    Cancelled,
    Unavailable,
    Failed,
};

// Native: present SCContentSharingPicker and, on the user's pick, capture
// via SCScreenshotManager and write a PNG to outPngPath. Blocks until the
// user picks or cancels. wholeDisplay only hints the picker's preferred
// mode (display vs window); the user's pick governs the actual capture.
// On failure, *errorOut (when non-null) is set to a human-readable reason.
// The non-Apple stub always returns Unavailable.
// Must be called on the main (GUI) thread; BLOCKS it until the user picks or cancels (or a timeout elapses).
PickerCaptureResult captureViaPickerToPng(const QString &outPngPath, bool wholeDisplay,
                                          QString *errorOut);

} // namespace trailer
