#include "Application.h"

#include "document/ImageAdapter.h"
#include "document/PdfAdapter.h"
#include "ui/MainWindow.h"

#include <QFileOpenEvent>

namespace trailer {

Application::Application(int& argc, char** argv) : QApplication(argc, argv) {
    setApplicationName(QStringLiteral("Trailer"));
    setOrganizationName(QStringLiteral("Trailer"));
    setApplicationVersion(QStringLiteral("0.1.0"));

    m_settings.load();
    m_recent.setMaxEntries(m_settings.recentMax());
    m_recent.load();

    m_registry.registerAdapter(std::make_unique<PdfAdapter>());
    m_registry.registerAdapter(std::make_unique<ImageAdapter>());
}

Application::~Application() = default;

MainWindow* Application::ensureWindow() {
    for (auto& ptr : m_windows) {
        if (ptr) {
            return ptr;
        }
    }
    auto* window = new MainWindow(this);
    window->setAttribute(Qt::WA_DeleteOnClose);
    connect(window, &QObject::destroyed, this, &Application::onWindowDestroyed);
    m_windows.append(window);
    window->show();
    return window;
}

void Application::openFiles(const QStringList& paths) {
    if (paths.isEmpty()) {
        return;
    }

    const OpenFilesIn mode = m_settings.openFilesIn();
    MainWindow* target = nullptr;

    switch (mode) {
        case OpenFilesIn::NewWindow:
            target = nullptr;
            break;
        case OpenFilesIn::SameWindow:
        case OpenFilesIn::NewTab:
            for (auto& ptr : m_windows) {
                if (ptr) {
                    target = ptr;
                    break;
                }
            }
            break;
    }

    if (!target) {
        target = ensureWindow();
    }

    for (const QString& path : paths) {
        auto doc = m_registry.open(path);
        target->addDocument(std::move(doc));
        m_recent.add(path);
    }
    m_recent.save();
    notifyWindowsRecentChanged();
}

void Application::clearRecent() {
    m_recent.clear();
    m_recent.save();
    notifyWindowsRecentChanged();
}

void Application::notifyWindowsRecentChanged() {
    for (auto& ptr : m_windows) {
        if (ptr) {
            ptr->rebuildRecentMenu();
        }
    }
}

void Application::onWindowDestroyed(QObject* window) {
    m_windows.erase(
        std::remove_if(m_windows.begin(), m_windows.end(),
            [window](const QPointer<MainWindow>& p) {
                return p.data() == window || p.isNull();
            }),
        m_windows.end());
}

bool Application::event(QEvent* event) {
    if (event->type() == QEvent::FileOpen) {
        auto* fileOpen = static_cast<QFileOpenEvent*>(event);
        const QString path = fileOpen->file();
        if (!path.isEmpty()) {
            openFiles({path});
            return true;
        }
    }
    return QApplication::event(event);
}

}  // namespace trailer
