#pragma once

#include <QJsonObject>
#include <QString>

#include <functional>
#include <memory>

namespace trailer {

// Everything a platform capture backend needs from the recorder,
// handed over at construction so the backend never reaches back into
// UxRecorder (or Qt GUI state) directly.
struct UxCaptureContext {
    // Absolute session directory. Backends write only beneath the
    // pre-created screen/ and camera/ subdirectories.
    QString sessionDir;

    // Bundle identifier of the sanctioned fallback application whose
    // foreground time stays in the recording (com.apple.Preview).
    QString previewBundleId;

    // True when printable key text may be written to the event
    // stream (mirrors the Qt-side capture's setting).
    bool captureKeyText = true;

    // Session-monotonic clock, milliseconds since session start.
    // Thread-safe; used to timestamp frames/segments so every capture
    // artefact aligns with events.jsonl.
    std::function<qint64()> elapsedMs;

    // Append an event to events.jsonl with source "macos" (or the
    // platform's name). Thread-safe; may be called from any queue or
    // thread the backend uses internally.
    std::function<void(const QString &type, const QJsonObject &data)> emitEvent;

    // Invoked when the user hits the global frustration-marker hotkey
    // while a *non-Trailer* allowed app (Preview) is frontmost — the
    // in-app QAction shortcut covers Trailer itself. May be called
    // from the backend's input thread; the recorder marshals to the
    // GUI thread. Optional.
    std::function<void()> frustrationHotkey;
};

// Platform half of the UX recorder: screen capture, camera capture,
// foreground-application tracking, and (where the OS allows) global
// input observation while Trailer or Preview is frontmost.
//
// The platform-neutral recorder owns the session, the event stream,
// and all Trailer semantics; backends only produce capture artefacts
// and "what the OS saw" events. macOS implements this with
// ScreenCaptureKit + AVFoundation + NSWorkspace + a CGEventTap
// (MacUxPlatformCapture.mm); other platforms currently compile the
// stub (StubUxPlatformCapture.cpp), which reports itself unavailable
// in the event stream and leaves the rest of the recorder working.
//
// Pausing on app switches is the backend's own responsibility (it is
// the one watching the foreground app); setPaused() is the additional
// user-driven master switch for visual/input capture.
class UxPlatformCapture {
  public:
    virtual ~UxPlatformCapture() = default;

    // False for stub backends — callers hide capture-related UI.
    virtual bool isSupported() const = 0;

    // Begin capture. Asynchronous: permission prompts and stream
    // start-up complete in the background, reporting outcomes as
    // events (screen_capture_started, camera_permission_denied, …).
    // Must not block the GUI thread and must not crash when any
    // permission is missing — partial capture is expected.
    virtual void start() = 0;

    // Stop and finalise capture artefacts. Called on the GUI thread
    // during shutdown; implementations may block briefly (bounded,
    // a couple of seconds) to close video files cleanly.
    virtual void stop() = 0;

    // Master pause for screen-frame retention and global input
    // observation. The camera stream keeps rolling (it records the
    // user, not the screen); see docs/ux-recorder.md.
    virtual void setPaused(bool paused) = 0;
    virtual bool isPaused() const = 0;
};

// Factory implemented once per platform (MacUxPlatformCapture.mm or
// StubUxPlatformCapture.cpp — exactly one is compiled in).
std::unique_ptr<UxPlatformCapture> createUxPlatformCapture(UxCaptureContext context);

// Relaunch-required-permission preflight for the startup gate (UXR-001).
//
// macOS applies a Screen Recording grant only to FUTURE launches of a
// binary — granting mid-session does not enable capture for the running
// process — so a session started without it silently produces zero
// screen frames. uxScreenRecordingGranted() is a read-only check (no
// prompt) the app calls BEFORE recording to decide whether to warn.
// uxRequestScreenRecording() triggers the system request so the app is
// registered in the Screen Recording privacy list (making the Settings
// deep-link useful); call it only when the user is about to head there.
//
// Both are no-ops-with-sane-defaults off macOS: granted() returns true
// (so non-macOS recorder builds never gate), request() does nothing.
bool uxScreenRecordingGranted();
void uxRequestScreenRecording();

} // namespace trailer
