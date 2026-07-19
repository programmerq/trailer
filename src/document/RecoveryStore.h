#pragma once

#include <QHash>
#include <QString>

#include <optional>

namespace trailer {

// App-managed store of auto-save RECOVERY SIDECARS.
//
// Per the never-worry-save write-side invariant
// (docs/decision-records/2026-07-19-autosave-recovery-sidecar.md, amending
// ADR 0004): auto-save must never write the user's backing file. Instead it
// writes a recovery snapshot to a sidecar living under the app's data
// directory (QStandardPaths::AppDataLocation `/autosave/`), keyed by a hash
// of the backing file's absolute path. Sidecars live in app-data — NOT next
// to the user's file — so an external-file watcher never sees them and the
// user's directory is never touched.
//
// A small JSON index (`<baseDir>/index.json`) maps each backing path to its
// sidecar and the source file's modification time captured when the snapshot
// was written. That mtime lets reopen distinguish "our own unsaved work over
// an unchanged source" (restore it, silently, marked dirty) from "the source
// changed under us since the snapshot" (leave it alone).
//
// The store touches only the sidecar files and its own index — never the
// user's backing file.
class RecoveryStore {
  public:
    struct Entry {
        QString sidecarPath;
        // Source file mtime (ms since epoch) captured when the snapshot was
        // written. Used to detect an external change to the source.
        qint64 sourceMtimeMs = 0;
    };

    // Default base dir is QStandardPaths::AppDataLocation + "/autosave".
    // Tests pass an explicit temp dir to stay off the real profile.
    explicit RecoveryStore(QString baseDir = {});

    // Deterministic sidecar path for a backing file: `<baseDir>/<hash>.<ext>`
    // where <hash> is a hash of the absolute backing path and <ext> is the
    // backing file's suffix (so the recovery copy reopens through the right
    // adapter). Does not create the file.
    QString sidecarPathFor(const QString &backingPath) const;

    // Record that a fresh snapshot was written for `backingPath` at
    // `sidecarPath`; captures the source's current mtime. Persists the index.
    void recordSnapshot(const QString &backingPath, const QString &sidecarPath);

    std::optional<Entry> lookup(const QString &backingPath) const;

    // Delete the sidecar file (if any) and drop the index entry. Called on
    // explicit Save, on Discard, and on a clean close — anything that means
    // the recovery snapshot is no longer needed. Never touches the backing
    // file.
    void clear(const QString &backingPath);

    // Recovery decision for reopen: returns the sidecar path to load from iff
    // an index entry exists, its sidecar file exists, the source is unchanged
    // since the snapshot, and the sidecar is at least as new as the source.
    // Otherwise nullopt (nothing to recover, or the source changed under us).
    std::optional<QString> pendingRecovery(const QString &backingPath) const;

    QString baseDir() const { return m_baseDir; }

  private:
    static QString normalize(const QString &backingPath);
    void load();
    void persist() const;

    QString m_baseDir;
    QHash<QString, Entry> m_index; // key: normalized absolute backing path
};

} // namespace trailer
