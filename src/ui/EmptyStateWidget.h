#pragma once

#include "recent/RecentFiles.h"

#include <QList>
#include <QWidget>

class QLabel;
class QPushButton;
class QVBoxLayout;

namespace trailer {

// Empty-state / first-run surface shown in a MainWindow that holds no
// document. Win/Linux keep a window alive when the user closes the last
// document (rather than quitting), so that window needs a welcoming,
// self-explanatory "there's nothing open yet, here's how to start"
// prompt rather than a blank void that reads as a crash.
//
// The whole widget is a drag target: dropping local files onto it emits
// filesDropped() so the host can route them through Application::openFiles.
// The "Open File…" button emits openRequested() so the host can reuse its
// existing File → Open flow.
//
// Below the button an optional inline "Recent" list surfaces the most
// recent files (macOS Preview's welcome behaviour: a plain vertical list
// of names, not a thumbnail grid). Clicking an entry emits
// openRecentRequested(path) so the host opens it through the same flow as
// File → Open Recent. The section is hidden entirely when there are no
// recent files — no empty placeholder, per the no-lying-controls /
// no-empty-affordance philosophy.
class EmptyStateWidget : public QWidget {
    Q_OBJECT

  public:
    explicit EmptyStateWidget(QWidget *parent = nullptr);

    // True while a drag with file URLs is hovering over the surface.
    // Exposed for tests and for the host to reason about the current
    // visual state.
    bool isDragHighlighted() const { return m_dragHighlight; }
    // Force the drag-highlight state. Used by the screenshot tool to
    // capture the highlighted look without synthesizing a live drag,
    // and available to tests.
    void setDragHighlighted(bool highlighted);

    // Populate (or refresh) the inline recent list from the RecentFiles
    // model. Shows at most kMaxRecentShown entries, most-recent-first
    // (entries arrive already sorted / trimmed from RecentFiles). Passing
    // an empty list hides the whole section. Idempotent: safe to call on
    // every empty-state show and whenever recents change.
    void setRecentEntries(const QList<RecentEntry> &entries);

    // Number of recent entries currently rendered in the inline list.
    // Exposed for tests.
    int recentEntryCount() const;
    // True while the inline recent section is shown (i.e. there is at
    // least one recent entry). Exposed for tests.
    bool isRecentSectionVisible() const;

    // Upper bound on how many recent entries the inline list renders,
    // regardless of how many the model holds. Preview keeps the welcome
    // list short; 8 is enough to be useful without turning the welcome
    // surface into a file browser. Raise if users ask for a longer list;
    // lower if it crowds the centred welcome block on small windows.
    static constexpr int kMaxRecentShown = 8;

  signals:
    // The user asked to open a file (clicked the "Open File…" button).
    void openRequested();
    // The user dropped one or more local files onto the surface.
    void filesDropped(const QStringList &paths);
    // The user clicked a recent entry; `path` is that entry's file path.
    void openRecentRequested(const QString &path);

  protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    // Draw the subtle dashed rounded-rect drop-target border, tinted
    // with the palette highlight colour while a drag hovers.
    void paintEvent(QPaintEvent *event) override;

  private:
    QLabel *m_iconLabel = nullptr;
    QLabel *m_headline = nullptr;
    QLabel *m_subtitle = nullptr;
    QPushButton *m_openButton = nullptr;
    // Container for the inline recent list (heading + entry buttons).
    // Hidden whenever there are no recent entries.
    QWidget *m_recentSection = nullptr;
    QVBoxLayout *m_recentEntriesLayout = nullptr;
    bool m_dragHighlight = false;
};

} // namespace trailer
