#include "document/ExternalChangeMonitor.h"

#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QTimer>

namespace trailer {

namespace {
// PHILOSOPHY: hand-tuned values stay hand-tuned. Debounce window for
// collapsing a burst of filesystem events (an editor that writes via
// truncate-then-append, or an atomic temp+rename, can fire several
// directory/file events in quick succession) into a single classification.
// Range considered: 100–500ms. Below ~150ms a multi-event save still slips
// through as two signals (double reload); above ~350ms a genuine external
// change feels laggy to notice. 250ms collapses the storm without a
// perceptible delay. Raise it if reloads still double-fire on some editor's
// save pattern; lower it if the reload feels sluggish.
constexpr int kDebounceMs = 250;
} // namespace

ExternalChangeMonitor::ExternalChangeMonitor(QObject *parent) : QObject(parent) {
    m_watcher = new QFileSystemWatcher(this);
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(kDebounceMs);
    connect(m_debounce, &QTimer::timeout, this, &ExternalChangeMonitor::emitDebounced);
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this,
            &ExternalChangeMonitor::onFilesystemEvent);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this,
            &ExternalChangeMonitor::onFilesystemEvent);
}

ExternalChangeMonitor::~ExternalChangeMonitor() = default;

int ExternalChangeMonitor::debounceMs() const { return m_debounce->interval(); }

void ExternalChangeMonitor::setDebounceMsForTest(int ms) { m_debounce->setInterval(ms); }

void ExternalChangeMonitor::setPath(const QString &path) {
    // Clear whatever we were watching before.
    const QStringList watched = m_watcher->files() + m_watcher->directories();
    if (!watched.isEmpty())
        m_watcher->removePaths(watched);
    m_debounce->stop();

    m_path = path;
    m_dir.clear();
    if (m_path.isEmpty())
        return;

    QFileInfo fi(m_path);
    m_dir = fi.absolutePath();
    // Watch the parent directory so an atomic replace (temp + rename) — which
    // drops the file watch because the inode changes — still notifies us.
    if (!m_dir.isEmpty())
        m_watcher->addPath(m_dir);
    if (fi.exists())
        m_watcher->addPath(m_path);
}

void ExternalChangeMonitor::rearmFileWatch() {
    if (m_path.isEmpty())
        return;
    // After an atomic rename/replace Qt silently drops the file from its watch
    // list even though a file exists at the path again. Re-add it so the next
    // in-place edit is caught.
    if (QFileInfo::exists(m_path) && !m_watcher->files().contains(m_path))
        m_watcher->addPath(m_path);
}

void ExternalChangeMonitor::onFilesystemEvent() {
    if (m_muted)
        return;
    rearmFileWatch();
    // (Re)start the debounce; a burst of events collapses into one emit.
    m_debounce->start();
}

void ExternalChangeMonitor::emitDebounced() {
    if (m_muted)
        return;
    // Re-arm once more in case the file reappeared between the last event and
    // this timeout (atomic replace lands the new file slightly after the
    // directoryChanged fires).
    rearmFileWatch();
    if (m_path.isEmpty())
        return;
    if (QFileInfo::exists(m_path))
        emit externalChange();
    else
        emit fileDeleted();
}

} // namespace trailer
