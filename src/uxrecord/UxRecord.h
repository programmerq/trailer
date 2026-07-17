#pragma once

#include <QJsonObject>
#include <QString>

namespace trailer {

class MainWindow;
class UxRecorder;

// Facade for the developer UX session recorder (docs/ux-recorder.md).
//
// This header is compiled into every build. When the project is
// configured without TRAILER_ENABLE_UX_RECORDER, everything below
// inlines to a no-op, so MainWindow / Application call sites stay free
// of #ifdef noise and default builds carry no recorder behaviour, UI,
// permissions traffic, or capture-framework linkage.
//
// The recorder is strictly local: events, screen frames, camera
// segments, and logs are written under AppPaths::uxSessionsDir() and
// nothing is ever transmitted anywhere (PHILOSOPHY.md "No telemetry").
namespace uxrecord {

#ifdef TRAILER_UX_RECORDER

// The active recorder, or nullptr when no --ux-record session is
// running. Owned by Application; set/cleared by UxRecorder start/stop.
UxRecorder *recorder();

// True while a recording session is live.
bool isActive();

// Append a Trailer-level semantic event ("document_opened",
// "operation_failed", "preview_fallback_started", …) to the session's
// events.jsonl. Safe to call from any thread; no-op when no session is
// active, so call sites never need their own guard.
void recordEvent(const QString &type, const QJsonObject &data = {});

// Install the recorder's UI (status-bar indicator, Recording menu,
// marker shortcuts, Preview hand-off) and semantic instrumentation
// into a MainWindow. Called once at the end of the MainWindow
// constructor; no-op when no session is active.
void attachToMainWindow(MainWindow *window);

#else

inline UxRecorder *recorder() {
    return nullptr;
}
constexpr bool isActive() {
    return false;
}
inline void recordEvent(const QString &, const QJsonObject & = {}) {}
inline void attachToMainWindow(MainWindow *) {}

#endif

} // namespace uxrecord
} // namespace trailer
