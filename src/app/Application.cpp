#include "Application.h"

#include "document/ImageAdapter.h"
#include "document/PdfAdapter.h"
#include "ui/MainWindow.h"

#include <QFileInfo>
#include <QFileOpenEvent>
#include <QSet>

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
    return ensureFreshWindow();
}

MainWindow* Application::ensureFreshWindow() {
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

    // Resolve the first-existing window once — used by SameWindow and
    // NewTab modes. For NewWindow we don't reuse anything; every file
    // gets a fresh window so closing it is "close this file" without
    // touching unrelated work.
    auto firstExistingWindow = [this]() -> MainWindow* {
        for (auto& ptr : m_windows) {
            if (ptr) return ptr;
        }
        return nullptr;
    };

    // Heuristic from the 2026-04-24 HITL feedback: "if there are
    // multiple single-page files open, like multiple images opened
    // together, we'll use the thumbnail bar for moving around."
    // The full thumbnail-bar mode is a bigger refactor, but the
    // first half of the request — keep them in one window — is
    // already supported by the QTabWidget central widget. When the
    // user opens a batch of images, route them all into one fresh
    // window so they share a tab strip rather than spawning N
    // separate frames.
    auto isImageBatch = [&paths]() -> bool {
        if (paths.size() < 2) return false;
        static const QSet<QString> imageExts = {
            QStringLiteral("png"), QStringLiteral("jpg"),
            QStringLiteral("jpeg"), QStringLiteral("bmp"),
            QStringLiteral("tif"), QStringLiteral("tiff"),
            QStringLiteral("webp"), QStringLiteral("gif"),
            QStringLiteral("heic"), QStringLiteral("heif"),
        };
        for (const QString& p : paths) {
            const QString ext = QFileInfo(p).suffix().toLower();
            if (!imageExts.contains(ext)) return false;
        }
        return true;
    };
    const bool batchedImages =
        mode == OpenFilesIn::NewWindow && isImageBatch();
    MainWindow* batchTarget = batchedImages ? ensureFreshWindow() : nullptr;

    for (const QString& path : paths) {
        auto doc = m_registry.open(path);

        MainWindow* target = nullptr;
        if (batchTarget) {
            // All images of this batch share one window so the user
            // can flip through them via the tab strip without
            // arranging multiple frames manually.
            target = batchTarget;
        } else {
            switch (mode) {
                case OpenFilesIn::NewWindow:
                    // One window per file. Even when `paths` has
                    // multiple entries we spawn a separate window for
                    // each so the user can arrange them independently.
                    target = ensureFreshWindow();
                    break;
                case OpenFilesIn::SameWindow:
                case OpenFilesIn::NewTab:
                    target = firstExistingWindow();
                    if (!target) target = ensureWindow();
                    break;
            }
        }

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
