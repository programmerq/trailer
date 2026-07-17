#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonObject>
#include <QList>
#include <QMutex>
#include <QString>

namespace trailer {

// Version stamped into every event envelope and into metadata.json /
// manifest.json. Bump when an analysis tool would need to branch on
// the layout (renamed envelope fields, changed timestamp format, …).
inline constexpr int kUxSchemaVersion = 1;

// Append-only JSON Lines writer for a UX recording session.
//
// One instance per session, owning events.jsonl. Every append stamps
// the shared envelope:
//
//   {"schema_version":1, "session_id":"…", "sequence":N,
//    "timestamp_utc":"…ISO-8601 with ms…", "elapsed_ms":M,
//    "source":"trailer|qt|macos|session", "type":"…", "data":{…}}
//
// `sequence` is a strictly increasing per-session counter and
// `elapsed_ms` comes from a monotonic QElapsedTimer started at open(),
// so events are orderable even across the wall-clock anomalies (NTP
// slew, DST) that timestamp_utc is exposed to.
//
// Threading: append() may be called from any thread (capture backends
// deliver from dispatch queues / the event-tap thread). Serialization
// happens on the caller's thread; lines accumulate in a small buffer
// that flush() drains to disk. The owner calls flush() from a timer so
// a crash loses at most one flush interval of events, and the buffer
// self-flushes at kMaxBufferedLines so an event storm can't grow
// memory without bound.
class UxEventStream {
  public:
    UxEventStream();
    ~UxEventStream();

    UxEventStream(const UxEventStream &) = delete;
    UxEventStream &operator=(const UxEventStream &) = delete;

    // Create/truncate `filePath` and start the monotonic clock.
    // Returns false (and stays closed) when the file cannot be opened.
    bool open(const QString &filePath, const QString &sessionId);

    bool isOpen() const;

    // Stamp the envelope around `data` and queue one JSONL line.
    // Returns the assigned sequence number, or 0 when the stream is
    // not open. Thread-safe.
    quint64 append(const QString &source, const QString &type, const QJsonObject &data = {});

    // Drain queued lines to disk and fsync-lite (QFile::flush).
    // Thread-safe; cheap when the buffer is empty.
    void flush();

    // Final flush + close. Further appends return 0.
    void close();

    // Milliseconds since open() on the monotonic clock. 0 when closed.
    qint64 elapsedMs() const;

    QString sessionId() const { return m_sessionId; }

    // Total events accepted so far (== last sequence number).
    quint64 eventCount() const;

  private:
    void writeBufferedLocked();

    mutable QMutex m_mutex;
    QFile m_file;
    QString m_sessionId;
    QElapsedTimer m_clock;
    QList<QByteArray> m_buffer;
    quint64 m_sequence = 0;
    bool m_open = false;
};

} // namespace trailer
