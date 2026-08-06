#pragma once

#include <QFileInfo>
#include <QString>

namespace trailer {

// One canonical on-disk identity for a file path — the single rule the
// app uses to decide whether two paths name the same file.
//
//   * canonicalFilePath() resolves symlinks, "..", and relative paths, so
//     /docs/link.pdf and /docs/real.pdf collapse to one key.
//   * It returns EMPTY for a path that does not exist. Falling back to
//     absoluteFilePath() keeps a not-yet-created (or just-deleted) path
//     distinct from every other missing path — without the fallback, every
//     missing file would key as "" and compare equal to every other one.
//   * An empty input stays empty. Callers MUST treat an empty key as "no
//     identity" and never match it against anything, including another
//     empty key (see Application::windowForOpenPath and RecentFiles::add).
//
// Known limitation, deliberately not papered over: this does not case-fold.
// On a case-insensitive volume (APFS default, NTFS) "A.pdf" and "a.pdf" are
// the same file but produce different keys, so they are treated as two
// documents. Case-folding here would be worse — it would merge two genuinely
// distinct files on a case-sensitive Linux volume, and every real entry
// point (Finder, Spotlight, the Open panel, argv, drag-and-drop, Recent
// Files) hands over the on-disk spelling already. Revisit only with a
// reported case where a user reaches the same file under two spellings.
inline QString canonicalPathKey(const QString &path) {
    if (path.isEmpty())
        return QString();
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

} // namespace trailer
