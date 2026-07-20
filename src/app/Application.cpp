#include "Application.h"

#include "TrailerVersion.h"
#include "document/ImageAdapter.h"
#include "document/PdfAdapter.h"
#include "platform/ScreenCapturePermission.h"
#include "ui/MainWindow.h"
#ifdef TRAILER_UX_RECORDER
#include "uxrecord/UxPlatformCapture.h"
#include "uxrecord/UxRecorder.h"
#include <QDesktopServices>
#include <QPushButton>
#include <QUrl>
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
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPixmap>
#include <QProcess>
#include <QPushButton>
#include <QScreen>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>
#include <QWindow>

namespace trailer {

void Application::applyIdentity() {
    setApplicationName(QStringLiteral("Trailer"));
    setOrganizationName(QStringLiteral("Trailer"));
    // organizationDomain is set for Qt identity alignment only (reverses to
    // io.github.programmerq). It does NOT move any settings path: the sole
    // QSettings consumer (DocumentTypeDefaults) uses the 2-arg
    // QSettings(org, app) constructor, which keys off organizationName and
    // ignores the domain. No settings migration is required on any platform.
    setOrganizationDomain(QStringLiteral("programmerq.github.io"));
    setApplicationVersion(QStringLiteral(TRAILER_VERSION_STRING));
}

Application::Application(int &argc, char **argv) : QApplication(argc, argv) {
    applyIdentity();

#ifdef Q_OS_MACOS
    // macOS keeps a dock icon + global menu bar alive with zero windows, so the
    // app must survive the last window closing; a dismissed dialog must never quit it.
    setQuitOnLastWindowClosed(false);
#endif
    // On Win/Linux the persistent empty-state window means the app is never left
    // with zero top-level windows, so a modal dialog is never the sole top-level
    // and dismissing it can't trigger an implicit quit. Qt's default
    // quit-on-last-window-closed (true) is therefore left in place there —
    // disabling it would strand the process with no window and no way to quit
    // or open a file after the last window is torn down via WA_DeleteOnClose.

    m_settings.load();
    m_recent.setMaxEntries(m_settings.recentMax());
    m_recent.load();
    m_typeDefaults.load();

    m_registry.registerAdapter(std::make_unique<PdfAdapter>());
    m_registry.registerAdapter(std::make_unique<ImageAdapter>());

#ifdef TRAILER_UX_RECORDER
    // ADR 0014: let Mechanism A (the screenshot-import Screen-Recording
    // explainer in src/platform/) defer to Mechanism B's authoritative live
    // TCC gate so the two never double-prompt for the same macOS permission.
    // Injected here (rather than src/platform/ depending on src/uxrecord/) and
    // compiled out of default builds, which keep A standalone (G14.4).
    setScreenRecordingGrantedProbe([] { return uxScreenRecordingGranted(); });
#endif

    // Snapshot the open file list at quit. Done via aboutToQuit (not
    // closeEvent on each window) so we capture every window before any
    // is torn down — closeEvent ordering is platform-dependent and a
    // window that's already deleted has no documents to enumerate.
    connect(this, &QCoreApplication::aboutToQuit, this, &Application::onAboutToQuit);

    // Keep every New-from-Clipboard action's enabled state honest as the
    // clipboard changes underneath us, so ⌘N is live rather than only
    // re-checked when a File menu opens.
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this,
            &Application::refreshClipboardActions);

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
        qWarning("Trailer: UX recording could not be started (see warnings "
                 "above); continuing without recording.");
        m_uxRecorder.reset();
        return;
    }
    qInfo("Trailer: UX recording session %s -> %s", qPrintable(m_uxRecorder->sessionId()),
          qPrintable(m_uxRecorder->sessionDir()));
#else
    qWarning("Trailer: this build does not include the UX recorder "
             "(configure with -DTRAILER_ENABLE_UX_RECORDER=ON).");
#endif
}

#ifdef TRAILER_UX_RECORDER
Application::UxRecordDecision Application::preflightUxRecording() {
    // Screen Recording already granted (or no ScreenCaptureKit gate on
    // this OS) → record straight away, no dialog. Once the user grants
    // it once, they never see this again.
    if (uxScreenRecordingGranted()) {
        // ADR 0014 (G14.2): the permission is resolved for this session, so
        // burn Mechanism A's first-use explainer flag too. That way granting
        // Screen Recording (including via B's Open-Settings path on a prior
        // launch) also suppresses the screenshot-import explainer — the two
        // flows share suppression state instead of re-asking independently.
        //
        // Only acknowledge (which does a disk save()) when the flag isn't
        // already set — otherwise every recorder launch re-saves settings for
        // no change. Check the persisted flag directly, NOT
        // shouldShowScreenCaptureExplainer(): in recorder builds that helper
        // consults the granted-probe (uxScreenRecordingGranted()), which is
        // true here by construction, so it would always report "don't show"
        // and we'd never burn the flag for the non-recorder path's benefit.
        if (!m_settings.firstUseAcknowledged(
                QString::fromLatin1(kScreenCaptureExplainerKey))) {
            acknowledgeScreenCaptureExplainer(m_settings);
        }
        return UxRecordDecision::Start;
    }

    // Missing → don't silently start a session that records no screen
    // frames (the failure mode that wasted a real 6-minute session,
    // UXR-001). One blocking, actionable dialog instead.
    QMessageBox box;
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(tr("UX Recorder — Screen Recording not enabled"));
    box.setText(tr("Trailer's UX recorder can't capture the screen yet."));
    box.setInformativeText(
        tr("macOS only applies a Screen Recording grant to the next launch of an "
           "app, so recording now would capture your input and camera but no "
           "screen.\n\nApprove Trailer under Screen Recording, then relaunch — "
           "with record-by-default, that's just opening another file. Camera and "
           "input recording are unaffected either way."));
    auto *settingsButton = box.addButton(tr("Open Settings && Quit"), QMessageBox::AcceptRole);
    auto *degradedButton = box.addButton(tr("Record Without Screen"), QMessageBox::DestructiveRole);
    auto *skipButton = box.addButton(tr("Don't Record This Launch"), QMessageBox::RejectRole);
    box.setDefaultButton(settingsButton);
    box.exec();

    QObject *clicked = box.clickedButton();
    if (clicked == settingsButton) {
        // Register Trailer in the privacy list (so the pane shows a
        // toggle) and deep-link straight to it, then quit so the next
        // launch picks up the grant.
        uxRequestScreenRecording();
        QDesktopServices::openUrl(QUrl(QStringLiteral(
            "x-apple.systempreferences:com.apple.preference.security?Privacy_ScreenCapture")));
        return UxRecordDecision::Quit;
    }
    if (clicked == skipButton) {
        return UxRecordDecision::Skip;
    }
    Q_UNUSED(degradedButton); // "Record Without Screen" — proceed degraded.
    return UxRecordDecision::Start;
}
#endif

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

int Application::windowCount() const {
    int n = 0;
    for (const auto &ptr : m_windows) {
        if (ptr)
            ++n;
    }
    return n;
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
    // Consume a capture dpr staged by a screenshot / clipboard grab (see
    // setPendingCaptureDpr) up front — BEFORE the empty-paths guard — and
    // reset it immediately. Doing this at the very top means a staged dpr
    // can never leak into a later ordinary open even if this particular
    // call has nothing to open (empty paths) or bails before the loop.
    // Only this batch — never a subsequent open — is treated as
    // capture-origin.
    const double captureDpr = m_pendingCaptureDpr;
    m_pendingCaptureDpr = 0.0;

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

        if (captureDpr > 0.0) {
            // Stamp the real screen dpr onto capture-origin images so the
            // viewer treats device px as logical px / dpr, opens at
            // logical size, and defaults to pixel-exact Actual Size.
            if (auto *img = dynamic_cast<ImageDocument *>(doc.get()))
                img->markCaptureOrigin(captureDpr);
        }

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
    //   - Linux / Windows lastWindowClosed → app quits (Qt default
    //     quit-on-last-window-closed is left enabled off-Mac): by the
    //     time we get here, every window has already been deleted via
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

namespace {

// Unique temp path for a transient import (clipboard image, screenshot).
QString transientImportPath(const QString &prefix, const QString &ext) {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString stamp =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    const QString suffix = QUuid::createUuid().toString(QUuid::Id128);
    return QDir(base).filePath(
        QStringLiteral("trailer-%1-%2-%3.%4").arg(prefix, stamp, suffix, ext));
}

// Single source of truth for "what, if anything, on the clipboard can
// New-from-Clipboard open right now". The enable-gate
// (clipboardHasOpenableContent) and the action (newFromClipboard) both route
// through this so they can never disagree: precedence is image FIRST, then
// existing local-file URLs, then an existing file path pasted as text, and
// file URLs / text that don't exist on disk are skipped. Without this shared
// seam a clipboard holding both an image and a stale file:// URL could enable
// ⌘N (gate saw the image) yet open nothing (action took the dead URL).
struct ClipboardOpenable {
    bool hasImage = false;
    QStringList files;
    bool any() const { return hasImage || !files.isEmpty(); }
};

ClipboardOpenable inspectClipboard() {
    ClipboardOpenable result;
    const QClipboard *clipboard = QGuiApplication::clipboard();
    const QMimeData *data = clipboard->mimeData();
    if (!data)
        return result;
    // Image takes precedence — matches the gate's original ordering.
    if (data->hasImage() && !clipboard->image().isNull()) {
        result.hasImage = true;
        return result;
    }
    for (const QUrl &url : data->urls()) {
        if (url.isLocalFile()) {
            const QString local = url.toLocalFile();
            if (!local.isEmpty() && QFileInfo::exists(local))
                result.files.append(local);
        }
    }
    if (!result.files.isEmpty())
        return result;
    const QString text = data->text().trimmed();
    if (!text.isEmpty() && QFileInfo::exists(text))
        result.files.append(text);
    return result;
}

} // namespace

bool Application::clipboardHasOpenableContent() {
    return inspectClipboard().any();
}

void Application::registerClipboardAction(QAction *action) {
    if (!action)
        return;
    m_clipboardActions.append(QPointer<QAction>(action));
    // Prime the state so the item is honest before its menu first opens.
    refreshClipboardActions();
}

void Application::refreshClipboardActions() {
    const bool ok = clipboardHasOpenableContent();
    m_clipboardActions.removeIf([](const QPointer<QAction> &p) { return p.isNull(); });
    for (const QPointer<QAction> &p : m_clipboardActions) {
        if (!p)
            continue;
        p->setEnabled(ok);
        // Disabled + tooltip is the honest state — never a popup that
        // just says the clipboard is empty (PHILOSOPHY → No popup that
        // just says "no").
        p->setToolTip(ok ? QString()
                         : tr("Copy an image or a file to the clipboard, then use this to open it."));
    }
}

QAction *Application::addNewFromClipboardAction(QMenu *fileMenu) {
    // Menu-item tooltips only render when the QMenu opts in.
    fileMenu->setToolTipsVisible(true);
    auto *action = fileMenu->addAction(tr("New from &Clipboard"));
    // ⌘N / Ctrl+N. The hottest acquire path: copy an image, ⌘N, see it.
    // Replaces the former standalone "New" (blank window) binding.
    action->setShortcut(QKeySequence::New);
    connect(action, &QAction::triggered, this, &Application::newFromClipboard);
    registerClipboardAction(action);
    // Re-check the clipboard whenever this menu is about to show, so the
    // item is correct even between dataChanged signals.
    connect(fileMenu, &QMenu::aboutToShow, this, &Application::refreshClipboardActions);
    return action;
}

void Application::addAcquireItems(QMenu *fileMenu, QWidget *captureContext) {
    fileMenu->setToolTipsVisible(true);

    // Screenshot as an explicit-mode submenu. The OS picker hides its
    // mode switch behind an undiscoverable spacebar cycle; surfacing
    // Whole Screen / Window / Selected Area as named items makes the
    // modes discoverable (see DR 2026-07-18-file-menu-acquire-ia, Option A).
    QMenu *screenshotMenu = fileMenu->addMenu(tr("Screenshot"));
    screenshotMenu->setToolTipsVisible(true);

    auto *wholeScreen = screenshotMenu->addAction(tr("Whole Screen"));
    connect(wholeScreen, &QAction::triggered, this,
            [this, captureContext]() { captureScreenshot(ShotMode::Screen, captureContext); });

    auto *window = screenshotMenu->addAction(tr("Window"));
    connect(window, &QAction::triggered, this,
            [this, captureContext]() { captureScreenshot(ShotMode::Window, captureContext); });

    auto *selectedArea = screenshotMenu->addAction(tr("Selected Area"));
    connect(selectedArea, &QAction::triggered, this,
            [this, captureContext]() { captureScreenshot(ShotMode::Region, captureContext); });

#ifndef Q_OS_MACOS
    // Only whole-screen capture is meaningful via the QScreen fallback.
    // Keep Window / Selected Area visible-but-disabled with an honest
    // tooltip rather than dropping them (G3 + G4).
    window->setEnabled(false);
    window->setToolTip(tr("Window capture isn't available on this platform yet."));
    selectedArea->setEnabled(false);
    selectedArea->setToolTip(tr("Selected-area capture isn't available on this platform yet."));
#endif

    // Placeholders for acquire sources with no backend yet. Present but
    // disabled + tooltip (G3; do not hide roadmap acquire sources here —
    // they are peers of Screenshot on the acquire surface).
    auto *scanner = fileMenu->addAction(tr("Scanner"));
    scanner->setEnabled(false);
    scanner->setToolTip(tr("Scanner import isn't available yet."));

    auto *camera = fileMenu->addAction(tr("Camera"));
    camera->setEnabled(false);
    camera->setToolTip(tr("Camera import isn't available yet."));
}

void Application::newFromClipboard() {
    // Route through the same predicate as the enable-gate so the two can't
    // drift: image FIRST, then existing local files (URLs or a pasted path),
    // stale/non-existent file URLs skipped.
    const ClipboardOpenable openable = inspectClipboard();

    if (openable.hasImage) {
        const QImage image = QGuiApplication::clipboard()->image();
        if (image.isNull())
            return;
        const QString path = transientImportPath("clipboard", "png");
        if (image.save(path, "PNG")) {
            // Recover a devicePixelRatio for the paste. The PNG round-trip
            // (and most clipboard sources) drop the dpr stamp, so we must
            // decide whether this paste is a HiDPI full-screen grab that
            // should open 1:1 — WITHOUT shrinking ordinary pastes.
            //
            // Conservative heuristic: a blanket "stamp the primary screen's
            // dpr whenever dpr<=1" was a regression — on Retina it halved the
            // logical size of EVERY ordinary paste (a copied logo, diagram,
            // pixel art). Instead:
            //   1. If the clipboard image already carries dpr > 1.0, honor it.
            //   2. Else if the raw pixel size EXACTLY equals some connected
            //      screen's device resolution (size() * devicePixelRatio(),
            //      i.e. a full-screen grab), stamp THAT screen's dpr.
            //   3. Else leave it at dpr 1 — an ordinary paste opens at its
            //      natural logical size (fit-capped as before), no regression.
            // A region screenshot pasted from the clipboard that doesn't match
            // a full screen size will open at device size (no worse than
            // pre-fix), pending owner confirmation on Retina hardware.
            double dpr = image.devicePixelRatio();
            if (dpr <= 1.0) {
                dpr = 1.0;
                const QSize raw = image.size();
                for (const QScreen *scr : QGuiApplication::screens()) {
                    const QSize deviceRes =
                        (QSizeF(scr->size()) * scr->devicePixelRatio()).toSize();
                    if (raw == deviceRes) {
                        dpr = scr->devicePixelRatio();
                        break;
                    }
                }
            }
            setPendingCaptureDpr(dpr);
            openFiles({path});
        }
        return;
    }

    if (!openable.files.isEmpty()) {
        openFiles(openable.files);
        return;
    }

    // Nothing openable on the clipboard. The ⌘N item is disabled in this
    // state, so reaching here means a programmatic trigger — say nothing
    // (no narration popup). See PHILOSOPHY → No popup that just says "no".
}

void Application::captureScreenshot(ShotMode mode, QWidget *context) {
    const QString path = transientImportPath("screenshot", "png");

#ifdef Q_OS_MACOS
    // Preflight the live Screen Recording TCC state before touching the OS
    // selection UI (the screen-capture preflight ADR). This is the single
    // capture backend #86 introduced; both the per-window picker and the
    // File ▸ Screenshot submenu / macOS no-window bar route through it. The
    // pre-permission explainer is retired for stills (owner decision
    // 2026-07-17); we lean on the OS Screen Recording prompt directly.
    const ScreenCapturePermissionState state = queryScreenCapturePermissionState();

    // The native capture block. Hides the capture context (if any) so it
    // doesn't occlude the target, shells to the macOS capture tool for proper
    // DPI handling and interactive selection, then restores. Returns true only
    // when a real image landed at `path`. Local lambda so the RequestAccess and
    // Proceed branches share it verbatim.
    auto runCapture = [&]() -> bool {
        if (context)
            context->hide();
        QStringList args;
        args << QStringLiteral("-x"); // silent (no capture sound)
        switch (mode) {
        case ShotMode::Screen:
            break;
        case ShotMode::Window:
            args << QStringLiteral("-iW");
            break;
        case ShotMode::Region:
            args << QStringLiteral("-i") << QStringLiteral("-s");
            break;
        }
        args << path;
        QProcess proc;
        proc.start(QStringLiteral("/usr/sbin/screencapture"), args);
        proc.waitForFinished(-1);
        if (context) {
            context->show();
            context->raise();
            context->activateWindow();
        }
        if (proc.exitCode() != 0) {
            // User cancelled the OS selection (Esc) — a no-op, not an error.
            // Stay silent: no dialog narrating the user's own cancel.
            return false;
        }
        const QFileInfo info(path);
        if (!info.exists() || info.size() == 0) {
            // Exit 0 but no file: a granted user who selected nothing. Silent —
            // permission is not the problem (we only reach here after Granted or
            // a successful request), so do not assert a denial.
            return false;
        }
        return true;
    };

    bool captured = false;
    switch (decideScreenCaptureFlow(state)) {
    case ScreenCaptureFlowAction::Proceed:
        captured = runCapture();
        break;
    case ScreenCaptureFlowAction::RequestAccess:
        if (requestScreenCaptureAccess()) {
            captured = runCapture();
        } else {
            // Denied — actionable degrade. A window context has a status bar,
            // so flash the recovery route there; the no-window Acquire flow
            // (nullptr context) has none, so surface the actionable modal.
            if (auto *mw = qobject_cast<MainWindow *>(context))
                mw->flashError(screenRecordingNeededMessage());
            else
                showScreenRecordingNeededModal();
            return;
        }
        break;
    }
    if (!captured)
        return; // cancelled or empty capture — already handled silently
    // screencapture writes raw device pixels with no dpr stamp; recover the
    // screen dpr so a Retina capture opens 1:1 (see openFiles). Prefer the
    // capture context's screen when we have one, else the primary screen.
    // Known limitation: an interactive `screencapture -i` on a mixed-DPI
    // multi-monitor setup can land on a non-primary screen, so the recovered
    // dpr may be wrong; owner to confirm on hardware.
    QScreen *dprScreen = nullptr;
    if (context && context->window() && context->window()->windowHandle())
        dprScreen = context->window()->windowHandle()->screen();
    if (!dprScreen)
        dprScreen = QGuiApplication::primaryScreen();
    if (dprScreen)
        setPendingCaptureDpr(dprScreen->devicePixelRatio());
#else
    // QScreen fallback: only whole-screen capture is supported. The
    // Window / Selected-Area items are disabled in the UI on this
    // platform, so `mode` should already be Screen here.
    if (mode != ShotMode::Screen)
        return;
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen)
        return;
    const QPixmap shot = screen->grabWindow(0);
    if (shot.isNull() || !shot.save(path, "PNG"))
        return;
    // grabWindow() stamps the screen dpr on the pixmap, but the PNG save drops
    // it; recover it so a HiDPI capture opens 1:1 (see openFiles).
    {
        const double dpr =
            shot.devicePixelRatio() > 0.0 ? shot.devicePixelRatio() : screen->devicePixelRatio();
        setPendingCaptureDpr(dpr);
    }
#endif

    openFiles({path});
}

#ifdef Q_OS_MACOS
void Application::showScreenRecordingNeededModal() {
    // No status bar in the no-window Acquire flow — surface the recoverable
    // degrade as one actionable modal with a direct route to the setting. This
    // ask-first modal is the sanctioned pattern (PHILOSOPHY allows popups for
    // non-self-evident errors).
    QMessageBox box;
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(tr("Acquire from Screenshot"));
    box.setText(screenRecordingNeededMessage());
    QPushButton *open =
        box.addButton(tr("Open System Settings"), QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(open);
    box.exec();
    if (box.clickedButton() == open)
        openScreenRecordingSettings(); // best-effort deep link to the pane
}

void Application::installNoWindowMenuBar() {
    auto *bar = new QMenuBar();
    bar->setNativeMenuBar(true);

    auto *fileMenu = bar->addMenu(tr("&File"));
    fileMenu->setToolTipsVisible(true);

    // Shared create/acquire group at the top of the File menu — the same
    // items the per-window MainWindow File menu carries, so create and
    // acquire stay reachable whether or not a document window is key.
    addNewFromClipboardAction(fileMenu);

    auto *openAction = fileMenu->addAction(tr("&Open…"));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &Application::openFilesFromDialog);

    fileMenu->addSeparator();

    // No window to hide during capture in no-window mode → nullptr.
    addAcquireItems(fileMenu, nullptr);

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
    // macOS-only note: on macOS there is no persistent empty window —
    // closing the last window leaves just the dock icon + global menu bar.
    // Re-activating the app with zero windows (dock click, Cmd-Tab back in)
    // deliberately does NOTHING automatic: no file-open panel is presented.
    // Owner ruling (backlog 2026-07-12-macos-launch-no-open-panel): macOS
    // launch/activation with no windows is dock icon + menu bar only, and an
    // automatically-presented Open panel — whose dismissal read as an
    // unwanted quit — is removed. ⌘O / File → Open remain explicit user
    // actions that open the panel (see installNoWindowMenuBar's openAction).
    // We therefore no longer special-case ApplicationStateChange here.
    return QApplication::event(event);
}

} // namespace trailer
