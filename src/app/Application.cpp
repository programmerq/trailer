#include "Application.h"

#include "TrailerVersion.h"
#include "document/ImageAdapter.h"
#include "document/PdfAdapter.h"
#include "ui/MainWindow.h"
#ifdef TRAILER_UX_RECORDER
#include "uxrecord/UxRecorder.h"
#endif

#include <QAction>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFileOpenEvent>
#include <QFileDialog>
#include <QGuiApplication>
#include <QImage>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QMessageBox>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>

namespace trailer {

Application::Application(int &argc, char **argv) : QApplication(argc, argv) {
    setApplicationName(QStringLiteral("Trailer"));
    setOrganizationName(QStringLiteral("Trailer"));
    setApplicationVersion(QStringLiteral(TRAILER_VERSION_STRING));

    m_settings.load();
    m_recent.setMaxEntries(m_settings.recentMax());
    m_recent.load();
    m_typeDefaults.load();

    m_registry.registerAdapter(std::make_unique<PdfAdapter>());
    m_registry.registerAdapter(std::make_unique<ImageAdapter>());

    // Snapshot the open file list at quit. Done via aboutToQuit (not
    // closeEvent on each window) so we capture every window before any
    // is torn down — closeEvent ordering is platform-dependent and a
    // window that's already deleted has no documents to enumerate.
    connect(this, &QCoreApplication::aboutToQuit, this, &Application::onAboutToQuit);

#ifdef Q_OS_MACOS
    installNoWindowMenuBar();
#endif
}

Application::~Application() = default;

void Application::startUxRecording() {
#ifdef TRAILER_UX_RECORDER
    if (m_uxRecorder) {
        return;
    }
    m_uxRecorder = std::make_unique<UxRecorder>();
    if (!m_uxRecorder->start()) {
        qWarning("Trailer: --ux-record was passed but the session could not be "
                 "started (see warnings above); continuing without recording.");
        m_uxRecorder.reset();
        return;
    }
    qInfo("Trailer: UX recording session %s -> %s",
          qPrintable(m_uxRecorder->sessionId()), qPrintable(m_uxRecorder->sessionDir()));
#else
    qWarning("Trailer: this build does not include the UX recorder "
             "(configure with -DTRAILER_ENABLE_UX_RECORDER=ON).");
#endif
}

MainWindow *Application::ensureWindow() {
    for (auto &ptr : m_windows) {
        if (ptr) {
            return ptr;
        }
    }
    return ensureFreshWindow();
}

QList<MainWindow *> Application::windows() const {
    QList<MainWindow *> out;
    out.reserve(m_windows.size());
    for (const auto &ptr : m_windows) {
        if (ptr)
            out.append(ptr.data());
    }
    return out;
}

MainWindow *Application::ensureFreshWindow() {
    auto *window = new MainWindow(this);
    window->setAttribute(Qt::WA_DeleteOnClose);
    connect(window, &QObject::destroyed, this, &Application::onWindowDestroyed);
    m_windows.append(window);
    window->show();
    return window;
}

void Application::openFiles(const QStringList &paths) {
    if (paths.isEmpty()) {
        return;
    }

    const OpenFilesIn mode = m_settings.openFilesIn();

    // Resolve the first-existing window once — used by SameWindow and
    // NewTab modes. For NewWindow we don't reuse anything; every file
    // gets a fresh window so closing it is "close this file" without
    // touching unrelated work.
    auto firstExistingWindow = [this]() -> MainWindow * {
        for (auto &ptr : m_windows) {
            if (ptr)
                return ptr;
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
        if (paths.size() < 2)
            return false;
        static const QSet<QString> imageExts = {
            QStringLiteral("png"),  QStringLiteral("jpg"), QStringLiteral("jpeg"),
            QStringLiteral("bmp"),  QStringLiteral("tif"), QStringLiteral("tiff"),
            QStringLiteral("webp"), QStringLiteral("gif"), QStringLiteral("heic"),
            QStringLiteral("heif"),
        };
        for (const QString &p : paths) {
            const QString ext = QFileInfo(p).suffix().toLower();
            if (!imageExts.contains(ext))
                return false;
        }
        return true;
    };
    const bool batchedImages = mode == OpenFilesIn::NewWindow && isImageBatch();
    MainWindow *batchTarget = batchedImages ? ensureFreshWindow() : nullptr;

    for (const QString &path : paths) {
        auto doc = m_registry.open(path);

        MainWindow *target = nullptr;
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
                if (!target)
                    target = ensureWindow();
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
    for (auto &ptr : m_windows) {
        if (ptr) {
            ptr->rebuildRecentMenu();
        }
    }
}

void Application::onWindowDestroyed(QObject *window) {
    m_windows.erase(std::remove_if(m_windows.begin(), m_windows.end(),
                                   [window](const QPointer<MainWindow> &p) {
                                       return p.data() == window || p.isNull();
                                   }),
                    m_windows.end());
}

bool Application::restorePreviousSession() {
    if (!m_settings.restorePreviousWindows())
        return false;
    const QStringList stored = m_settings.sessionOpenFiles();
    QStringList paths;
    paths.reserve(stored.size());
    // Filter out paths that no longer exist — a stale session entry
    // from a deleted file should not pop up an error dialog on launch.
    // We silently drop them; the file remains in the recent list (if
    // present there) so the user can re-locate it via File → Open
    // Recent if the underlying file comes back.
    for (const QString &p : stored) {
        if (!p.isEmpty() && QFileInfo::exists(p)) {
            paths.append(p);
        }
    }
    if (paths.isEmpty())
        return false;
    openFiles(paths);
    return !m_windows.isEmpty();
}

void Application::onAboutToQuit() {
    // Walk every still-live window for its open documents.
    //   - macOS Cmd+Q: QCoreApplication::quit() doesn't fire
    //     closeEvents on the windows; they're still alive in
    //     m_windows when aboutToQuit lands, so this walk catches
    //     the session.
    //   - Linux / Windows lastWindowClosed → app quits: by the time
    //     we get here, every window has already been deleted via
    //     WA_DeleteOnClose, so m_windows is empty and the session
    //     list is empty too — which is correct, since the user
    //     manually closed every window before the implicit quit.
    //   - Explicit menu Quit on Linux/Win: same as macOS Cmd+Q.
    QStringList paths;
    for (const auto &ptr : m_windows) {
        if (!ptr)
            continue;
        const int total = ptr->documentCount();
        for (int i = 0; i < total; ++i) {
            IDocument *doc = nullptr;
            if (!ptr->documentAt(i, &doc) || !doc)
                continue;
            const QString p = doc->filePath();
            if (!p.isEmpty() && !paths.contains(p)) {
                paths.append(p);
            }
        }
    }
    m_settings.setSessionOpenFiles(paths);
    m_settings.save();
}

#ifdef Q_OS_MACOS
namespace {

QString transientImportPath(const QString &prefix, const QString &ext) {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString stamp =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    const QString suffix = QUuid::createUuid().toString(QUuid::Id128);
    return QDir(base).filePath(
        QStringLiteral("trailer-%1-%2-%3.%4").arg(prefix, stamp, suffix, ext));
}

} // namespace

void Application::installNoWindowMenuBar() {
    auto *bar = new QMenuBar();
    bar->setNativeMenuBar(true);

    auto *fileMenu = bar->addMenu(tr("&File"));

    auto *newAction = fileMenu->addAction(tr("&New"));
    newAction->setShortcut(QKeySequence::New);
    connect(newAction, &QAction::triggered, this, [this]() { ensureFreshWindow(); });

    auto *openAction = fileMenu->addAction(tr("&Open…"));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &Application::openFilesFromDialog);

    auto *newFromClipboardAction = fileMenu->addAction(tr("New from &Clipboard"));
    connect(newFromClipboardAction, &QAction::triggered, this, &Application::newFromClipboard);

    auto *acquireAction = fileMenu->addAction(tr("&Acquire…"));
    connect(acquireAction, &QAction::triggered, this, &Application::acquireFromScreenshot);

    fileMenu->addSeparator();

    auto *closeWindowAction = fileMenu->addAction(tr("&Close Window"));
    closeWindowAction->setShortcut(QKeySequence::Close);
    connect(closeWindowAction, &QAction::triggered, this, []() {
        if (auto *w = qobject_cast<MainWindow *>(QApplication::activeWindow())) {
            w->close();
        }
    });

    auto *quitAction = fileMenu->addAction(tr("&Quit"));
    quitAction->setShortcut(QKeySequence::Quit);
    quitAction->setMenuRole(QAction::QuitRole);
    connect(quitAction, &QAction::triggered, this, &QCoreApplication::quit);

    m_noWindowMenuBar = bar;
}

void Application::openFilesFromDialog() {
    const QStringList paths = QFileDialog::getOpenFileNames(
        nullptr, tr("Open files"), QString(),
        tr("Documents (*.pdf *.png *.jpg *.jpeg *.bmp *.tif *.tiff *.webp "
           "*.gif *.heic *.heif);;All files (*)"));
    openFiles(paths);
}

void Application::newFromClipboard() {
    const QMimeData *data = QGuiApplication::clipboard()->mimeData();
    if (!data)
        return;

    QStringList paths;
    for (const QUrl &url : data->urls()) {
        if (url.isLocalFile()) {
            const QString local = url.toLocalFile();
            if (!local.isEmpty())
                paths.append(local);
        }
    }
    if (!paths.isEmpty()) {
        openFiles(paths);
        return;
    }

    const QImage image = QGuiApplication::clipboard()->image();
    if (!image.isNull()) {
        const QString path = transientImportPath("clipboard", "png");
        if (image.save(path, "PNG")) {
            openFiles({path});
            return;
        }
    }

    const QString text = data->text().trimmed();
    if (!text.isEmpty() && QFileInfo::exists(text)) {
        openFiles({text});
        return;
    }

    QMessageBox::information(nullptr, tr("New from Clipboard"),
                             tr("Clipboard does not currently contain an image or file path."));
}

void Application::acquireFromScreenshot() {
    const QString path = transientImportPath("acquire", "png");
    QProcess proc;
    proc.start(QStringLiteral("/usr/sbin/screencapture"),
               {QStringLiteral("-i"), QStringLiteral("-x"), path});
    proc.waitForFinished(-1);
    if (proc.exitCode() != 0)
        return;
    const QFileInfo info(path);
    if (!info.exists() || info.size() == 0)
        return;
    openFiles({path});
}
#endif

bool Application::event(QEvent *event) {
    if (event->type() == QEvent::FileOpen) {
        auto *fileOpen = static_cast<QFileOpenEvent *>(event);
        const QString path = fileOpen->file();
        if (!path.isEmpty()) {
            openFiles({path});
            return true;
        }
    }
    return QApplication::event(event);
}

} // namespace trailer
