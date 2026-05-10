#pragma once

#include <QDockWidget>
#include <QTimer>

#include <vector>

class QListView;
class QListWidget;
class QStackedWidget;
class QTabWidget;
class QTreeView;

namespace trailer {

class IDocument;
class ThumbnailModel;

class Sidebar : public QDockWidget {
    Q_OBJECT

  public:
    // What the sidebar is currently presenting. The top-bar's
    // sidebar-mode picker drives this; modes the underlying feature
    // doesn't yet support (TOC requires a PDF outline reader,
    // HighlightsAndNotes requires text-aware highlights) are kept
    // as enum members so the picker can dim them rather than
    // hide them from the menu.
    enum class Mode {
        Hidden,
        Pages,             // page thumbnails for the active document
        SearchResults,     // page thumbnails filtered to query matches
        TableOfContents,   // PDF /Outlines tree (not yet implemented)
        HighlightsAndNotes // text-aware highlights + notes (deferred)
    };
    Q_ENUM(Mode)

    explicit Sidebar(QWidget *parent = nullptr);

    void setDocument(IDocument *doc);
    void refreshThumbnails();
    void refreshAnnotations();

    Mode mode() const { return m_mode; }
    // Number of items currently in the Highlights & Notes list.
    // MainWindow uses this to gate the picker entry's enabled state
    // — empty list = greyed-out menu entry.
    int highlightsAndNotesCount() const;
    // Switch presentation. Hidden hides the dock; the others bring
    // it back if it was hidden. Pages/SearchResults reuse the same
    // QListView with a different filter on the model.
    void setMode(Mode mode);
    // The list of page indices the SearchResults mode should show.
    // Empty list disables the filter (mode falls back to all pages).
    void setSearchMatchPages(const std::vector<int> &pages);

  signals:
    void deletePagesRequested(const std::vector<int> &pageIndices);
    void movePageRequested(int from, int to);
    void annotationSelected(int id);
    // Fires when the user explicitly toggles the dock open / shut
    // via the dock's title-bar X. The top-bar's mode picker uses
    // this to keep its check-state in sync.
    void modeChanged(Mode mode);

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

  private slots:
    void onThumbnailActivated(const QModelIndex &index);
    void syncSelectionFromDocument();
    void onAnnotationActivated();

  private:
    void applyMode();

    IDocument *m_doc = nullptr;
    QStackedWidget *m_stack = nullptr;
    QTabWidget *m_tabs = nullptr;
    QListView *m_thumbnails = nullptr;
    QListWidget *m_annotations = nullptr;
    // Tree view bound to the active document's outlineModel() on
    // setDocument. Shown in TableOfContents mode; collapses cleanly
    // when the doc has no /Outlines tree.
    QTreeView *m_outline = nullptr;
    ThumbnailModel *m_model = nullptr;
    QTimer m_pageSyncTimer;
    int m_placeholderIndex = 0;
    int m_tabsIndex = 0;
    int m_outlineIndex = 0;
    int m_annotationsIndex = 0;
    bool m_syncingSelection = false;
    Mode m_mode = Mode::Hidden;
    std::vector<int> m_searchMatchPages;
};

} // namespace trailer
