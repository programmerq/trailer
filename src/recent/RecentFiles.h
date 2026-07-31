#pragma once

#include "document/IDocument.h"

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>

namespace trailer {

// Mirrors Sidebar::Mode by name + ordinal. Lives in this header so
// recent/ doesn't have to pull in ui/Sidebar.h (which transitively
// drags in QtWidgets); ui/Sidebar.cpp converts between the two with
// a single static_assert-guarded cast. Keep the order in sync with
// Sidebar::Mode.
enum class SidebarMode : int {
    Hidden = 0,
    Pages = 1,
    SearchResults = 2,
    TableOfContents = 3,
    HighlightsAndNotes = 4,
};

struct RecentEntry {
    QString path;
    QString displayName;
    QDateTime openedAt;
    // View state at the time the user last closed (or saved) this
    // file. Restored on reopen so a 200-page document picks up where
    // the user left off. -1 / 0.0 sentinel values mean "not yet
    // captured" — the open path leaves the document at its natural
    // defaults in that case.
    int currentPage = -1;
    double zoomFactor = 0.0;
    int scrollY = 0;
    // Legacy: persisted for back-compat with pre-Workstream-I
    // recent.json files. The new sidebarMode field below is the
    // source of truth; sidebarVisible is derived from it on save
    // (false ↔ Hidden, true ↔ anything else) and only consulted on
    // load when sidebarMode is absent.
    bool sidebarVisible = true;
    // Workstream I additions. Defaults represent "no captured state"
    // so an entry written by an older Trailer still loads cleanly.
    ZoomMode zoomMode = ZoomMode::Custom;
    SidebarMode sidebarMode = SidebarMode::Hidden;
    bool markupToolbarVisible = false;
    // Qt saveGeometry() / saveState() blobs. Empty means "not yet
    // captured" — the window keeps its constructor-time geometry and
    // dock layout. Stored base64-encoded in recent.json so the JSON
    // remains human-inspectable.
    QByteArray windowGeometry;
    QByteArray windowState;

    // True when the entry carries any captured view-state. Used by
    // the restore path to decide whether to apply per-file state or
    // fall through to per-type defaults / hardcoded defaults.
    bool hasViewState() const {
        return currentPage >= 0 || zoomFactor > 0.0 || scrollY != 0 ||
               zoomMode != ZoomMode::Custom || markupToolbarVisible ||
               !windowGeometry.isEmpty() || !windowState.isEmpty();
    }
};

class RecentFiles {
  public:
    RecentFiles();
    explicit RecentFiles(QString filePath);

    void load();
    void save() const;

    void add(const QString &path);
    void clear();

    // Replace the view-state fields of the entry whose canonical
    // path matches `path`. No-op if the entry doesn't exist (the
    // user closed a file that was never in the list, e.g. a temp
    // scratch doc). Caller is responsible for invoking save() to
    // persist. `state` carries only the view-state subset of
    // RecentEntry — path / displayName / openedAt are untouched.
    void updateViewState(const QString &path, const RecentEntry &state);

    // Look up the captured view-state for `path` (canonical match).
    // Returns a default-constructed entry (path empty) if no match.
    RecentEntry findByPath(const QString &path) const;

    // Most-recent-first entries whose path currently exists on disk,
    // capped to `limit`. For native OS recents surfaces (the macOS Dock
    // icon menu / system Recent Documents list — see
    // src/platform/DockRecents.h) that can't grey out or tooltip a dead
    // entry the way Trailer's own File > Open Recent menu does (G3):
    // offering a path Trailer already knows is gone would be a lying
    // control in chrome Trailer doesn't render. Trailer's own in-app menu
    // deliberately keeps missing entries as-is (entries()) — opening one
    // surfaces a normal "file not found" error, which is an acceptable
    // popup for a genuinely missing file, not a disabled-control case.
    // Entries are already de-duplicated by add()'s own invariant, so no
    // further de-dup happens here. `limit <= 0` returns an empty list.
    QList<RecentEntry> existingEntries(int limit) const;

    QList<RecentEntry> entries() const { return m_entries; }

    int maxEntries() const { return m_maxEntries; }
    void setMaxEntries(int value);

    QString filePath() const { return m_filePath; }

  private:
    void trim();

    QString m_filePath;
    QList<RecentEntry> m_entries;
    int m_maxEntries = 50;
};

} // namespace trailer
