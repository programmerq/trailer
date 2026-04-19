#pragma once

#include "document/DocumentRegistry.h"
#include "recent/RecentFiles.h"
#include "settings/Settings.h"

#include <QApplication>
#include <QList>
#include <QPointer>
#include <QStringList>

namespace trailer {

class MainWindow;

class Application : public QApplication {
    Q_OBJECT

public:
    Application(int& argc, char** argv);
    ~Application() override;

    void openFiles(const QStringList& paths);
    void clearRecent();

    Settings& settings() { return m_settings; }
    RecentFiles& recentFiles() { return m_recent; }
    DocumentRegistry& registry() { return m_registry; }

    MainWindow* ensureWindow();

protected:
    bool event(QEvent* event) override;

private slots:
    void onWindowDestroyed(QObject* window);

private:
    void notifyWindowsRecentChanged();

    Settings m_settings;
    RecentFiles m_recent;
    DocumentRegistry m_registry;
    QList<QPointer<MainWindow>> m_windows;
};

}  // namespace trailer
