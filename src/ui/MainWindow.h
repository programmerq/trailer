#pragma once

#include "document/IDocument.h"

#include <QActionGroup>
#include <QMainWindow>
#include <QStringList>
#include <memory>

class QAction;
class QMenu;

namespace trailer {

class AnimationBar;
class Application;
class DocumentView;
class Magnifier;
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
    void onSave();
    void onSaveAs();
    void onRotateLeft();
    void onRotateRight();
    void onFlipHorizontal();
    void onFlipVertical();
    void onAdjustSize();
    void onAdjustColour();
    void onExportAs();
    void onTakeScreenshot();
    void onInsertPages();
    void onCropPages();
    void onAbout();
    void onCurrentDocumentChanged(IDocument* doc);

private:
    void buildMenus();
    void buildEditMenu(QMenu* editMenu);
    void buildViewMenu(QMenu* viewMenu);
    void buildToolsMenu(QMenu* toolsMenu);
    void syncViewModeActions(IDocument* doc);
    void showSearchBar();
    void hideSearchBar();
    void updateTitleForDocument(IDocument* doc);
    int selectedPageForEdit(IDocument* doc) const;

    Application* m_app;
    DocumentView* m_documentView = nullptr;
    AnimationBar* m_animationBar = nullptr;
    Magnifier* m_magnifier = nullptr;
    SearchBar* m_searchBar = nullptr;
    Sidebar* m_sidebar = nullptr;
    QMenu* m_recentMenu = nullptr;

    QAction* m_saveAction = nullptr;
    QAction* m_saveAsAction = nullptr;
    QAction* m_rotateLeftAction = nullptr;
    QAction* m_rotateRightAction = nullptr;
    QAction* m_flipHorizontalAction = nullptr;
    QAction* m_flipVerticalAction = nullptr;
    QAction* m_adjustSizeAction = nullptr;
    QAction* m_adjustColourAction = nullptr;
    QAction* m_exportAsAction = nullptr;
    QAction* m_screenshotAction = nullptr;
    QAction* m_insertPagesAction = nullptr;
    QAction* m_cropPagesAction = nullptr;
    QAction* m_printAction = nullptr;
    QAction* m_findAction = nullptr;
    QAction* m_findNextAction = nullptr;
    QAction* m_findPreviousAction = nullptr;

    QAction* m_zoomInAction = nullptr;
    QAction* m_zoomOutAction = nullptr;
    QAction* m_zoomActualAction = nullptr;
    QAction* m_zoomFitAction = nullptr;
    QAction* m_magnifierAction = nullptr;

    QActionGroup* m_viewModeGroup = nullptr;
    QAction* m_singlePageAction = nullptr;
    QAction* m_twoPagesAction = nullptr;
    QAction* m_continuousAction = nullptr;

    QAction* m_previousPageAction = nullptr;
    QAction* m_nextPageAction = nullptr;
};

}  // namespace trailer
