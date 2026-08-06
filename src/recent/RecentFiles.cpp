#include "RecentFiles.h"

#include "settings/AppPaths.h"
#include "util/PathKey.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace trailer {

namespace {

// "Same file?" is one rule, shared with the already-open dedup in
// Application::openFiles — see util/PathKey.h. This wrapper keeps the
// local call sites reading as before; the rule itself lives in one place
// so the Recent list and the open path can never drift apart on what
// counts as the same document.
QString canonicalize(const QString &path) { return canonicalPathKey(path); }

const char *zoomModeKey(ZoomMode m) {
    switch (m) {
    case ZoomMode::Custom:
        return "custom";
    case ZoomMode::FitInView:
        return "fit_in_view";
    case ZoomMode::FitToWidth:
        return "fit_to_width";
    case ZoomMode::Actual:
        return "actual";
    }
    return "custom";
}

ZoomMode zoomModeFromKey(const QString &key) {
    if (key == QLatin1String("fit_in_view"))
        return ZoomMode::FitInView;
    if (key == QLatin1String("fit_to_width"))
        return ZoomMode::FitToWidth;
    if (key == QLatin1String("actual"))
        return ZoomMode::Actual;
    return ZoomMode::Custom;
}

const char *sidebarModeKey(SidebarMode m) {
    switch (m) {
    case SidebarMode::Hidden:
        return "hidden";
    case SidebarMode::Pages:
        return "pages";
    case SidebarMode::SearchResults:
        return "search_results";
    case SidebarMode::TableOfContents:
        return "table_of_contents";
    case SidebarMode::HighlightsAndNotes:
        return "highlights_and_notes";
    }
    return "hidden";
}

SidebarMode sidebarModeFromKey(const QString &key) {
    if (key == QLatin1String("pages"))
        return SidebarMode::Pages;
    if (key == QLatin1String("search_results"))
        return SidebarMode::SearchResults;
    if (key == QLatin1String("table_of_contents"))
        return SidebarMode::TableOfContents;
    if (key == QLatin1String("highlights_and_notes"))
        return SidebarMode::HighlightsAndNotes;
    return SidebarMode::Hidden;
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
        // Workstream-I additions: zoomMode / sidebarMode / markupToolbarVisible /
        // window geometry+state. Older recent.json files without these keys
        // load with sensible defaults — sidebarMode falls back to the legacy
        // sidebar_visible bool (Hidden vs Pages) so existing installs keep
        // their last sidebar choice.
        if (obj.contains(QStringLiteral("zoom_mode"))) {
            entry.zoomMode = zoomModeFromKey(obj.value(QStringLiteral("zoom_mode")).toString());
        }
        if (obj.contains(QStringLiteral("sidebar_mode"))) {
            entry.sidebarMode =
                sidebarModeFromKey(obj.value(QStringLiteral("sidebar_mode")).toString());
        } else {
            // Legacy fallback: only "visible / hidden" was tracked. Map
            // visible→Pages because that's the only useful sidebar state
            // a pre-Workstream-I install could have had.
            entry.sidebarMode = entry.sidebarVisible ? SidebarMode::Pages : SidebarMode::Hidden;
        }
        entry.markupToolbarVisible =
            obj.value(QStringLiteral("markup_toolbar_visible")).toBool(false);
        const QString geom = obj.value(QStringLiteral("window_geometry")).toString();
        if (!geom.isEmpty()) {
            entry.windowGeometry = QByteArray::fromBase64(geom.toLatin1());
        }
        const QString state = obj.value(QStringLiteral("window_state")).toString();
        if (!state.isEmpty()) {
            entry.windowState = QByteArray::fromBase64(state.toLatin1());
        }
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
        // Derive sidebar_visible from sidebarMode so the legacy
        // key keeps a faithful value for any older reader.
        obj.insert(QStringLiteral("sidebar_visible"),
                   entry.sidebarMode != SidebarMode::Hidden);
        // Workstream-I additions. Always emit the enum keys so the
        // load path can disambiguate "set to default" vs "missing"
        // — but skip the geometry / state blobs when empty so the
        // JSON stays compact for unseen entries.
        obj.insert(QStringLiteral("zoom_mode"), QString::fromLatin1(zoomModeKey(entry.zoomMode)));
        obj.insert(QStringLiteral("sidebar_mode"),
                   QString::fromLatin1(sidebarModeKey(entry.sidebarMode)));
        obj.insert(QStringLiteral("markup_toolbar_visible"), entry.markupToolbarVisible);
        if (!entry.windowGeometry.isEmpty()) {
            obj.insert(QStringLiteral("window_geometry"),
                       QString::fromLatin1(entry.windowGeometry.toBase64()));
        }
        if (!entry.windowState.isEmpty()) {
            obj.insert(QStringLiteral("window_state"),
                       QString::fromLatin1(entry.windowState.toBase64()));
        }
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

void RecentFiles::updateViewState(const QString &path, const RecentEntry &state) {
    if (path.isEmpty())
        return;
    const QString canonical = canonicalize(path);
    for (RecentEntry &entry : m_entries) {
        if (canonicalize(entry.path) == canonical) {
            // Copy only the view-state subset of fields. path /
            // displayName / openedAt belong to the bookkeeping pass
            // that ran when the file was opened — preserve them.
            entry.currentPage = state.currentPage;
            entry.zoomFactor = state.zoomFactor;
            entry.scrollY = state.scrollY;
            entry.sidebarVisible = state.sidebarMode != SidebarMode::Hidden;
            entry.zoomMode = state.zoomMode;
            entry.sidebarMode = state.sidebarMode;
            entry.markupToolbarVisible = state.markupToolbarVisible;
            entry.windowGeometry = state.windowGeometry;
            entry.windowState = state.windowState;
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

QList<RecentEntry> RecentFiles::existingEntries(int limit) const {
    QList<RecentEntry> result;
    if (limit <= 0)
        return result;
    for (const RecentEntry &entry : m_entries) {
        if (result.size() >= limit)
            break;
        if (QFileInfo::exists(entry.path)) {
            result.append(entry);
        }
    }
    return result;
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
