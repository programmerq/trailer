#pragma once

#include <QWidget>

class QLabel;
class QPushButton;

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

  signals:
    // The user asked to open a file (clicked the "Open File…" button).
    void openRequested();
    // The user dropped one or more local files onto the surface.
    void filesDropped(const QStringList &paths);

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
    bool m_dragHighlight = false;
};

} // namespace trailer
