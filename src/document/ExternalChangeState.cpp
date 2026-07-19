#include "document/ExternalChangeState.h"

#include <QFileInfo>

namespace trailer {

FileBaseline FileBaseline::fromPath(const QString &path) {
    FileBaseline b;
    if (path.isEmpty())
        return b;
    QFileInfo fi(path);
    if (!fi.exists())
        return b;
    b.valid = true;
    b.mtimeMs = fi.lastModified().toMSecsSinceEpoch();
    b.size = fi.size();
    return b;
}

ExternalChangeState classifyExternalChange(const FileBaseline &baseline, bool fileExists,
                                           qint64 curMtimeMs, qint64 curSize, bool dirty) {
    // No baseline to compare against (untitled / never-saved doc): there is
    // nothing on disk we are responsible for, so nothing to reconcile.
    if (!baseline.valid)
        return ExternalChangeState::NoChange;

    // The file we baselined is gone. Keep the buffer; Save recreates it.
    if (!fileExists)
        return ExternalChangeState::Deleted;

    // mtime is the primary signal; size is a cheap secondary that catches a
    // same-second overwrite whose mtime rounds to the baselined value on a
    // 1s-granularity filesystem.
    const bool changed = (curMtimeMs != baseline.mtimeMs) || (curSize != baseline.size);
    if (!changed)
        return ExternalChangeState::NoChange;

    return dirty ? ExternalChangeState::DirtyConflict : ExternalChangeState::CleanExternalChange;
}

ExternalChangeState classifyExternalChangeFor(const FileBaseline &baseline, const QString &path,
                                              bool dirty) {
    QFileInfo fi(path);
    const bool exists = !path.isEmpty() && fi.exists();
    const qint64 mtimeMs = exists ? fi.lastModified().toMSecsSinceEpoch() : 0;
    const qint64 size = exists ? fi.size() : -1;
    return classifyExternalChange(baseline, exists, mtimeMs, size, dirty);
}

} // namespace trailer
