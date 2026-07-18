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
    QDir stagingDir(staging);
    if (stagingDir.exists())
        stagingDir.removeRecursively();
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

    // Staging is fully written and validated on disk. Only NOW retire the
    // previous live session and promote staging into its place.
    QDir liveDir(m_dir);
    if (liveDir.exists() && !liveDir.removeRecursively())
        return bail();
    if (!QDir().rename(staging, m_dir))
        return bail();
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
