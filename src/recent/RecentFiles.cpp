#include "RecentFiles.h"

#include "settings/AppPaths.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace trailer {

namespace {

QString canonicalize(const QString &path) {
    const QString canonical = QFileInfo(path).canonicalFilePath();
    return canonical.isEmpty() ? QFileInfo(path).absoluteFilePath() : canonical;
}

} // namespace

RecentFiles::RecentFiles() : RecentFiles(AppPaths::recentFile()) {}

RecentFiles::RecentFiles(QString filePath) : m_filePath(std::move(filePath)) {}

void RecentFiles::load() {
    m_entries.clear();

    QFile file(m_filePath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QByteArray payload = file.readAll();
    file.close();

    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &error);
    if (error.error != QJsonParseError::NoError || !doc.isArray()) {
        return;
    }

    for (const QJsonValue &value : doc.array()) {
        const QJsonObject obj = value.toObject();
        RecentEntry entry;
        entry.path = obj.value(QStringLiteral("path")).toString();
        entry.displayName = obj.value(QStringLiteral("display_name")).toString();
        entry.openedAt =
            QDateTime::fromString(obj.value(QStringLiteral("opened_at")).toString(), Qt::ISODate);
        // View-state fields are additive — old recent.json files load
        // with the defaulted -1 / 0.0 / true values and behave as if
        // no state was captured.
        entry.currentPage = obj.value(QStringLiteral("current_page")).toInt(-1);
        entry.zoomFactor = obj.value(QStringLiteral("zoom_factor")).toDouble(0.0);
        entry.scrollY = obj.value(QStringLiteral("scroll_y")).toInt(0);
        entry.sidebarVisible = obj.value(QStringLiteral("sidebar_visible")).toBool(true);
        if (!entry.path.isEmpty()) {
            m_entries.append(entry);
        }
    }
}

void RecentFiles::save() const {
    QJsonArray arr;
    for (const RecentEntry &entry : m_entries) {
        QJsonObject obj;
        obj.insert(QStringLiteral("path"), entry.path);
        obj.insert(QStringLiteral("display_name"), entry.displayName);
        obj.insert(QStringLiteral("opened_at"), entry.openedAt.toString(Qt::ISODate));
        // Skip emitting unset view-state fields so the JSON stays
        // small for entries the user has never re-opened. Sidebar
        // state is the exception: false is meaningful (user
        // explicitly hid the sidebar) and the default (true) is
        // also a real value, so always emit.
        if (entry.currentPage >= 0) {
            obj.insert(QStringLiteral("current_page"), entry.currentPage);
        }
        if (entry.zoomFactor > 0.0) {
            obj.insert(QStringLiteral("zoom_factor"), entry.zoomFactor);
        }
        if (entry.scrollY != 0) {
            obj.insert(QStringLiteral("scroll_y"), entry.scrollY);
        }
        obj.insert(QStringLiteral("sidebar_visible"), entry.sidebarVisible);
        arr.append(obj);
    }

    AppPaths::ensureDirExists(QFileInfo(m_filePath).absolutePath());

    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }
    file.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    file.close();
}

void RecentFiles::add(const QString &path) {
    if (path.isEmpty()) {
        return;
    }
    const QString canonical = canonicalize(path);

    m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(),
                                   [&canonical](const RecentEntry &e) {
                                       return canonicalize(e.path) == canonical;
                                   }),
                    m_entries.end());

    RecentEntry entry;
    entry.path = canonical;
    entry.displayName = QFileInfo(canonical).fileName();
    entry.openedAt = QDateTime::currentDateTimeUtc();
    m_entries.prepend(entry);
    trim();
}

void RecentFiles::clear() {
    m_entries.clear();
}

void RecentFiles::updateViewState(const QString &path, int currentPage, double zoomFactor,
                                  int scrollY, bool sidebarVisible) {
    if (path.isEmpty())
        return;
    const QString canonical = canonicalize(path);
    for (RecentEntry &entry : m_entries) {
        if (canonicalize(entry.path) == canonical) {
            entry.currentPage = currentPage;
            entry.zoomFactor = zoomFactor;
            entry.scrollY = scrollY;
            entry.sidebarVisible = sidebarVisible;
            return;
        }
    }
}

RecentEntry RecentFiles::findByPath(const QString &path) const {
    if (path.isEmpty())
        return {};
    const QString canonical = canonicalize(path);
    for (const RecentEntry &entry : m_entries) {
        if (canonicalize(entry.path) == canonical) {
            return entry;
        }
    }
    return {};
}

void RecentFiles::setMaxEntries(int value) {
    m_maxEntries = value > 0 ? value : 1;
    trim();
}

void RecentFiles::trim() {
    while (m_entries.size() > m_maxEntries) {
        m_entries.removeLast();
    }
}

} // namespace trailer
