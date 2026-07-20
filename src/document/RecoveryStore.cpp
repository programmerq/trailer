#include "RecoveryStore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace trailer {

RecoveryStore::RecoveryStore(QString baseDir) : m_baseDir(std::move(baseDir)) {
    if (m_baseDir.isEmpty()) {
        const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        m_baseDir = QDir(appData).filePath(QStringLiteral("autosave"));
    }
    QDir().mkpath(m_baseDir);
    load();
}

QString RecoveryStore::normalize(const QString &backingPath) {
    const QFileInfo fi(backingPath);
    // absoluteFilePath (not canonical) so a not-yet-existing / just-deleted
    // source still maps to a stable key.
    const QString abs = fi.absoluteFilePath();
    return abs.isEmpty() ? backingPath : abs;
}

QString RecoveryStore::sidecarPathFor(const QString &backingPath) const {
    const QString key = normalize(backingPath);
    const QByteArray digest =
        QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex();
    QString suffix = QFileInfo(backingPath).suffix().toLower();
    if (suffix.isEmpty())
        suffix = QStringLiteral("bin");
    return QDir(m_baseDir).filePath(QString::fromLatin1(digest) + QLatin1Char('.') + suffix);
}

void RecoveryStore::recordSnapshot(const QString &backingPath, const QString &sidecarPath) {
    Entry e;
    e.sidecarPath = sidecarPath;
    const QFileInfo src(backingPath);
    e.sourceMtimeMs = src.exists() ? src.lastModified().toMSecsSinceEpoch() : 0;
    m_index.insert(normalize(backingPath), e);
    persist();
}

std::optional<RecoveryStore::Entry> RecoveryStore::lookup(const QString &backingPath) const {
    const auto it = m_index.constFind(normalize(backingPath));
    if (it == m_index.constEnd())
        return std::nullopt;
    return it.value();
}

void RecoveryStore::clear(const QString &backingPath) {
    const QString key = normalize(backingPath);
    const auto it = m_index.constFind(key);
    if (it != m_index.constEnd()) {
        if (!it.value().sidecarPath.isEmpty())
            QFile::remove(it.value().sidecarPath);
        m_index.remove(key);
        persist();
    }
    // Also remove a deterministic sidecar left behind without an index entry
    // (e.g. a crash between snapshot write and index persist).
    const QString det = sidecarPathFor(backingPath);
    if (QFile::exists(det))
        QFile::remove(det);
}

std::optional<QString> RecoveryStore::pendingRecovery(const QString &backingPath) const {
    const auto entry = lookup(backingPath);
    if (!entry)
        return std::nullopt;
    const QFileInfo sidecar(entry->sidecarPath);
    if (!sidecar.exists())
        return std::nullopt;
    const QFileInfo src(backingPath);
    if (!src.exists())
        return std::nullopt; // backing gone — don't guess a destination
    const qint64 curSrc = src.lastModified().toMSecsSinceEpoch();
    // Only auto-restore our own snapshot over a source we haven't been beaten
    // to: the source must be unchanged since the snapshot AND the sidecar at
    // least as new as the source. If the source changed under us (external
    // edit, or a save from elsewhere) the sidecar is stale — leave it alone.
    const qint64 sidecarMs = sidecar.lastModified().toMSecsSinceEpoch();
    if (entry->sourceMtimeMs == curSrc && sidecarMs >= curSrc)
        return entry->sidecarPath;
    return std::nullopt;
}

void RecoveryStore::load() {
    QFile f(QDir(m_baseDir).filePath(QStringLiteral("index.json")));
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject())
        return;
    const QJsonObject root = doc.object();
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        const QJsonObject o = it.value().toObject();
        Entry e;
        e.sidecarPath = o.value(QStringLiteral("sidecar")).toString();
        e.sourceMtimeMs =
            static_cast<qint64>(o.value(QStringLiteral("sourceMtimeMs")).toDouble());
        if (!e.sidecarPath.isEmpty())
            m_index.insert(it.key(), e);
    }
}

void RecoveryStore::persist() const {
    QJsonObject root;
    for (auto it = m_index.constBegin(); it != m_index.constEnd(); ++it) {
        QJsonObject o;
        o.insert(QStringLiteral("sidecar"), it.value().sidecarPath);
        o.insert(QStringLiteral("sourceMtimeMs"),
                 static_cast<double>(it.value().sourceMtimeMs));
        root.insert(it.key(), o);
    }
    QDir().mkpath(m_baseDir);
    QFile f(QDir(m_baseDir).filePath(QStringLiteral("index.json")));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

} // namespace trailer
