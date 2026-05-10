#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

namespace trailer {

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
    bool sidebarVisible = true;
};

class RecentFiles {
  public:
    RecentFiles();
    explicit RecentFiles(QString filePath);

    void load();
    void save() const;

    void add(const QString &path);
    void clear();

    // Update the view-state fields of the entry whose canonical path
    // matches `path`. No-op if the entry doesn't exist (the user
    // closed a file that was never in the list, e.g. a temp scratch
    // doc). Caller is responsible for invoking save() to persist.
    void updateViewState(const QString &path, int currentPage, double zoomFactor, int scrollY,
                         bool sidebarVisible);

    // Look up the captured view-state for `path` (canonical match).
    // Returns a default-constructed entry (path empty) if no match.
    RecentEntry findByPath(const QString &path) const;

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
