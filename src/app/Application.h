#pragma once

#include "document/DocumentRegistry.h"
#include "document/RecoveryStore.h"
#include "ml/MlScheduler.h"
#include "ml/ModelRegistry.h"
#include "recent/RecentFiles.h"
#include "settings/DocumentTypeDefaults.h"
#include "settings/Settings.h"

#include <QApplication>
#include <QList>
#include <QMenuBar>
#include <QPointer>
#include <QStringList>

#include <memory>

namespace trailer {

class MainWindow;
class UxRecorder;

class Application : public QApplication {
    Q_OBJECT

  public:
    Application(int &argc, char **argv);
    ~Application() override;

    // Sets the process-wide Qt identity strings (org/app name, domain, version). Static so tests can assert them without constructing the full application.
    static void applyIdentity();

    void openFiles(const QStringList &paths);
    void clearRecent();

    // Set just before a screenshot / clipboard-origin openFiles() call so
    // the resulting image document is stamped with the real capture
    // devicePixelRatio (device px / dpr = logical size) and defaults to
    // Actual Size. Consumed and reset by the next openFiles(); a value <=
    // 1.0 marks the doc capture-origin (Actual default) without stamping
    // a HiDPI ratio. Ordinary opens leave this unset (0.0) and are
    // unaffected.
    void setPendingCaptureDpr(double dpr) { m_pendingCaptureDpr = dpr; }

    // Reopen the documents persisted at the last aboutToQuit. Honours
    // the user's "Restore previous windows on launch" setting. Called
    // from main.cpp when the user launches with no CLI file args.
    // Returns true if at least one file was restored — the caller then
    // skips spawning an empty MainWindow.
    bool restorePreviousSession();

    // Begin a local UX recording session. Called from main.cpp before
    // any window exists so the first window already carries the
    // recording indicator. Recorder-enabled builds call this on every
    // launch unless --no-ux-record is passed. In builds configured
    // without TRAILER_ENABLE_UX_RECORDER this only logs a warning, and
    // is unreachable in practice because main.cpp compiles the call
    // out entirely.
    void startUxRecording();

#ifdef TRAILER_UX_RECORDER
    // Outcome of the pre-recording permission gate (UXR-001).
    enum class UxRecordDecision {
        Start, // record this launch (permissions ok, or "Record Without Screen")
        Skip,  // run Trailer normally, no session this launch
        Quit,  // user chose to fix permissions; main.cpp should exit(0)
    };
    // macOS only applies a Screen Recording grant to the NEXT launch, so
    // starting a session without it silently yields zero screen frames.
    // Before recording, check that grant and — if missing — show one
    // blocking dialog (open Settings & quit / record without screen /
    // don't record). Returns Start immediately when the grant is present
    // (or off macOS). Called from main.cpp; see docs/ux-recorder.md.
    UxRecordDecision preflightUxRecording();
#endif

    Settings &settings() { return m_settings; }
    RecentFiles &recentFiles() { return m_recent; }
    DocumentTypeDefaults &documentTypeDefaults() { return m_typeDefaults; }
    DocumentRegistry &registry() { return m_registry; }
    RecoveryStore &recoveryStore() { return m_recoveryStore; }
    ModelRegistry &modelRegistry() { return m_modelRegistry; }
    MlScheduler &mlScheduler() { return m_mlScheduler; }

    // Return the first existing window, or spawn one if none exist.
    // Idempotent: callers can use it to "make sure there's a window".
    MainWindow *ensureWindow();
    // Always spawn a new empty window and return it. Used by the
    // window-per-file open flow so each file gets its own frame.
    MainWindow *ensureFreshWindow();
    // Snapshot of every live MainWindow this Application owns.
    // QPointer entries can be null (a destruction is queued); the
    // Window menu filters those out before showing the list.
    QList<MainWindow *> windows() const;
    // Count of live (non-null) MainWindows. Used by the empty-state
    // window model to decide whether closing the last document should
    // close the window (other windows exist) or persist it as an
    // empty-state window (this is the last window).
    int windowCount() const;
#ifdef Q_OS_MACOS
    QMenuBar *noWindowMenuBar() const { return m_noWindowMenuBar.data(); }
#endif

  protected:
    bool event(QEvent *event) override;

  private slots:
    void onWindowDestroyed(QObject *window);
    // Snapshot the paths of every currently-open document into
    // Settings::sessionOpenFiles so the next launch can reopen them.
    // Wired to QCoreApplication::aboutToQuit.
    void onAboutToQuit();

  private:
    void notifyWindowsRecentChanged();
#ifdef Q_OS_MACOS
    void installNoWindowMenuBar();
    void openFilesFromDialog();
    void newFromClipboard();
    void acquireFromScreenshot();
#endif

    Settings m_settings;
    RecentFiles m_recent;
    DocumentTypeDefaults m_typeDefaults;
    DocumentRegistry m_registry;
    RecoveryStore m_recoveryStore;
    ModelRegistry m_modelRegistry;
    // Single ML task scheduler shared across MainWindows. Holds a
    // worker thread + power-policy watcher; lives as long as the
    // QApplication so its destructor blocks on outstanding tasks.
    MlScheduler m_mlScheduler{&m_settings};
    QList<QPointer<MainWindow>> m_windows;
    // Capture devicePixelRatio staged by a screenshot / clipboard grab,
    // consumed by the next openFiles(). 0.0 == none (ordinary open).
    double m_pendingCaptureDpr = 0.0;
#ifdef Q_OS_MACOS
    QPointer<QMenuBar> m_noWindowMenuBar;
#endif
#ifdef TRAILER_UX_RECORDER
    // Live for the rest of the process once --ux-record started it;
    // stop() is driven by aboutToQuit (self-connected) and the
    // destructor, so teardown is safe in either order.
    std::unique_ptr<UxRecorder> m_uxRecorder;
#endif
};

} // namespace trailer
