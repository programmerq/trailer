#pragma once

#include "document/DocumentRegistry.h"
#include "document/RecoveryStore.h"
#include "ml/MlScheduler.h"
#include "ml/ModelRegistry.h"
#include "recent/RecentFiles.h"
#include "settings/DocumentTypeDefaults.h"
#include "settings/SessionDraftStore.h"
#include "settings/Settings.h"

#include <QApplication>
#include <QList>
#include <QMenuBar>
#include <QPointer>
#include <QStringList>

#include <functional>
#include <memory>

class QAction;
class QMenu;
class QWidget;

namespace trailer {

class MainWindow;
class IDocument;
class UxRecorder;

// Screenshot capture mode. Shared by the File → Screenshot submenu
// (explicit modes) and the Tools → Take Screenshot picker so both drive
// the one capture backend (Application::captureScreenshot).
enum class ShotMode { Screen, Window, Region };

// How a quit request should treat the open windows/documents.
//   Normal      — prompt to save/name every unsaved or untitled document,
//                 one at a time (ADR-0004 close-save at quit); Cancel on
//                 any prompt aborts the quit.
//   KeepWindows — no prompt: serialize the open-window set, including the
//                 bytes of unsaved/untitled documents, to the draft store
//                 so the next launch restores them (macOS "Quit and Keep
//                 Windows", ⌥⌘Q).
// See docs/decision-records/2026-07-16-quit-and-keep-windows.md.
enum class QuitMode { Normal, KeepWindows };

class Application : public QApplication {
    Q_OBJECT

  public:
    Application(int &argc, char **argv);
    ~Application() override;

    // Sets the process-wide Qt identity strings (org/app name, domain, version). Static so tests can assert them without constructing the full application.
    static void applyIdentity();

    // Open `paths` into windows/tabs per the user's open-files-in
    // preference. When `markUntitled` is true the opened document(s) are
    // flagged untitled (see IDocument::isUntitled) — used by the macOS
    // clipboard / screenshot import paths, whose backing file is a
    // transient temp file the user never chose. Untitled docs prompt
    // Save-As on close instead of closing silently (ADR-0004).
    void openFiles(const QStringList &paths, bool markUntitled = false);
    void clearRecent();

    // Set just before a screenshot / clipboard-origin openFiles() call so
    // the resulting image document is stamped with the real capture
    // devicePixelRatio (device px / dpr = logical size) and defaults to
    // Actual Size. Consumed and reset by the next openFiles(); a value <=
    // 1.0 marks the doc capture-origin (Actual default) without stamping
    // a HiDPI ratio. Ordinary opens leave this unset (0.0) and are
    // unaffected.
    void setPendingCaptureDpr(double dpr) { m_pendingCaptureDpr = dpr; }

    // Shared "create / acquire" File-menu group. Both the per-window
    // MainWindow File menu and the macOS no-window menu bar call these
    // so create/acquire commands stay reachable whether or not a
    // document window is key (the regression these fix: New /
    // New-from-Clipboard / Acquire used to live ONLY in the macOS
    // no-window bar and vanished the moment a window became key).
    //
    // addNewFromClipboardAction: the ⌘N item. Its enabled state tracks
    // the clipboard live (image or openable file URL → enabled; else
    // disabled + tooltip, never a popup). Opts the File menu into showing
    // item tooltips so the disabled-state hint renders.
    QAction *addNewFromClipboardAction(QMenu *fileMenu);
    // addAcquireItems: the Screenshot submenu (Whole Screen / Window /
    // Selected Area), plus disabled Scanner / Camera placeholders.
    // `captureContext` is the window to hide during capture on macOS so
    // it doesn't occlude the shot; nullptr from the no-window bar.
    void addAcquireItems(QMenu *fileMenu, QWidget *captureContext);

    // Open whatever the clipboard holds (image → temp PNG; file URL /
    // path → open directly). No-op when the clipboard has nothing
    // openable — the ⌘N action is disabled in that state, so this is
    // only a guard, never a narration popup.
    void newFromClipboard();
    // Capture a screenshot in `mode` and open the result. `context` is
    // hidden during capture on macOS. Returns silently on user-cancel.
    void captureScreenshot(ShotMode mode, QWidget *context);

    // True when the clipboard currently holds an image or an openable
    // file (URL or an on-disk path in its text). Drives the ⌘N item's
    // enabled state.
    static bool clipboardHasOpenableContent();

    // Reopen the documents persisted at the last aboutToQuit. Honours
    // the user's "Restore previous windows on launch" setting. Called
    // from main.cpp when the user launches with no CLI file args.
    // Returns true if at least one file was restored — the caller then
    // skips spawning an empty MainWindow.
    //
    // A kept-windows draft store (written by requestQuit(KeepWindows))
    // takes precedence: it is restored and consumed first, so unsaved /
    // untitled documents come back with their content intact; the
    // path-list session is the fallback for a plain quit.
    bool restorePreviousSession();

    // Route a quit request through the requested mode. The explicit menu
    // commands map fixedly: ⌘Q → Normal (ALWAYS runs the per-doc prompt),
    // ⌥⌘Q → KeepWindows (ALWAYS keeps, NEVER prompts). The OS
    // NSQuitAlwaysKeepsWindows setting no longer flips this — it governs
    // only macOS's own window auto-restoration, not what these commands do
    // (decision-record refinement 2026-07-19). Returns true if the quit
    // proceeded (performQuit was invoked), false if it was aborted (a Normal
    // prompt was Cancelled) — nothing is written on abort.
    bool requestQuit(QuitMode mode);

    // Recreate the windows/documents held in the kept-windows draft store,
    // then consume (clear) it. Draft (unsaved/untitled) documents are
    // rehydrated from their stored bytes; saved documents reopen from disk.
    // Returns true if at least one document was restored. Public so a
    // headless test can drive restore without a real relaunch.
    bool restoreKeptWindows();

    // Snapshot the current open-window set into draft descriptors for the
    // KeepWindows path. Unsaved/untitled image documents become draft blobs
    // (their bytes); saved documents become path references. Public for
    // headless testing of the capture step.
    QList<SessionWindowDescriptor> captureSessionForKeep() const;

    // Test seams. performQuit is what requestQuit calls to actually quit —
    // overridable so a headless test asserts quit-was-called vs aborted
    // without terminating the test process. The keeps-windows probe feeds
    // the D3 OS-setting composition; default reads NSQuitAlwaysKeepsWindows
    // (false off macOS).
    void setPerformQuitForTesting(std::function<void()> fn) { m_performQuit = std::move(fn); }
    void setQuitKeepsWindowsProbeForTesting(std::function<bool()> fn) {
        m_quitKeepsWindowsProbe = std::move(fn);
    }
    // Direct access to the draft store (its directory is AppData/session-
    // drafts). Exposed so tests can assert what a quit wrote / clear state.
    SessionDraftStore &sessionDraftStore() { return m_draftStore; }
    // Repoint the draft store at a throwaway directory. Tests use this to
    // inject a store whose save() is made to fail (e.g. an unwritable path)
    // so the failed-save fallback can be exercised without touching the
    // real AppData location.
    void setSessionDraftStoreDirForTesting(const QString &dir) {
        m_draftStore = SessionDraftStore(dir);
    }

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

  public slots:
    // Re-evaluate the enabled state + tooltip of every registered
    // New-from-Clipboard action against the current clipboard. Wired to
    // QClipboard::dataChanged and to each File menu's aboutToShow.
    void refreshClipboardActions();

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
    // Track a New-from-Clipboard action so refreshClipboardActions()
    // keeps its enabled state + tooltip live. QPointer entries survive
    // the owning menu/window being destroyed.
    void registerClipboardAction(QAction *action);
    // True iff `doc`'s current content can be captured LOSSLESSLY as a
    // kept-windows draft blob with no user interaction — i.e. it is an image
    // document with a non-null raster we can PNG-encode. A dirty/untitled
    // document for which this is false (a PDF with unsaved annotations, an
    // image whose raster is null) must NOT be silently persisted as a clean
    // path reference; requestQuit(KeepWindows) falls back to the ADR-0004
    // per-document Save/Discard/Cancel prompt for exactly those before
    // quitting, so their edits are saved or explicitly discarded. See the
    // decision record (KeepWindows keeps what it can draft, prompts for
    // anything dirty it cannot).
    bool canDraftForKeep(IDocument *doc) const;
    // Run the Normal per-document Save/Discard/Cancel prompt across every
    // window's dirty/untitled documents. Returns false if any prompt was
    // Cancelled (or a Save failed) — the caller must then abort the quit.
    bool promptDirtyDocsForQuit();
#ifdef Q_OS_MACOS
    void installNoWindowMenuBar();
    void openFilesFromDialog();
    // Shared degrade UI for the no-window Acquire flow: one actionable modal
    // pointing at System Settings ▸ Screen Recording (the screen-capture
    // preflight ADR). The windowed path degrades via MainWindow::flashError
    // instead; captureScreenshot picks the right one by context.
    void showScreenRecordingNeededModal();
#endif

    // New-from-Clipboard actions across every File menu (per-window +
    // the macOS no-window bar). Kept in sync by refreshClipboardActions.
    QList<QPointer<QAction>> m_clipboardActions;

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
    // Kept-windows draft store (macOS "Quit and Keep Windows"). Directory
    // AppData/session-drafts; written by requestQuit(KeepWindows) and
    // consumed by restoreKeptWindows() on the next launch.
    SessionDraftStore m_draftStore;
    // Test seams — see the *ForTesting setters. m_performQuit defaults to
    // QCoreApplication::quit; m_quitKeepsWindowsProbe defaults to the
    // native NSQuitAlwaysKeepsWindows read (false off macOS / unset).
    std::function<void()> m_performQuit;
    std::function<bool()> m_quitKeepsWindowsProbe;
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
