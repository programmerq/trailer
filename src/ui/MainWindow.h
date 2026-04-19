#pragma once

#include "document/IDocument.h"

#include <QActionGroup>
#include <QMainWindow>
#include <QStringList>
#include <memory>

class QAction;
class QMenu;

namespace trailer {

class Application;
class DocumentView;
class SearchBar;
class Sidebar;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(Application* app, QWidget* parent = nullptr);

    void addDocument(std::unique_ptr<IDocument> document);
    int documentCount() const;

public slots:
    void rebuildRecentMenu();

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void onOpen();
    void onAbout();
    void onCurrentDocumentChanged(IDocument* doc);

private:
    void buildMenus();
    void buildEditMenu(QMenu* editMenu);
    void buildViewMenu(QMenu* viewMenu);
    void syncViewModeActions(IDocument* doc);
    void showSearchBar();
    void hideSearchBar();

    Application* m_app;
    DocumentView* m_documentView = nullptr;
    SearchBar* m_searchBar = nullptr;
    Sidebar* m_sidebar = nullptr;
    QMenu* m_recentMenu = nullptr;

    QAction* m_printAction = nullptr;
    QAction* m_findAction = nullptr;
    QAction* m_findNextAction = nullptr;
    QAction* m_findPreviousAction = nullptr;

    QAction* m_zoomInAction = nullptr;
    QAction* m_zoomOutAction = nullptr;
    QAction* m_zoomActualAction = nullptr;
    QAction* m_zoomFitAction = nullptr;

    QActionGroup* m_viewModeGroup = nullptr;
    QAction* m_singlePageAction = nullptr;
    QAction* m_twoPagesAction = nullptr;
    QAction* m_continuousAction = nullptr;
};

}  // namespace trailer
