#pragma once

#include "document/IDocument.h"

#include <QMainWindow>
#include <QStringList>
#include <memory>

class QMenu;

namespace trailer {

class Application;
class DocumentView;
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

private:
    void buildMenus();

    Application* m_app;
    DocumentView* m_documentView = nullptr;
    Sidebar* m_sidebar = nullptr;
    QMenu* m_recentMenu = nullptr;
};

}  // namespace trailer
