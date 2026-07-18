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
    // Start from a clean slate so a previous, larger session can't leave
    // orphan blobs behind. clear() removes the whole directory; we recreate
    // it and write the current session.
    clear();
    QDir().mkpath(m_dir);

    QJsonArray windowArray;
    int wIdx = 0;
    for (const SessionWindowDescriptor &win : windows) {
        QJsonArray docArray;
        int dIdx = 0;
        for (const SessionDocDescriptor &doc : win.docs) {
            QJsonObject docObj;
            if (doc.kind == SessionDocDescriptor::Kind::Draft) {
                const QString name = blobName(wIdx, dIdx, doc.format);
                QSaveFile blob(QDir::cleanPath(m_dir + QLatin1Char('/') + name));
                if (!blob.open(QIODevice::WriteOnly))
                    return false;
                if (blob.write(doc.bytes) != doc.bytes.size())
                    return false;
                if (!blob.commit())
                    return false;
                docObj[QStringLiteral("kind")] = QStringLiteral("draft");
                docObj[QStringLiteral("blob")] = name;
                docObj[QStringLiteral("format")] = doc.format;
                docObj[QStringLiteral("untitled")] = doc.untitled;
                docObj[QStringLiteral("originalPath")] = doc.originalPath;
                docObj[QStringLiteral("displayName")] = doc.displayName;
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

    QSaveFile manifest(manifestPath());
    if (!manifest.open(QIODevice::WriteOnly))
        return false;
    const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (manifest.write(json) != json.size())
        return false;
    return manifest.commit();
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
                dd.displayName = docObj.value(QStringLiteral("displayName")).toString();
                const QString name = docObj.value(QStringLiteral("blob")).toString();
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
