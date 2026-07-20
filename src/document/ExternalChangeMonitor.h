#pragma once

#include <QObject>
#include <QString>

class QFileSystemWatcher;
class QTimer;

namespace trailer {

// Watches a single document's file for external modification / deletion /
// atomic replacement and emits a debounced, typed signal when the on-disk
// copy may have changed. It deliberately does NOT decide whether a change is
// a real conflict — that pure decision lives in ExternalChangeState.h and is
// made by whoever holds the document's load-time baseline (the MainWindow
// slot). The monitor's only jobs are:
//
//   * watch the file AND its parent directory (Qt drops a file watch when the
//     file is renamed/replaced atomically — watching the directory lets us
//     notice the replacement and re-arm the file watch),
//   * debounce a burst of filesystem events into one signal,
//   * mute itself around our own saves so a self-write never self-triggers.
//
// See docs/decision-records/2026-07-19-external-file-change-handling.md.
class ExternalChangeMonitor : public QObject {
    Q_OBJECT

  public:
    explicit ExternalChangeMonitor(QObject *parent = nullptr);
    ~ExternalChangeMonitor() override;

    // Point the monitor at `path` (watching it and its parent directory).
    // An empty path clears all watches (used for untitled documents). Safe
    // to call repeatedly on document switch.
    void setPath(const QString &path);
    QString path() const { return m_path; }

    // Suppress emissions while our own save is writing the file. The save
    // path brackets its write with mute(true) / mute(false); any filesystem
    // events that arrive while muted are dropped rather than queued, because
    // the baseline is refreshed on save completion and would classify them as
    // NoChange anyway.
    void mute(bool muted) { m_muted = muted; }
    bool isMuted() const { return m_muted; }

    // Debounce window. Hand-tuned; see the constant in the .cpp.
    int debounceMs() const;
    void setDebounceMsForTest(int ms);

    // Test seam: drive the same code path a QFileSystemWatcher signal would,
    // without needing real filesystem-event timing. Re-arms the file watch
    // (if the path exists) and starts the debounce, exactly like a real event.
    void pokeForTest() { onFilesystemEvent(); }

  signals:
    // The watched file still exists but its directory/file entry changed —
    // the listener should re-stat and classify (NoChange / CleanExternalChange
    // / DirtyConflict) against its baseline.
    void externalChange();
    // The watched file no longer exists on disk.
    void fileDeleted();

  private slots:
    void onFilesystemEvent();
    void emitDebounced();

  private:
    void rearmFileWatch();

    QFileSystemWatcher *m_watcher = nullptr;
    QTimer *m_debounce = nullptr;
    QString m_path;
    QString m_dir;
    bool m_muted = false;
};

} // namespace trailer
