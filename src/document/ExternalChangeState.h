#pragma once

#include <QString>

#include <cstdint>

namespace trailer {

// How the on-disk copy of an open document relates to what we last read
// (or last wrote). Computed by the pure classifier below from a load-time
// baseline, the current on-disk state, and whether the in-memory buffer
// has unsaved edits. Deliberately UI-free so it is unit-testable without
// a widget or an event loop — the whole "is this a conflict?" decision
// lives here as a free function, and the monitor / save-guard both consult
// it. See docs/decision-records/2026-07-19-external-file-change-handling.md.
enum class ExternalChangeState {
    // On-disk mtime + size match the baseline (or there is no baseline to
    // compare against, e.g. an untitled doc) — nothing to do.
    NoChange,
    // The file changed on disk and our buffer is clean, so we can silently
    // reload from disk (Preview-style) with nothing to lose.
    CleanExternalChange,
    // The file changed on disk AND our buffer has unsaved edits — a genuine
    // conflict we must never auto-resolve. Surface the banner.
    DirtyConflict,
    // The file no longer exists on disk. Keep the buffer; Save recreates it.
    Deleted,
};

// Snapshot of a file's identity captured at load time (and refreshed after
// each successful save so our own writes never look like an external
// change). mtime is the primary signal; size is a cheap secondary signal
// that catches same-second overwrites the 1s-granularity mtime can miss on
// some filesystems.
struct FileBaseline {
    bool valid = false;
    // QFileInfo::lastModified().toMSecsSinceEpoch(); 0 when invalid.
    qint64 mtimeMs = 0;
    // File size in bytes; -1 when invalid.
    qint64 size = -1;

    // Stat `path` and build a baseline. Returns an invalid baseline for an
    // empty path or a file that does not exist (untitled / never-saved doc).
    static FileBaseline fromPath(const QString &path);
};

// Pure decision — no filesystem access, no Qt-UI deps. This is the key
// testability lever: feed it a baseline, the current on-disk mtime/size,
// whether the file currently exists, and the dirty flag, and it returns the
// classification. `curMtimeMs` / `curSize` are ignored when `fileExists`
// is false.
ExternalChangeState classifyExternalChange(const FileBaseline &baseline, bool fileExists,
                                           qint64 curMtimeMs, qint64 curSize, bool dirty);

// Convenience wrapper that stats `path` and classifies. Not pure (touches
// the filesystem) — the overload above is the unit-test seam.
ExternalChangeState classifyExternalChangeFor(const FileBaseline &baseline, const QString &path,
                                              bool dirty);

} // namespace trailer
