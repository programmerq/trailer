#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

namespace trailer {

struct RecentEntry {
    QString path;
    QString displayName;
    QDateTime openedAt;
};

class RecentFiles {
public:
    RecentFiles();
    explicit RecentFiles(QString filePath);

    void load();
    void save() const;

    void add(const QString& path);
    void clear();

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

}  // namespace trailer
