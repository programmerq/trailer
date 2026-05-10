#include "SignatureStore.h"

#include "settings/AppPaths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <algorithm>

namespace trailer {

namespace {

QString jsonPathFor(const QString &pngPath) {
    QFileInfo fi(pngPath);
    return fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() + QStringLiteral(".json");
}

} // namespace

SignatureStore::SignatureStore() : SignatureStore(AppPaths::signaturesDir()) {}

SignatureStore::SignatureStore(QString dir) : m_dir(std::move(dir)) {}

std::vector<Signature> SignatureStore::loadAll() const {
    std::vector<Signature> out;
    QDir dir(m_dir);
    if (!dir.exists())
        return out;

    const QFileInfoList entries =
        dir.entryInfoList({QStringLiteral("*.png")}, QDir::Files | QDir::NoSymLinks, QDir::Time);

    for (const QFileInfo &fi : entries) {
        Signature s;
        s.id = fi.completeBaseName();
        s.pngPath = fi.absoluteFilePath();

        // Try the JSON sidecar first; fall back to file mtime / stem.
        const QString jsonPath = jsonPathFor(s.pngPath);
        QFile jf(jsonPath);
        if (jf.exists() && jf.open(QIODevice::ReadOnly)) {
            const QJsonDocument doc = QJsonDocument::fromJson(jf.readAll());
            jf.close();
            if (doc.isObject()) {
                const QJsonObject obj = doc.object();
                s.label = obj.value(QStringLiteral("label")).toString();
                s.altText = obj.value(QStringLiteral("alt_text")).toString();
                const QString created = obj.value(QStringLiteral("created")).toString();
                if (!created.isEmpty()) {
                    s.createdAt = QDateTime::fromString(created, Qt::ISODate);
                }
            }
        }
        if (s.label.isEmpty())
            s.label = s.id;
        if (!s.createdAt.isValid())
            s.createdAt = fi.lastModified();

        out.push_back(std::move(s));
    }

    // Sort newest-first for the picker.
    std::sort(out.begin(), out.end(),
              [](const Signature &a, const Signature &b) { return a.createdAt > b.createdAt; });
    return out;
}

Signature SignatureStore::add(const QImage &image, const QString &label, const QString &altText) {
    Signature out;
    if (image.isNull())
        return out;

    AppPaths::ensureDirExists(m_dir);

    // id = sig_YYYYMMDDhhmmsszzz_NNN — sortable and collision-free.
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QString stamp = now.toString(QStringLiteral("yyyyMMddHHmmsszzz"));
    const QString id = QStringLiteral("sig_%1_%2").arg(stamp).arg(++m_seq, 3, 10, QLatin1Char('0'));
    const QString pngPath = m_dir + QLatin1Char('/') + id + QStringLiteral(".png");

    QImage rgba = image.convertToFormat(QImage::Format_ARGB32);
    if (!rgba.save(pngPath, "PNG"))
        return out;

    // Sidecar JSON.
    QJsonObject obj;
    obj.insert(QStringLiteral("label"), label);
    obj.insert(QStringLiteral("created"), now.toString(Qt::ISODate));
    obj.insert(QStringLiteral("alt_text"), altText);
    const QString jsonPath = jsonPathFor(pngPath);
    QSaveFile jf(jsonPath);
    if (jf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        jf.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
        jf.commit();
    }

    out.id = id;
    out.label = label.isEmpty() ? id : label;
    out.altText = altText;
    out.createdAt = now;
    out.pngPath = pngPath;
    return out;
}

bool SignatureStore::remove(const QString &id) {
    if (id.isEmpty())
        return false;
    const QString pngPath = m_dir + QLatin1Char('/') + id + QStringLiteral(".png");
    const QString jsonPath = m_dir + QLatin1Char('/') + id + QStringLiteral(".json");
    bool removed = false;
    removed |= QFile::remove(pngPath);
    removed |= QFile::remove(jsonPath);
    return removed;
}

} // namespace trailer
