#pragma once

#include "uxrecord/UxEventStream.h"

#include <QFile>
#include <QJsonObject>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

class QTimer;

namespace trailer {

class UxPlatformCapture;
class UxQtEventCapture;

// Map a capture event type to the stream it degrades ("screen",
// "camera", "input", "platform"), or "" if the event is not a
// capture failure. Pure + free so it can be unit-tested without a
// recorder; the start() classifier and tests share it. See UXR-002 in
// docs/ux-recorder-todo.md.
QString uxDegradedStreamForEventType(const QString &type);

// Owner of one --ux-record session (docs/ux-recorder.md).
//
// Platform-neutral half of the recorder: creates the session
// directory under AppPaths::uxSessionsDir(), owns events.jsonl
// (UxEventStream), tees Qt log output into trailer.log, installs the
// application-wide Qt input observer (UxQtEventCapture), and drives
// the platform capture backend (screen / camera / foreground app /
// global input). Trailer-facing semantic instrumentation lives in
// UxTrailerHooks.cpp and reaches this class through the
// uxrecord:: facade.
//
// Lifecycle: Application constructs and start()s it before the first
// window exists; stop() runs on aboutToQuit (self-connected) and from
// the destructor, and is idempotent. Sessions that never reach stop()
// keep manifest.json status "recording"; the next start() sweeps those
// whose pid is gone and rewrites them as "crashed".
//
// Strictly local output — no network classes are used anywhere in
// src/uxrecord/ (PHILOSOPHY.md "No telemetry").
class UxRecorder : public QObject {
    Q_OBJECT

  public:
    // `baseDirOverride` redirects the ux-sessions root (tests use a
    // QTemporaryDir); empty means AppPaths::uxSessionsDir().
    // `withPlatformCapture=false` skips the screen/camera/input
    // backend so platform-neutral tests stay headless-safe.
    explicit UxRecorder(QString baseDirOverride = {}, bool withPlatformCapture = true,
                        QObject *parent = nullptr);
    ~UxRecorder() override;

    // Create the session directory and begin recording. Returns false
    // (and records nothing, leaving the facade inactive) when the
    // directory or events.jsonl cannot be created.
    bool start();
    void stop();
    bool isRecording() const { return m_recording; }

    QString sessionId() const { return m_sessionId; }
    QString sessionDir() const { return m_sessionDir; }

    // Append one event. Thread-safe (UxEventStream serialises).
    // `source` is one of "session", "trailer", "qt", "macos".
    void recordEvent(const QString &source, const QString &type, const QJsonObject &data = {});
    // Shorthand for source "trailer" — the semantic event stream.
    void recordTrailerEvent(const QString &type, const QJsonObject &data = {});

    // Insert a manual timeline marker (kind: "frustration",
    // "unexpected_behavior", "important_moment", "note",
    // "preview_handoff") and, when called on the GUI thread with an
    // active window, save a screenshot of it under screenshots/.
    // Safe to call from any thread; marshals itself to the GUI thread.
    void insertMarker(const QString &kind, const QString &note = {});

    qint64 elapsedMs() const { return m_stream.elapsedMs(); }

    // Record that a capture stream is degraded for this session (e.g.
    // "screen" when Screen Recording isn't granted). Accumulated into
    // manifest.json's `degraded` array — so a downstream consumer sees
    // a missing stream without scanning events.jsonl — and broadcast
    // via degradedStreamsChanged for the recording indicator. The
    // start() classifier calls this through uxDegradedStreamForEventType;
    // exposed publicly so tests can drive it directly. GUI-thread only;
    // idempotent per stream; no-op once recording has stopped. UXR-002.
    void reportStreamDegraded(const QString &stream);
    QStringList degradedStreams() const { return m_degradedStreams; }

    bool platformCaptureSupported() const;
    // Master pause for screen-frame retention + global input
    // observation (camera keeps rolling; see docs/ux-recorder.md).
    void setVisualCapturePaused(bool paused);
    bool visualCapturePaused() const;

  signals:
    // A capture permission / availability problem worth surfacing in
    // the status bar ("Screen Recording permission missing…").
    // Emitted on the GUI thread.
    void captureIssue(const QString &message);
    void markerInserted(const QString &kind);
    // The sorted set of degraded streams, emitted whenever it grows.
    // Drives the persistent "● REC · no screen" indicator state so a
    // degraded stream is visible for the whole session, not just at
    // the moment it failed. UXR-002.
    void degradedStreamsChanged(const QStringList &streams);

  private:
    void writeMetadata();
    void writeManifest(const QString &status);
    void markStaleSessions();
    void flushNow();
    void saveMarkerScreenshot(quint64 sequence, const QString &kind);
    QJsonObject captureConfigJson() const;

    QString m_baseDirOverride;
    bool m_withPlatformCapture = true;

    QString m_sessionId;
    QString m_sessionDir;
    QString m_startedUtc;
    bool m_recording = false;

    UxEventStream m_stream;
    QTimer *m_flushTimer = nullptr;
    UxQtEventCapture *m_qtCapture = nullptr;
    std::unique_ptr<UxPlatformCapture> m_platformCapture;

    // trailer.log sink fed by the installed Qt message handler.
    QFile m_logFile;
    QMutex m_logMutex;
    quint64 m_markerCount = 0;
    // Sorted, de-duplicated set of degraded capture streams. Touched
    // only on the GUI thread (reportStreamDegraded), read by
    // writeManifest. UXR-002.
    QStringList m_degradedStreams;
};

} // namespace trailer
