#include "Application.h"

#include "TrailerVersion.h"
#include "document/ImageAdapter.h"
#include "document/PdfAdapter.h"
#include "platform/QuitMenu.h"
#include "platform/PortalScreenshot.h"
#include "platform/ScreenCaptureBackend.h"
#include "platform/ScreenCapturePermission.h"
#include "ui/MainWindow.h"
#include "util/TempPath.h"
#ifdef TRAILER_UX_RECORDER
#include "uxrecord/UxPlatformCapture.h"
#include "uxrecord/UxRecorder.h"
#include <QDesktopServices>
#include <QPushButton>
#include <QUrl>
#endif

#include <QAction>
#include <QBuffer>
#include <QClipboard>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
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
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QScreen>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>
#include <QWindow>

namespace trailer {

namespace {

// Serialize a structurally-edited PDF's live editor graph (structural edits
// included, regular annotations written as editable /Annot) to a throwaway
// temp and read the bytes back. writeRecoverySnapshot() does NOT mutate the
// live document. Returns an empty QByteArray on any failure — the caller
// treats empty as "not snapshottable" and falls back to the per-doc prompt so
// the doc is never silently dropped.
QByteArray snapshotStructuralPdfBytes(PdfDocument *pdf) {
    if (!pdf)
        return {};
    ScopedTempFile snap(QStringLiteral("trailer-keep-XXXXXX.pdf"));
    if (!snap.isValid() || !pdf->writeRecoverySnapshot(snap.path()))
        return {};
    QFile f(snap.path());
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}

} // namespace

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

    // Quit seams (see requestQuit). Default performQuit terminates the app;
    // the keeps-windows probe reads the OS NSQuitAlwaysKeepsWindows default
    // (false off macOS). Tests override both to observe quit decisions
    // without terminating the process.
    m_performQuit = [] { QCoreApplication::quit(); };
    m_quitKeepsWindowsProbe = [] { return QuitMenu::osQuitAlwaysKeepsWindows(); };

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

void Application::openFiles(const QStringList &paths, bool markUntitled) {
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

    // CF-5: In NewWindow mode, reuse an empty/untouched launch window for
    // the FIRST opened document — and for an image batch's shared window —
    // instead of spawning a second window and orphaning the empty one
    // (matches Preview.app). "Empty/untouched" == a live MainWindow with
    // documentCount()==0 (a window with no document holds nothing to
    // clobber). Compute the candidate ONCE, up front, BEFORE the batch
    // decision, and consume it at most once: the image batch takes it if
    // present, otherwise the first single NewWindow file does; every
    // subsequent file still gets a fresh window. Never reuse a window that
    // already holds a document — opening a file while a real document
    // window is active must still spawn a new window.
    MainWindow *reuseCandidate = nullptr;
    if (mode == OpenFilesIn::NewWindow) {
        // Prefer the active/frontmost window if it's one of ours and empty.
        if (auto *active = qobject_cast<MainWindow *>(QApplication::activeWindow())) {
            if (m_windows.contains(active) && active->documentCount() == 0)
                reuseCandidate = active;
        }
        // Fallback: the launch-window case where offscreen/headless may not
        // set an active window — exactly one live window, and it's empty.
        if (!reuseCandidate && windowCount() == 1) {
            if (MainWindow *only = firstExistingWindow();
                only && only->documentCount() == 0)
                reuseCandidate = only;
        }
    }

    // Consume the reuse candidate at most once, then fall back to a fresh
    // window. Shared by the image-batch target below and the per-file
    // NewWindow loop so the consume-once invariant lives in one place and
    // can't drift between the two call sites.
    auto takeReuseOrFresh = [&]() -> MainWindow * {
        if (reuseCandidate) {
            MainWindow *w = reuseCandidate;
            reuseCandidate = nullptr;
            return w;
        }
        return ensureFreshWindow();
    };

    // An image batch shares one window (the tab strip) rather than spawning
    // N frames. Reuse the empty launch window as that batch window when one
    // is available so the batch path no longer orphans it; taking the
    // candidate here consumes it so the single-file loop below can't claim
    // it a second time.
    MainWindow *batchTarget = batchedImages ? takeReuseOrFresh() : nullptr;

    for (const QString &path : paths) {
        auto doc = m_registry.open(path);

        // Defensive: a failed open (null document) must not consume the
        // reuse candidate or a freshly spawned window on a no-op
        // addDocument(nullptr). Skip it before any target is selected so a
        // later good file still reuses the empty launch window.
        if (!doc)
            continue;

        // Reopen-recovery: if a newer auto-save recovery sidecar exists for
        // this backing file (e.g. the app crashed mid-session before an
        // explicit Save), silently restore the in-progress edit as a dirty
        // document. The backing file is not touched — it stays byte-identical
        // until the user Saves; if they Discard, their file was never
        // modified. Sidecars live in app-data, so this reads only our own
        // snapshot, never the user's directory.
        if (!path.isEmpty()) {
            if (const auto sidecar = m_recoveryStore.pendingRecovery(path)) {
                if (!doc->recoverFrom(*sidecar)) {
                    // Restore failed (corrupt/unreadable snapshot): drop the
                    // stale sidecar and open the pristine backing file.
                    m_recoveryStore.clear(path);
                }
            }
        }

        if (captureDpr > 0.0) {
            // Stamp the real screen dpr onto capture-origin images so the
            // viewer treats device px as logical px / dpr, opens at
            // logical size, and defaults to pixel-exact Actual Size.
            if (auto *img = dynamic_cast<ImageDocument *>(doc.get()))
                img->markCaptureOrigin(captureDpr);
        }

        // Transient import (clipboard / screenshot): the doc is backed
        // only by a temp file the user never chose, so mark it untitled
        // — it stays clean but must prompt Save-As on close rather than
        // vanishing silently. Only raster imports use this path today.
        if (markUntitled) {
            if (auto *img = dynamic_cast<ImageDocument *>(doc.get()))
                img->markUntitled();
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
                // CF-5: the FIRST file reuses an empty launch window
                // if one is available (consume-once), then subsequent
                // files spawn fresh.
                target = takeReuseOrFresh();
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
        // Never record a transient import's temp path in Recent Files: the
        // user never chose that location and the file is subject to OS
        // cleanup, so a Recent entry pointing at it would dangle. Once the
        // user Save-As's the untitled doc to a real destination the normal
        // save flow records that chosen path.
        if (!markUntitled)
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

bool Application::requestQuit(QuitMode mode) {
    // Decision-record refinement (2026-07-19): the EXPLICIT menu commands are
    // decoupled from the OS `NSQuitAlwaysKeepsWindows` setting. That setting
    // governs only macOS's own automatic window auto-restoration; it does NOT
    // flip what ⌘Q / ⌥⌘Q do here. So the mapping is fixed and honest:
    //   QuitMode::KeepWindows (⌥⌘Q) — ALWAYS keeps, NEVER prompts.
    //   QuitMode::Normal      (⌘Q)  — ALWAYS runs the per-doc Save/Discard/
    //                                 Cancel prompt.
    // (The m_quitKeepsWindowsProbe seam is retained — it still feeds the
    // native macOS chrome and the decoupling regression test — but it no
    // longer changes which branch an explicit command takes.)
    if (mode == QuitMode::KeepWindows) {
        // Fresh snapshot cache per quit attempt: canDraftForKeep populates it
        // with the proven-producible structural-PDF bytes below, and a stale
        // entry keyed by a since-freed doc pointer must never be reused.
        m_keepStructuralSnapshotCache.clear();
        // KeepWindows keeps what it can draft and prompts for anything dirty
        // it cannot (ADR-0004 no-silent-loss floor). An unsaved/untitled
        // document that is NOT losslessly draftable — a pathless PDF, an image
        // with a null raster — would otherwise be stored as a clean
        // {kind:"path"} reference and restore with its edits gone. For exactly
        // those docs we raise the Normal per-doc Save/Discard/Cancel prompt
        // BEFORE quitting, so their edits are saved or explicitly discarded.
        // Draftable docs — images, and PDFs whether annotation-dirty
        // (AnnotatedPath) or structurally edited (StructuralDraft) — are kept
        // silently with no prompt.
        for (MainWindow *win : windows()) {
            if (!win)
                continue;
            for (IDocument *doc : win->collectDirtyDocsForQuit()) {
                if (!canDraftForKeep(doc)) {
                    if (!win->confirmCloseForQuit(doc))
                        return false; // Cancel / failed save: keep all, write nothing
                }
            }
        }

        // Serialize the open-window set (including the remaining unsaved /
        // untitled image bytes) so the next launch restores it. The save is
        // atomic: a failure leaves any prior valid session intact. If it
        // fails we must NOT silently quit and lose the still-unsaved drafts —
        // fall back to the Normal prompt path so nothing is dropped.
        const QList<SessionWindowDescriptor> session = captureSessionForKeep();
        if (!m_draftStore.save(session)) {
            if (!promptDirtyDocsForQuit())
                return false; // aborted — keep every document
            m_draftStore.clear();
        } else {
            // The kept blob/draft/path now supersedes any autosave recovery
            // sidecar (#90) for each kept doc with an on-disk original: it is
            // the same-or-newer state, re-opened dirty on restore. Clear those
            // stale sidecars so a later File→Open of the original does not
            // resurrect superseded pre-keep state via pendingRecovery(). The
            // backing file itself is never touched (RecoveryStore::clear only
            // removes the sidecar + index entry).
            clearRecoverySidecarsFor(session);
        }
        m_keepStructuralSnapshotCache.clear();
        if (m_performQuit)
            m_performQuit();
        return true;
    }

    // Normal: prompt to save/name every unsaved or untitled document, one
    // at a time, across every window. Cancel (or a failed save) on any
    // prompt aborts the quit with everything still open and nothing written.
    if (!promptDirtyDocsForQuit())
        return false; // aborted — keep every document, write nothing
    // Every document resolved (saved or discarded). Drop any stale kept-
    // windows draft so a later launch doesn't resurrect a quit we cleaned.
    m_draftStore.clear();
    if (m_performQuit)
        m_performQuit();
    return true;
}

bool Application::canDraftForKeep(IDocument *doc) const {
    // "Draftable" == the kept-windows capture can persist this document's
    // unsaved state with NO user interaction, so ⌥⌘Q keeps it silently
    // instead of falling back to the ADR-0004 per-doc prompt.
    //
    //  * Image doc with a non-null raster — captured byte-for-byte as PNG.
    //  * PDF whose dirtiness is ANNOTATION-ONLY and which has an on-disk
    //    path — captured as its original path plus a JSON payload of the
    //    unsaved annotations, re-applied editable + dirty on restore
    //    (restoreAnnotationsFromDraft).
    //  * PDF with STRUCTURAL page edits (rotate/delete/move/crop/insert —
    //    hasStructuralEdits()) and an on-disk path — captured as a
    //    StructuralDraft: the edited-PDF BLOB bytes (writeRecoverySnapshot)
    //    plus the original path, loaded back into a private editor copy on
    //    restore (recoverFrom) with Save re-pointed to the original and the
    //    doc marked dirty. The qpdf command list cannot be serialized (its
    //    commands hold live in-memory object handles), so we persist the
    //    edited bytes instead. See
    //    docs/backlog/2026-07-19-structural-pdf-keep-fidelity.md.
    //
    // A pathless PDF cannot be reopened from / re-associated to disk, so it is
    // not draftable and falls back to the per-doc prompt.
    //
    // Two further structural-PDF exclusions keep the never-worry-save floor
    // honest — both routing to the per-doc prompt rather than a silent keep:
    //   FIX 2 — a structural PDF carrying a PENDING redaction/signature
    //     annotation is NOT silently draftable: writeRecoverySnapshot() bakes
    //     those in destructively, so a silent keep would return them BURNED IN
    //     and un-editable (a silent irreversible commit in a no-prompt flow).
    //   FIX 1 — a structural PDF whose snapshot cannot actually be produced is
    //     NOT draftable: we PROVE producibility here by serializing the bytes,
    //     and cache them for captureSessionForKeep so it does not re-serialize.
    //     If the snapshot fails/empty, returning false here makes requestQuit
    //     prompt the doc instead of silently omitting it from the capture.
    if (auto *img = dynamic_cast<ImageDocument *>(doc))
        return !img->image().isNull();
    if (auto *pdf = dynamic_cast<PdfDocument *>(doc)) {
        if (!pdf->isDirty() || pdf->filePath().isEmpty())
            return false;
        if (!pdf->hasStructuralEdits())
            return true; // annotation-only → AnnotatedPath (no blob needed)
        // Already proven snapshottable earlier in this same ⌥⌘Q flow.
        if (m_keepStructuralSnapshotCache.contains(doc))
            return true;
        if (pdf->hasPendingDestructiveAnnotation())
            return false; // FIX 2 — would burn the redaction/signature in
        const QByteArray bytes = snapshotStructuralPdfBytes(pdf);
        if (bytes.isEmpty())
            return false; // FIX 1 — unsnapshottable → prompt, never drop
        m_keepStructuralSnapshotCache.insert(doc, bytes);
        return true;
    }
    return false;
}

bool Application::promptDirtyDocsForQuit() {
    for (MainWindow *win : windows()) {
        if (!win)
            continue;
        for (IDocument *doc : win->collectDirtyDocsForQuit()) {
            if (!win->confirmCloseForQuit(doc))
                return false; // Cancel / failed save aborts the quit
        }
    }
    return true;
}

void Application::clearRecoverySidecarsFor(const QList<SessionWindowDescriptor> &session) {
    for (const SessionWindowDescriptor &wd : session) {
        for (const SessionDocDescriptor &dd : wd.docs) {
            // The on-disk original differs by kind: Path / AnnotatedPath carry
            // it in `path`; Draft (titled image) / StructuralDraft in
            // `originalPath`. An untitled Draft has neither — nothing to clear.
            QString original;
            switch (dd.kind) {
            case SessionDocDescriptor::Kind::Path:
            case SessionDocDescriptor::Kind::AnnotatedPath:
                original = dd.path;
                break;
            case SessionDocDescriptor::Kind::Draft:
            case SessionDocDescriptor::Kind::StructuralDraft:
                original = dd.originalPath;
                break;
            }
            if (!original.isEmpty())
                m_recoveryStore.clear(original);
        }
    }
}

QList<SessionWindowDescriptor> Application::captureSessionForKeep() const {
    QList<SessionWindowDescriptor> out;
    for (MainWindow *win : windows()) {
        if (!win)
            continue;
        SessionWindowDescriptor wd;
        const int total = win->documentCount();
        for (int i = 0; i < total; ++i) {
            IDocument *doc = nullptr;
            if (!win->documentAt(i, &doc) || !doc)
                continue;

            auto *img = dynamic_cast<ImageDocument *>(doc);
            auto *pdf = dynamic_cast<PdfDocument *>(doc);
            // hasUnsavedWork() (not bare isDirty()) so a CLEAN image doc whose
            // backing file was DELETED underneath (CF-7) is DRAFTED here — its
            // in-memory raster is the last copy. Without this it would fall to
            // the else-branch and be stored as a {kind:"path"} reference to a
            // file that no longer exists, silently losing the raster on
            // restore. isUntitled() keeps #78's untitled-draft case.
            const bool dirtyOrUntitled = doc->isUntitled() || doc->hasUnsavedWork();
            // Only draft a dirty/untitled doc we can capture without a prompt.
            // A dirty non-draftable doc has already been resolved (saved or
            // discarded) by requestQuit's prompt fallback, so by here it is
            // either clean-on-disk (→ Path) or was discarded (its edits are
            // meant to be gone). We never store a still-dirty doc we cannot
            // capture as a clean Path silently — that path is unreachable
            // post-prompt.
            const bool needsImageDraft = dirtyOrUntitled && img && canDraftForKeep(doc);
            // A dirty PDF (with an on-disk path) is captured one of two ways:
            //  * STRUCTURAL edits present → a StructuralDraft: the edited-PDF
            //    blob bytes plus the original path (the qpdf command list is
            //    not serializable, so we persist the bytes).
            //  * annotation-ONLY dirty → an AnnotatedPath: the on-disk path
            //    plus the unsaved annotations (re-applied editable + dirty on
            //    restore).
            const bool isDraftablePdf = dirtyOrUntitled && pdf && !img && canDraftForKeep(doc);
            const bool needsPdfStructuralDraft = isDraftablePdf && pdf->hasStructuralEdits();
            const bool needsPdfAnnotationDraft = isDraftablePdf && !pdf->hasStructuralEdits();
            SessionDocDescriptor dd;
            if (needsPdfStructuralDraft) {
                // Reuse the edited-PDF bytes canDraftForKeep already proved
                // producible (and cached) for this doc in this same ⌥⌘Q flow,
                // so we serialize the (possibly large) document only once. On a
                // cache miss re-serialize as a fallback; writeRecoverySnapshot
                // does NOT mutate the live document.
                QByteArray bytes = m_keepStructuralSnapshotCache.value(doc);
                if (bytes.isEmpty())
                    bytes = snapshotStructuralPdfBytes(pdf);
                if (bytes.isEmpty()) {
                    // Could not snapshot a structurally-edited PDF. Do NOT
                    // silently drop it (storing a clean Path ref would lose the
                    // edits with no prompt). canDraftForKeep PROVES the snapshot
                    // up front and returns false when it cannot — so requestQuit
                    // has already prompted this doc and it is not reached here in
                    // practice; if it ever fires, skip rather than mis-capture.
                    continue;
                }
                dd.kind = SessionDocDescriptor::Kind::StructuralDraft;
                dd.bytes = bytes;
                dd.format = QStringLiteral("pdf");
                dd.originalPath = doc->filePath();
            } else if (needsPdfAnnotationDraft) {
                dd.kind = SessionDocDescriptor::Kind::AnnotatedPath;
                dd.path = doc->filePath();
                const std::vector<Annotation> &live = pdf->annotations()->annotations();
                dd.annotations = QList<Annotation>(live.begin(), live.end());
            } else if (needsImageDraft) {
                // Persist the raster's exact bytes so an unsaved/untitled
                // window returns byte-for-byte. PNG is lossless, so the
                // round-trip preserves every pixel.
                QByteArray bytes;
                QBuffer buffer(&bytes);
                bool encoded = buffer.open(QIODevice::WriteOnly) &&
                               img->image().save(&buffer, "PNG");
                buffer.close();
                if (!encoded || bytes.isEmpty()) {
                    // Could not encode a dirty/untitled doc's bytes. Do NOT
                    // silently drop it — skipping here would lose its content
                    // with no prompt. canDraftForKeep() gates on a non-null
                    // image, so in practice this is unreachable; if it ever
                    // fires, prefer keeping the whole capture honest by
                    // aborting the draft for this doc via the prompt path.
                    // (Belt-and-braces: requestQuit already prompted for the
                    // non-draftable case; a null-image image-doc would have
                    // failed canDraftForKeep and been prompted too.)
                    continue;
                }
                dd.kind = SessionDocDescriptor::Kind::Draft;
                dd.bytes = bytes;
                dd.format = QStringLiteral("png");
                dd.untitled = doc->isUntitled();
                dd.originalPath = doc->isUntitled() ? QString() : doc->filePath();
                // Persist the HiDPI restore state: a PNG blob does not carry
                // Qt's devicePixelRatio, so without this an unsaved Retina
                // screenshot (dpr 2, capture-origin) restores double-sized.
                dd.devicePixelRatio = img->image().devicePixelRatio();
                dd.captureOrigin = img->isCaptureOrigin();
            } else if (!doc->filePath().isEmpty()) {
                // A titled doc stored as a clean Path ref, reopened from disk.
                // Reaching here for a dirty non-draftable doc is only possible
                // AFTER requestQuit's prompt fallback resolved it: a Save left
                // it clean-on-disk (Path is exact), a Discard means its edits
                // are intentionally gone (Path reopens the on-disk version).
                // Non-dirty saved docs land here directly. Either way no
                // unsaved content is silently lost.
                dd.kind = SessionDocDescriptor::Kind::Path;
                dd.path = doc->filePath();
            } else {
                continue; // nothing persistable (e.g. a path-less non-image)
            }
            wd.docs.append(dd);
        }
        if (!wd.docs.isEmpty())
            out.append(wd);
    }
    return out;
}

bool Application::restoreKeptWindows() {
    if (!m_draftStore.hasSession())
        return false;

    const QList<SessionWindowDescriptor> descriptors = m_draftStore.restore();
    bool anyOpened = false;
    for (const SessionWindowDescriptor &wd : descriptors) {
        MainWindow *win = nullptr;
        for (const SessionDocDescriptor &dd : wd.docs) {
            std::unique_ptr<IDocument> doc;
            if (dd.kind == SessionDocDescriptor::Kind::Draft) {
                QImage img;
                if (!img.loadFromData(dd.bytes, dd.format.toLatin1().constData()))
                    continue;
                // A PNG blob does not carry Qt's devicePixelRatio, so re-stamp
                // the persisted dpr before handing the image to the document —
                // otherwise a Retina screenshot (dpr 2) restores at dpr 1 and
                // renders double its logical size.
                img.setDevicePixelRatio(dd.devicePixelRatio > 0.0 ? dd.devicePixelRatio : 1.0);
                auto imgDoc = std::make_unique<ImageDocument>(QString());
                imgDoc->restoreFromDraft(img, dd.originalPath, dd.untitled,
                                         /*dirty=*/!dd.untitled, dd.captureOrigin);
                doc = std::move(imgDoc);
            } else if (dd.kind == SessionDocDescriptor::Kind::StructuralDraft) {
                // A structurally-edited PDF, restored from its self-sufficient
                // edited-blob bytes. Materialize the blob to a temp; recoverFrom
                // makes its OWN owned private copy, so this temp is safe to drop
                // once it returns. The original file is never touched.
                ScopedTempFile blobFile(QStringLiteral("trailer-keep-restore-XXXXXX.pdf"));
                if (!blobFile.isValid())
                    continue;
                {
                    QFile f(blobFile.path());
                    if (!(f.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
                          f.write(dd.bytes) == dd.bytes.size()))
                        continue;
                    f.close();
                }
                const bool haveOriginal =
                    !dd.originalPath.isEmpty() && QFileInfo::exists(dd.originalPath);
                if (haveOriginal) {
                    // Reopen the ORIGINAL (so m_path points at it and Save
                    // re-associates there), then load the edited blob into a
                    // private editor copy via recoverFrom — which re-points Save
                    // at the original, repopulates annotations as editable, and
                    // marks the doc dirty.
                    auto opened = m_registry.open(dd.originalPath);
                    auto *pdf = dynamic_cast<PdfDocument *>(opened.get());
                    if (!pdf || !pdf->recoverFrom(blobFile.path()))
                        continue; // could not rebuild the edited graph → drop
                    doc = std::move(opened);
                    m_recent.add(dd.originalPath);
                } else {
                    // The original was MOVED or DELETED since ⌥⌘Q. The blob is
                    // the COMPLETE edited PDF and does not need the original to
                    // render (the original was only the Save re-association
                    // target), so DON'T silently discard the captured work:
                    // open the blob and return it as an UNTITLED, dirty doc
                    // whose first Save prompts Save-As — mirroring how the image
                    // Draft branch restores bytes as an untitled doc.
                    auto opened = std::make_unique<PdfDocument>(blobFile.path());
                    if (!opened->recoverFrom(blobFile.path())) // owns a private copy
                        continue;
                    opened->markUntitledForRecovery();
                    doc = std::move(opened);
                }
            } else if (dd.kind == SessionDocDescriptor::Kind::AnnotatedPath) {
                // A PDF reopened from disk with its unsaved annotations
                // re-applied as editable objects and marked dirty — so it
                // returns exactly as it was at ⌥⌘Q: same file, same edits,
                // still unsaved. If the file is gone we drop just this doc.
                if (dd.path.isEmpty() || !QFileInfo::exists(dd.path))
                    continue;
                doc = m_registry.open(dd.path);
                if (auto *pdf = dynamic_cast<PdfDocument *>(doc.get()))
                    pdf->restoreAnnotationsFromDraft(dd.annotations, /*dirty=*/true);
                m_recent.add(dd.path);
            } else {
                if (dd.path.isEmpty() || !QFileInfo::exists(dd.path))
                    continue;
                doc = m_registry.open(dd.path);
                m_recent.add(dd.path);
            }
            if (!doc)
                continue;
            if (!win)
                win = ensureFreshWindow();
            win->addDocument(std::move(doc));
            anyOpened = true;
        }
    }
    m_recent.save();
    notifyWindowsRecentChanged();

    // Consume the store on a successful restore so the kept session is a
    // one-shot — a subsequent launch falls back to the path-list session.
    m_draftStore.clear();
    return anyOpened;
}

bool Application::restorePreviousSession() {
    // A kept-windows draft store wins: it carries unsaved/untitled content
    // the path-list session cannot, and is consumed on restore.
    if (restoreKeptWindows())
        return true;
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
    connect(wholeScreen, &QAction::triggered, this, [this, wholeScreen, captureContext]() {
        // Re-entrancy guard. On Wayland the portal capture spins a nested
        // event loop (PortalScreenshot.cpp) that keeps this menu live, so a
        // user could click Whole Screen again mid-capture and stack a second
        // portal request / nested loop. Disable the action for the duration
        // and restore it after. The QPointer covers the case where the capture
        // context (and hence this menu + action) is destroyed inside the
        // nested loop — we then simply don't touch the freed action. On
        // X11/Windows there is no nested loop, so this is a harmless no-op.
        QPointer<QAction> guard(wholeScreen);
        guard->setEnabled(false);
        captureScreenshot(ShotMode::Screen, captureContext);
        if (guard)
            guard->setEnabled(true);
    });

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

    // Wayland whole-screen capture needs the XDG screenshot portal (a
    // client-side grab yields only a null pixmap there). When no portal is
    // running, disable Whole Screen with an explaining tooltip rather than
    // letting the click silently produce nothing (G3; backlog
    // 2026-07-12-wayland-screenshot-portal). Re-evaluated on every open so a
    // portal that starts mid-session re-enables the item. On X11/Windows this
    // is a no-op — isWaylandSession() is false, so the item stays enabled.
    auto refreshWholeScreen = [wholeScreen]() {
        const bool ok = !trailer::isWaylandSession() || trailer::portalScreenshotAvailable();
        wholeScreen->setEnabled(ok);
        wholeScreen->setToolTip(
            ok ? QString()
               : tr("Screenshot capture needs a desktop screenshot portal, which isn't running."));
    };
    refreshWholeScreen();
    connect(screenshotMenu, &QMenu::aboutToShow, this, refreshWholeScreen);
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
            openFiles({path}, /*markUntitled=*/true);
            return;
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
    // Opt-in ScreenCaptureKit picker backend (macOS 14+). The system picker
    // selects a window or display, so it substitutes for the interactive
    // Screen / Window modes but NOT for a freeform Region rectangle — Region
    // (and any picker Unavailable/Failed) falls through to the screencapture
    // path below. On a clean pick we're done; a picker cancel is a
    // self-caused no-op (say nothing, per PHILOSOPHY → No popup that just
    // says "no").
    //
    // The picker uses the system picker itself as the consent surface, so it
    // is intentionally NOT gated by the Screen-Recording TCC preflight that
    // guards the screencapture shell-out below (different consent model —
    // docs/decision-records/2026-07-16-capture-permission-preflight.md).
    const auto backend = trailer::effectiveCaptureBackend(
        m_settings.captureBackend(), trailer::screenCaptureKitAvailable(),
        /*freeformRegion=*/mode == ShotMode::Region);
    if (backend == trailer::CaptureBackend::ScreenCaptureKit) {
        QString err;
        const auto r = trailer::captureViaPickerToPng(
            path, /*wholeDisplay=*/mode == ShotMode::Screen, &err);
        if (r == trailer::PickerCaptureResult::Ok) {
            openFiles({path});
            return;
        }
        if (r == trailer::PickerCaptureResult::Cancelled)
            return;
        // Unavailable/Failed -> fall through to the screencapture path.
        qWarning() << "Application: ScreenCaptureKit picker capture failed, falling back to"
                   << "screencapture:" << err;
    }

    // --- screencapture shell-out path (TCC-gated by PR #77) ---
    // Preflight the live Screen Recording TCC state before touching the OS
    // selection UI (the screen-capture preflight ADR). This gates ONLY the
    // /usr/sbin/screencapture shell-out; the picker path above is intentionally
    // ungated (different consent model). The pre-permission explainer is retired
    // for stills (owner decision 2026-07-17); we lean on the OS Screen Recording
    // prompt directly.
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
    // Linux/BSD/Windows: only whole-screen capture is supported. The
    // Window / Selected-Area items are disabled in the UI on this
    // platform, so `mode` should already be Screen here.
    if (mode != ShotMode::Screen)
        return;

    // Pick the capture path from the session type + portal availability.
    // X11/xcb/Windows -> QScreen::grabWindow (a client-side root grab works).
    // Wayland -> the XDG screenshot portal (grabWindow returns null there);
    // Wayland with no portal -> honest-degrade, never a silent null (G3,
    // backlog 2026-07-12-wayland-screenshot-portal).
    switch (trailer::chooseLinuxScreenshotBackend(trailer::isWaylandSession(),
                                                  trailer::portalScreenshotAvailable())) {
    case trailer::LinuxScreenshotBackend::Portal: {
        QString err;
        // capturePortalScreenshotToPng runs a nested QEventLoop that DOES
        // dispatch DeferredDelete, so `context` (the triggering MainWindow)
        // can be destroyed while we wait for the portal. Hold it in a QPointer
        // captured BEFORE the call and re-check it before any post-call
        // dereference; if the window is gone there is nothing to flash to, so
        // abort quietly. (openFiles() below does not touch `context`, so the
        // success path is unaffected by a mid-capture close.)
        QPointer<QWidget> ctxGuard(context);
        const auto r = trailer::capturePortalScreenshotToPng(path, /*interactive=*/false, &err);
        if (r == trailer::PortalCaptureResult::Cancelled)
            return; // user dismissed the portal prompt — a self-caused no-op
        if (r != trailer::PortalCaptureResult::Ok) {
            // The portal was advertised as available but the capture failed.
            // Surface it — do not fall back to a grabWindow that would only
            // yield a null pixmap under Wayland.
            qWarning() << "Application: portal screenshot failed:" << err;
            if (auto *mw = qobject_cast<MainWindow *>(ctxGuard.data()))
                mw->flashError(tr("Screenshot capture via the desktop portal failed."));
            return;
        }
        // Portal wrote the PNG to `path`; the portal image already carries the
        // compositor's pixel dimensions, so no dpr recovery is applied here.
        break;
    }
    case trailer::LinuxScreenshotBackend::QScreenGrab: {
        QScreen *screen = QGuiApplication::primaryScreen();
        if (!screen)
            return;
        const QPixmap shot = screen->grabWindow(0);
        if (shot.isNull() || !shot.save(path, "PNG"))
            return;
        // grabWindow() stamps the screen dpr on the pixmap, but the PNG save
        // drops it; recover it so a HiDPI capture opens 1:1 (see openFiles).
        const double dpr =
            shot.devicePixelRatio() > 0.0 ? shot.devicePixelRatio() : screen->devicePixelRatio();
        setPendingCaptureDpr(dpr);
        break;
    }
    case trailer::LinuxScreenshotBackend::Unavailable:
        // Wayland with no screenshot portal. The menu item is disabled +
        // tooltipped for this state, but a programmatic/edge trigger can still
        // land here — degrade honestly rather than emit a silent null (G3).
        if (auto *mw = qobject_cast<MainWindow *>(context))
            mw->flashError(tr("Screenshot capture needs a desktop screenshot portal, "
                              "which isn't running."));
        return;
    }
#endif

    openFiles({path}, /*markUntitled=*/true);
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
    connect(quitAction, &QAction::triggered, this,
            [this]() { requestQuit(QuitMode::Normal); });

    // "Quit and Keep Windows" (⌥⌘Q). The functional accelerator works
    // here even without the native in-place Option swap; QuitMenu installs
    // that visual alternate on top (a display nicety only).
    auto *keepAction = fileMenu->addAction(tr("Quit and Keep Windows"));
    keepAction->setShortcut(QKeySequence(Qt::MetaModifier | Qt::AltModifier | Qt::Key_Q));
    connect(keepAction, &QAction::triggered, this,
            [this]() { requestQuit(QuitMode::KeepWindows); });

    m_noWindowMenuBar = bar;
    QuitMenu::installAlternateKeepItem(quitAction, keepAction);
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
