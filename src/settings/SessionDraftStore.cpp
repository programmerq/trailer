#include "SessionDraftStore.h"

#include "AppPaths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace trailer {

namespace {

// Manifest schema version. Bump on any incompatible change to the layout
// so a store written by an older/newer build is rejected cleanly (restore
// returns empty) rather than mis-parsed — the honest-restore posture the
// decision record demands (better to not restore than to restore wrong).
constexpr int kManifestVersion = 1;

QString blobName(int windowIndex, int docIndex, const QString &format) {
    const QString ext = format.isEmpty() ? QStringLiteral("bin") : format;
    return QStringLiteral("blob-%1-%2.%3").arg(windowIndex).arg(docIndex).arg(ext);
}

} // namespace

SessionDraftStore::SessionDraftStore() : m_dir(AppPaths::sessionDraftsDir()) {}

SessionDraftStore::SessionDraftStore(QString storeDir) : m_dir(std::move(storeDir)) {}

QString SessionDraftStore::manifestPath() const {
    return QDir::cleanPath(m_dir + QLatin1String("/manifest.json"));
}

bool SessionDraftStore::save(const QList<SessionWindowDescriptor> &windows) const {
    // Atomic save (never wipe a valid prior session for a failed new one):
    // build the entire new session in a sibling STAGING directory and swap
    // it into place only after every blob + the manifest has been written
    // and committed. The live directory is untouched until that swap, so a
    // failure at any point here leaves the previous session fully intact.
    // (Contrast the old behaviour, which clear()ed the live store FIRST and
    // so destroyed a good session whenever the new save then failed.)
    const QString staging = m_dir + QLatin1String(".staging");
    const QString backup = m_dir + QLatin1String(".backup");
    QDir stagingDir(staging);
    if (stagingDir.exists())
        stagingDir.removeRecursively();
    // Settle any stale .backup left by a prior interrupted swap. If the live
    // dir is ALSO missing, that .backup is the sole surviving copy of the
    // previous session (a swap that got as far as live->.backup but then
    // failed to publish AND failed to restore) — recover it back to live
    // rather than destroy it. Only when a live session already exists is the
    // stale .backup genuinely redundant and safe to remove.
    QDir backupDir(backup);
    if (backupDir.exists()) {
        if (!QDir(m_dir).exists())
            QDir().rename(backup, m_dir); // promote the orphaned prior session
        else
            backupDir.removeRecursively();
    }
    if (!QDir().mkpath(staging))
        return false; // e.g. parent unwritable / a file sits where a dir must go

    auto bail = [&]() -> bool {
        QDir(staging).removeRecursively();
        return false;
    };

    QJsonArray windowArray;
    int wIdx = 0;
    for (const SessionWindowDescriptor &win : windows) {
        QJsonArray docArray;
        int dIdx = 0;
        for (const SessionDocDescriptor &doc : win.docs) {
            QJsonObject docObj;
            if (doc.kind == SessionDocDescriptor::Kind::Draft) {
                const QString name = blobName(wIdx, dIdx, doc.format);
                QSaveFile blob(QDir::cleanPath(staging + QLatin1Char('/') + name));
                if (!blob.open(QIODevice::WriteOnly))
                    return bail();
                if (blob.write(doc.bytes) != doc.bytes.size())
                    return bail();
                if (!blob.commit())
                    return bail();
                docObj[QStringLiteral("kind")] = QStringLiteral("draft");
                docObj[QStringLiteral("blob")] = name;
                docObj[QStringLiteral("format")] = doc.format;
                docObj[QStringLiteral("untitled")] = doc.untitled;
                docObj[QStringLiteral("originalPath")] = doc.originalPath;
                docObj[QStringLiteral("dpr")] = doc.devicePixelRatio;
                docObj[QStringLiteral("captureOrigin")] = doc.captureOrigin;
            } else {
                docObj[QStringLiteral("kind")] = QStringLiteral("path");
                docObj[QStringLiteral("path")] = doc.path;
            }
            docArray.append(docObj);
            ++dIdx;
        }
        QJsonObject winObj;
        winObj[QStringLiteral("docs")] = docArray;
        windowArray.append(winObj);
        ++wIdx;
    }

    QJsonObject root;
    root[QStringLiteral("version")] = kManifestVersion;
    root[QStringLiteral("windows")] = windowArray;

    QSaveFile manifest(QDir::cleanPath(staging + QLatin1String("/manifest.json")));
    if (!manifest.open(QIODevice::WriteOnly))
        return bail();
    const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (manifest.write(json) != json.size())
        return bail();
    if (!manifest.commit())
        return bail();

    // Staging is fully written and validated on disk. Promote it via a
    // two-rename swap so the live session is never absent from disk between
    // two operations that could each fail:
    //   1. rename live -> .backup   (preserve the prior session)
    //   2. rename staging -> live   (publish the new session)
    //   3. delete .backup           (only after step 2 succeeds)
    // If step 2 fails, restore .backup -> live so the prior session survives
    // intact. (Contrast the old removeRecursively(live)-then-rename, whose
    // crash/failure gap could leave BOTH the old and new sessions gone.)
    QDir liveDir(m_dir);
    const bool hadLive = liveDir.exists();
    if (hadLive && !QDir().rename(m_dir, backup))
        return bail();
    if (!QDir().rename(staging, m_dir)) {
        // Publish failed; put the prior session back where it was.
        if (hadLive)
            QDir().rename(backup, m_dir);
        return bail();
    }
    if (hadLive)
        QDir(backup).removeRecursively();
    return true;
}

QList<SessionWindowDescriptor> SessionDraftStore::restore() const {
    QList<SessionWindowDescriptor> out;

    QFile manifest(manifestPath());
    if (!manifest.open(QIODevice::ReadOnly))
        return out;
    const QByteArray json = manifest.readAll();
    manifest.close();

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return out;
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("version")).toInt(-1) != kManifestVersion)
        return out;

    const QJsonArray windowArray = root.value(QStringLiteral("windows")).toArray();
    for (const QJsonValue &winVal : windowArray) {
        const QJsonObject winObj = winVal.toObject();
        SessionWindowDescriptor win;
        const QJsonArray docArray = winObj.value(QStringLiteral("docs")).toArray();
        for (const QJsonValue &docVal : docArray) {
            const QJsonObject docObj = docVal.toObject();
            SessionDocDescriptor dd;
            const QString kind = docObj.value(QStringLiteral("kind")).toString();
            if (kind == QLatin1String("draft")) {
                dd.kind = SessionDocDescriptor::Kind::Draft;
                dd.format = docObj.value(QStringLiteral("format")).toString(QStringLiteral("png"));
                dd.untitled = docObj.value(QStringLiteral("untitled")).toBool();
                dd.originalPath = docObj.value(QStringLiteral("originalPath")).toString();
                dd.devicePixelRatio = docObj.value(QStringLiteral("dpr")).toDouble(1.0);
                dd.captureOrigin = docObj.value(QStringLiteral("captureOrigin")).toBool();
                // Basename-only guard: the blob name comes from JSON that a
                // future/foreign build (or a tampered store) could seed with a
                // traversal payload like "../../etc/passwd". Strip any path
                // components so the join can only ever address a file directly
                // inside the store dir.
                const QString name =
                    QFileInfo(docObj.value(QStringLiteral("blob")).toString()).fileName();
                if (name.isEmpty())
                    continue; // no addressable blob → drop just this doc
                QFile blob(QDir::cleanPath(m_dir + QLatin1Char('/') + name));
                if (!blob.open(QIODevice::ReadOnly))
                    continue; // a missing blob drops just that doc, not the session
                dd.bytes = blob.readAll();
                blob.close();
            } else {
                dd.kind = SessionDocDescriptor::Kind::Path;
                dd.path = docObj.value(QStringLiteral("path")).toString();
            }
            win.docs.append(dd);
        }
        out.append(win);
    }
    return out;
}

bool SessionDraftStore::hasSession() const {
    QFile manifest(manifestPath());
    if (!manifest.open(QIODevice::ReadOnly))
        return false;
    const QByteArray json = manifest.readAll();
    manifest.close();
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("version")).toInt(-1) != kManifestVersion)
        return false;
    return !root.value(QStringLiteral("windows")).toArray().isEmpty();
}

void SessionDraftStore::clear() const {
    QDir dir(m_dir);
    if (dir.exists())
        dir.removeRecursively();
}

} // namespace trailer
