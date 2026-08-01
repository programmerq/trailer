#pragma once

#include "document/IDocument.h"
#include "ui/IconHelper.h"

#include <QActionGroup>
#include <QHash>
#include <QMainWindow>
#include <QSet>
#include <QStringList>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

class QCloseEvent;
class QResizeEvent;
class QTimer;

class QAction;
class QGraphicsOpacityEffect;
class QLabel;
class QMenu;
class QPropertyAnimation;
class QStackedWidget;
class QToolButton;

namespace trailer {

class AnimationBar;
class Application;
class DocumentView;
class EmptyStateWidget;
class ExternalChangeMonitor;
class FileChangeBanner;
class Inspector;
class FormToolbar;
class Magnifier;
class MarkupToolbar;
class SearchBar;
class Sidebar;

class CancellationToken;
class MlProgressWidget;
class OcrController;
class SamController;

// Resolve the pages to OCR for a Recognize Text request when the choice is
// unambiguous, so a dialog offering no real choice can be skipped. Returns
// the single-page set {currentPage} for a one-page document; returns
// nullopt for multi-page documents, signalling the caller to present
// RecognizeTextDialog for a page-range pick. Free function (no MainWindow
// instance) so it is unit-testable headlessly.
std::optional<std::vector<int>> resolveRecognizePages(const IDocument &doc);

class MainWindow : public QMainWindow {
    Q_OBJECT

  public:
    explicit MainWindow(Application *app, QWidget *parent = nullptr);
    Application *app() const { return m_app; }

    void addDocument(std::unique_ptr<IDocument> document);
    int documentCount() const;
    // Pass-through to DocumentView::documentAt. Returns 1 on success
    // and writes the document pointer through `out`; returns 0 with
    // `*out = nullptr` on out-of-range or missing widget. Used by
    // Application::onAboutToQuit to enumerate open paths.
    int documentAt(int index, IDocument **out) const;
    // Index of the current tab in this window's DocumentView, or -1 if
    // no tabs are open. Used by the diagnostics/feedback report to
    // record which open document the user was looking at.
    int currentDocumentIndex() const;

    // Read-only chrome-visibility accessors for the diagnostics/feedback
    // report (src/diagnostics/FeedbackReport.cpp). Each mirrors the
    // QWidget::isVisible() of the corresponding chrome piece.
    bool isSidebarVisible() const;
    bool isMarkupToolbarVisible() const;
    bool isFormToolbarVisible() const;
    // Current tab's document-view VIEWPORT size, in logical pixels — the
    // area fit-to-window math (FitInView/FitToWidth) actually sizes
    // against, distinct from the window's outer geometry (which also
    // includes toolbars/menus/chrome). Empty when there is no current
    // document, or its view widget isn't a QAbstractScrollArea (the stub
    // adapter's placeholder). Read-only accessor for the diagnostics/
    // feedback report, same pattern as the visibility getters above.
    QSize currentDocumentViewportSize() const;

    // Lightweight status-bar feedback. Replaces operation-failure
    // QMessageBox::warning calls so the user is not punched in the
    // face by a modal every time a crop/save/export fails. Errors
    // carry a longer timeout (12 s) so a user with their eyes on the
    // document still has time to notice; success / neutral messages
    // fade quickly. The error helper prefixes the message with a
    // warning glyph; the success helper with a check.
    void flashError(const QString &message);
    void flashSuccess(const QString &message);
    void flashStatus(const QString &message);

    // Test-only seam for the unsaved-changes close prompt. The offscreen
    // UAT harness cannot click a real modal, so it forces the outcome of
    // confirmCloseDirtyDoc() to Save / Discard / Cancel; Prompt (the
    // default) shows the real QMessageBox in a headed session. See
    // confirmCloseDirtyDoc().
    enum class CloseResponse { Prompt, Save, Discard, Cancel };
    void setCloseResponseForTesting(CloseResponse r) { m_closeResponseForTesting = r; }
    // Test-only seam for the Save-As destination. When non-empty,
    // chooseSaveAsPath() returns this instead of showing the native file
    // dialog, letting the harness drive the untitled-document Save flow.
    void setSaveAsPathForTesting(const QString &path) { m_saveAsPathForTesting = path; }
    // Test-only seam (DR 2026-07-31-transient-zoom-readout): shrinks the
    // zoom indicator's visible-hold and fade durations so UAT coverage of
    // the reveal/fade sequence runs on fake, fast, deterministic timing
    // instead of sleeping through the real hand-tuned (~1.5s) production
    // duration. See kZoomIndicatorVisibleMs/-FadeMs in the constructor.
    void setZoomIndicatorTimingForTesting(int visibleMs, int fadeMs) {
        m_zoomIndicatorVisibleMs = visibleMs;
        m_zoomIndicatorFadeMs = fadeMs;
    }

    // Test seam (ADR 0002 review item 13; no-op in production). When set,
    // replaces the ensureOcrModelsReady() call made when the user activates
    // the missing-model in-context hint link, so a test can prove the
    // click→download-consent routing without spawning a real modal or a
    // network download. Returns whether the model is (now) ready.
    void setOcrModelDownloadHookForTesting(std::function<bool()> hook) {
        m_ocrModelDownloadHook = std::move(hook);
    }

    // Test seam for the Remove Background op lifecycle. When set, onRemove-
    // Background() runs this override INSTEAD of the real BackgroundRemover
    // and skips the model-download pre-flight, so the menu-glyph state
    // machine + cancellation + byte-preservation can be exercised without a
    // network-downloaded ONNX model. The override runs on the MlScheduler
    // worker thread; poll the token exactly as the real remover does. Return
    // a null QImage to simulate cancel/failure. nullptr in production.
    void setBackgroundRemoveFnForTesting(
        std::function<QImage(const QImage &, const CancellationToken *)> fn) {
        m_bgRemoveFnOverride = std::move(fn);
    }

    // Test-only introspection for the pointer-keyed large-doc notice /
    // pageHasText caches (Copilot review #58). Both are keyed by the raw
    // IDocument* and must be purged when a document closes so a recycled
    // address can't inherit a prior dismissal or a stale probe hit. These
    // let a UAT prove the documentAboutToBeRemoved hook drops the entry.
    bool isLargeDocOcrHintDismissedForTesting(const IDocument *doc) const {
        return m_largeDocOcrHintDismissed.contains(const_cast<IDocument *>(doc));
    }
    bool pageHasTextCacheHasDocForTesting(const IDocument *doc) const {
        return m_pageHasTextCacheDoc == doc;
    }
    // Test-only access to the auto-OCR controller so a UAT can inject a
    // recording recognizer + force model-ready and then drive the menu-level
    // force-rerun affordance end-to-end (onRerunRecognizeText →
    // submitUserPages forceRerun=true). Never used in production.
    OcrController *ocrControllerForTesting() const { return m_ocrController; }

    // Quit-time support for Application::requestQuit(QuitMode::Normal).
    // collectDirtyDocsForQuit returns this window's documents that need a
    // save/name prompt at quit (dirty OR untitled), current-document-first
    // so the user usually sees only one prompt. confirmCloseForQuit runs
    // the ADR-0004 Save/Discard/Cancel prompt for one document, returning
    // false on Cancel or a failed save (Application aborts the quit). Both
    // reuse the same machinery as the window-close path.
    std::vector<IDocument *> collectDirtyDocsForQuit() const;
    bool confirmCloseForQuit(IDocument *doc) { return confirmCloseDirtyDoc(doc); }

    // Test-only: the "Quit and Keep Windows" (⌥⌘Q) action, so a headless
    // test can assert its shortcut and that triggering it routes to the
    // KeepWindows quit path without a real menu event.
    QAction *quitKeepWindowsActionForTesting() const { return m_quitKeepWindowsAction; }

    // Honest terminal message for a finished Recognize Text batch. Cancelled
    // batches report the no-changes-saved message; otherwise the message is
    // truthful about whether any text was actually recognized — a zero-block
    // run says "No text found" rather than falsely claiming completion.
    // Static + public so it is unit-testable without a MainWindow instance.
    static QString recognizeCompletionMessage(bool cancelled, int blockCount);

    // Re-tint every themed toolbar/menu icon this window owns (its own
    // binder plus the markup/form toolbars) from the current palette, and
    // refresh the dynamic background-removal badge. Called by Application
    // after a live theme (colour-scheme) change so icons stay legible when
    // the app switches light↔dark without a restart.
    void refreshThemedIcons();

  public slots:
    void rebuildRecentMenu();
    // Save every dirty document with an established file path. Wired
    // to a 30 s timer; exposed as a public slot so tests can drive
    // the same code path without waiting for the timer.
    void autoSaveDirtyDocs();

  private slots:
    // Forward an AnnotationOverlay selection change to the Inspector
    // pane. Connected with Qt::UniqueConnection per-overlay in
    // onCurrentDocumentChanged so the Inspector always tracks the
    // overlay belonging to the current document.
    void onAnnotationSelectionChanged(int id);
    // Auto-switch the markup toolbar back to Select after a one-shot
    // shape is drawn. Without this the toolbar stays on (say)
    // Rectangle, so a follow-up click on the just-drawn shape would
    // create a second overlapping rectangle instead of selecting the
    // first. 2026-05-20 HITL pass.
    void onAnnotationCommitted(int id);

  protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    // Prompt to save / discard / cancel for every dirty document in
    // this window before the OS-level close completes. If the user
    // hits Cancel on any prompt, the window stays open.
    void closeEvent(QCloseEvent *event) override;
    // Escape clears the Magnifier (and other transient "modes").
    // Most modes are toggle actions so the user can flip them off
    // explicitly; Escape is the universal "I'm done with this
    // mode" affordance Mac apps lean on. Falls through to the base
    // implementation when nothing matches.
    void keyPressEvent(QKeyEvent *event) override;
    // Track window-state transitions so the Window-menu maximize/restore
    // action can retitle itself ("Maximize" ↔ "Restore" on Win/Linux;
    // static "Zoom" on macOS). Falls through to the base implementation.
    void changeEvent(QEvent *event) override;
    // Reposition the floating zoom-% HUD (m_zoomIndicator) so it stays
    // anchored to the bottom-right of the document area across window
    // resizes. See repositionZoomIndicator().
    void resizeEvent(QResizeEvent *event) override;
    // Suppress Qt's built-in right-click toolbar/dock context menu.
    // That menu lets the user accidentally hide a toolbar with no
    // discoverable way to bring it back — the source of the
    // "I'm tired of re-enabling toolbars" frustration. Visibility
    // lives in the View menu (and the main toolbar toggles); this
    // is the single source of truth.
    QMenu *createPopupMenu() override;

  private slots:
    void onOpen();
    // Handle DocumentView::allTabsClosed under the empty-state window
    // model: close this window if other windows exist (avoid empty-
    // window pile-up), otherwise persist it and show the empty state.
    void onAllTabsClosed();
    // Swap the central stack between the document page and the empty
    // state based on whether any document is open.
    void updateEmptyState();
    // Wipe every Trailer-managed file under AppPaths::*. Used by
    // Tools → Reset Trailer Settings…  for the "is this stale
    // state from an older build?" diagnostic. Asks for explicit
    // confirmation because this is destructive.
    void onResetTrailerSettings();
    // Open the unified Preferences dialog (Edit → Preferences…).
    void onOpenPreferences();
    void onSave();
    void onSaveAs();
    void onRotateLeft();
    void onRotateRight();
    void onFlipHorizontal();
    void onFlipVertical();
    void onAdjustSize();
    void onAdjustColour();
    void onRemoveBackground();
    void onInstantAlpha();
    void onSmartLasso();
    void onRecognizeText();
    // Force re-run of text recognition for a single-page document. The
    // Recognize Text… dialog — the only place the force-rerun checkbox
    // lives — is skipped for single-page docs, so this entry is the only
    // way to re-recognise a page whose OCR landed non-empty-but-wrong text
    // (watermark noise, a stray glyph). Routes to the same invalidate-then-
    // OCR path as the dialog checkbox (submitUserPages forceRerun=true).
    // Backlog: docs/backlog/2026-07-15-single-page-force-rerun.md.
    void onRerunRecognizeText();
    void onExportAs();
    void onCopyPageAsImage();
    void onExportPasswordProtected();
    void onReduceFileSize();
    void onTakeScreenshot();
    void onCropImage();
    void onInsertPages();
    void onCropPages();
    // Activate the on-page drag-to-crop tool (direct-manipulation crop).
    void onCropPagesByDragging();
    // Apply a crop from the overlay's page-space keep-rectangle.
    void onCropRectCommitted(const QRectF &docRect, int page);
    void onAbout();
    // Help → Check for Updates… (see docs/platform-conventions.md for
    // the per-OS menu placement note). Always enabled regardless of the
    // Preferences → Updates auto-check toggle (G3: never a dead
    // control) — runs one manual check, disclosing the URL it's about
    // to fetch before firing, then reports the outcome (update found /
    // up to date / error) in a small modal.
    void onCheckForUpdates();
    void onAutoFillCurrentForm();
    void onManageMyCard();
    void onManageSignatures();
    // anchorGlobalPos is the screen-space position the FormToolbar
    // captured from the Sign-Here button before emitting; the
    // SignaturePicker pops up there. Default-constructed QPoint
    // (i.e. the QObject::invokeMethod / Tools-menu paths that
    // don't have an anchor) falls back to the cursor position.
    void onSignHere(const QPoint &anchorGlobalPos = QPoint());
    void onCurrentDocumentChanged(IDocument *doc);
    // Forms-toolbar enable/populate for `doc`. Extracted from
    // onCurrentDocumentChanged so it can run both at open and when the
    // document's async form detection completes (PR #63: the qpdf parse that
    // answers supportsFormFilling() runs on a background worker, so forms are
    // not known at open).
    void refreshFormCapabilities(IDocument *doc);
    // Slot fired by IDocument::capabilityNotifier() when async capability
    // detection completes; re-runs refreshFormCapabilities for the current
    // document. Must be a member function — not a lambda — so the
    // Qt::UniqueConnection flag in the connect() call actually takes effect.
    void onDocumentCapabilitiesChanged();
    // Invoked whenever the active document's annotation store mutates
    // (add / remove / update / undo / redo). Refreshes the window
    // title, dirty marker, and Undo/Redo action state. Must be a
    // member function — not a lambda — so the Qt::UniqueConnection
    // flag in the connect() call actually takes effect.
    void onActiveAnnotationStoreChanged();
    // Slot fired by the active document's PageChangeNotifier when the current
    // page changes. Re-derives the large-doc OCR notice and pushes the visible
    // page to the auto-OCR controller (which surfaces / clears the missing-
    // model hint). Replaces the former 150 ms m_ocrPagePoll. Must be a member
    // function — not a lambda — so the Qt::UniqueConnection flag takes effect.
    void onActivePageChanged(int page);
    // Fired by the active document's SelectableTextStore when a page's OCR
    // entry is added / replaced / removed. Refreshes the single-page re-run
    // affordance, whose enabled state and status glyph depend on
    // hasResults(currentPage()) and so must update the moment OCR lands after
    // open. Must be a member — not a lambda — so Qt::UniqueConnection in the
    // connect() call takes effect.
    void onSelectableTextPageChanged(int page);

  private:
    void buildMenus();
    void buildEditMenu(QMenu *editMenu);
    void buildViewMenu(QMenu *viewMenu);
    void buildGoMenu(QMenu *goMenu);
    void buildToolsMenu(QMenu *toolsMenu);
    void buildWindowMenu(QMenu *windowMenu);
    // Slim always-visible top-bar with sidebar mode picker, zoom,
    // rotate, markup / forms toolbar toggles, and an embedded
    // search field. Built last so every action it surfaces has
    // already been created by the menu builders above.
    void buildMainToolbar();
    // Re-pins the ADR-0007 canonical toolbar order (main first / its own
    // row; markup and form each on their own separate row below) after a
    // QMainWindow::restoreState() call. restoreState() restores toolbar
    // area order + row breaks from the blob (matched by object name),
    // which can silently reinstate an older/foreign arrangement — see
    // the call sites in onCurrentDocumentChanged(). None of the three
    // toolbars are user-movable, so there is never a legitimate reason
    // for a persisted blob to win over the construction-time order.
    void reassertToolbarLayout();
    // Repopulate the Window menu's dynamic window list before it
    // shows. The static items (Minimize / Zoom / Bring All to
    // Front) stay; the per-window check-actions get rebuilt from
    // Application::windows() each time so freshly-opened or
    // closed frames show up immediately.
    void refreshWindowMenuList();
    // Retitle the Window-menu maximize/restore action to match the
    // current window state: "&Restore" when maximized, "&Maximize"
    // otherwise. No-op on macOS, where the action keeps its native
    // static "&Zoom" label. Called on WindowStateChange and before the
    // Window menu shows.
    void updateMaximizeActionLabel();
    // Shows the one-time "redaction is not defence-grade" warning
    // (DESIGN §6.11.6) the first time the user activates the
    // Redaction tool. Returns true if the user either already
    // acknowledged or accepts now; false if they cancel. On accept,
    // persists the acknowledgement in the Application settings so
    // the modal never reappears.
    bool confirmRedactionFirstUse();
    // Generalised one-time warning. `key` selects a flag in
    // Settings::firstUseAcknowledged; once the user accepts, it is
    // remembered across sessions and the dialog never shows again.
    // `acceptText` is the label of the accept button — defaults to
    // the standard "OK" if empty.
    bool confirmFirstUse(const QString &key, const QString &title, const QString &body,
                         const QString &acceptText = QString());
    // Save `doc` to `targetPath` without blocking the UI thread.
    // PDFs use the two-phase split (worker thread for qpdf, UI
    // thread for QPdfDocument reload); image saves run synchronously
    // because they are fast.
    void saveDocumentAsync(IDocument *doc, const QString &targetPath);
    // External-file-change plumbing (ADR 2026-07-19).
    // Point the monitor at `doc`'s file and hide any stale banner; called on
    // every current-document change.
    void retargetExternalChangeMonitor(IDocument *doc);
    // Monitor emitted externalChange: classify the current doc against its
    // baseline and act — silently reload a clean doc, or raise the conflict
    // banner for a dirty one.
    void onExternalFileChanged();
    // Monitor emitted fileDeleted: raise the deleted banner and keep the
    // buffer so Save recreates the file.
    void onExternalFileDeleted();
    // Reload the current document from disk in place and refresh the view
    // wiring (search model, sidebar, title). Shared by the silent clean-doc
    // reload and the banner's explicit Reload action.
    void reloadCurrentDocumentFromDisk();
    // Save-time conflict guard: returns true (and raises the banner) when a
    // save of `doc` would clobber an uncaused external change — the caller
    // must then abort the save. Deleted-on-disk is NOT blocked (Save
    // recreates the file). The adapter-level guard (saveWouldClobber…) is the
    // last line of defense; this surfaces the banner before the write starts.
    bool guardSaveAgainstExternalChange(IDocument *doc);
    // True iff `doc`'s backing file changed under us in a way a save must not
    // clobber. Lets the save orchestration distinguish a guard *refusal* from a
    // real write *failure* so the former routes to the conflict banner (F6).
    bool externalConflictPending(IDocument *doc) const;
    // Run the Save-As dialog for `doc` and return the chosen destination
    // path, or an empty string if the user cancelled. Shared by onSaveAs()
    // and the unsaved-changes close prompt so both offer the same dialog.
    QString chooseSaveAsPath(IDocument *doc);
    // Prompt (or, under a forced test response, decide) whether to close
    // a dirty document. Returns true to proceed with closing this doc,
    // false to abort (Cancel, or a Save that failed / was cancelled).
    // Reuses chooseSaveAsPath for untitled docs exactly as closeEvent
    // does. Shared by closeEvent's window-close walk and the
    // DocumentView::documentCloseRequested tab-close veto so both paths
    // behave identically.
    bool confirmCloseDirtyDoc(IDocument *doc);
    void syncViewModeActions(IDocument *doc);
    void showSearchBar();
    void hideSearchBar();
    void updateTitleForDocument(IDocument *doc);
    // Re-derive the large-doc "Recognize text" notice visibility (ADR
    // 0006). Called from onCurrentDocumentChanged, the active document's
    // PageChangeNotifier (page moved), and its SelectableTextStore::changed
    // (OCR results landed), so the notice is guarded by a real per-page text
    // check and self-clears once the page gains text / OCR results / is
    // dismissed — no polling timer.
    void updateLargeDocOcrHint();
    void updateUndoRedoActions(IDocument *doc);
    // Refresh the status-bar zoom-% readout from the current document's
    // zoomFactor(), and keep the zoom actions' tooltips carrying the live
    // percent. Pushed from the zoom call sites and the doc-changed path
    // because IDocument is not a QObject, so there's no zoomFactorChanged
    // signal to subscribe to.
    //
    // The readout itself is TRANSIENT (DR 2026-07-31-transient-zoom-
    // readout): `reveal=true` (the five explicit zoom-action triggers)
    // shows it at full opacity and (re)starts the fade-out hold;
    // `reveal=false` (doc-open, doc-switch, the async initial-fit landing,
    // and any resize-driven re-fit) only updates the cached text/tooltips
    // without disturbing visibility, so opening a file never flashes
    // chrome the user didn't ask to see. The on-demand "what zoom am I
    // at" answer, even while the label is hidden or mid-fade, is the
    // always-current tooltip on Zoom In/Out/Actual Size/Fit Page/Fit to
    // Width.
    void updateZoomIndicator(bool reveal = false);
    // Show the transient readout at full opacity and (re)start its
    // fade-out hold timer. Called only from updateZoomIndicator(true).
    void revealZoomIndicatorTransient();
    // Position the floating m_zoomIndicator HUD relative to the current
    // window size (bottom-right of the document area, clear of the
    // status bar and any corner size-grip). Called on construction and
    // on every resizeEvent().
    void repositionZoomIndicator();
    int selectedPageForEdit(IDocument *doc) const;
    // Size the window to fit the first document opened. Clamped to a
    // 1100×750 floor and a 90%-of-screen ceiling so very small docs
    // don't shrink the window and very large ones don't fill the
    // screen. Caps the implied zoom at ~75% — we'd rather have a
    // big-but-screen-bounded window than a small one where the doc
    // is unreadable. One-shot per window; subsequent file opens
    // leave the size alone.
    void applyInitialWindowSize(IDocument *doc);

    // Trigger a background scoring pass through MlScheduler so we know
    // whether to badge the Remove Background menu entry for `doc`. The
    // scoring itself is pure CPU but routed through the scheduler so it
    // is cooperatively cancellable when the document closes mid-compute
    // and honours the battery toggle (Prefetch is dropped on battery
    // when mlRunOnBattery=false; that's the right trade-off — the badge
    // is a nicety, not a feature). One-shot per document pointer: once
    // we have a verdict we cache it and skip rescoring on subsequent
    // tab switches. Called from onCurrentDocumentChanged; safe to call
    // for non-image documents (no-ops).
    void scheduleBackgroundCandidateScore(IDocument *doc);

    // Single icon+tooltip authority for the Remove Background menu entry
    // (DR 2026-07-21-bg-removal-menu-status-glyph). Layers the transient
    // op-status glyph OVER the candidate-recommendation badge, in priority:
    //   1. Unavailable (entry disabled — wrong doc type / policy blocked):
    //      a muted "can't" glyph; the applyMlPolicy() tooltip stands.
    //   2. Calculating (op in flight): a "busy" glyph; tooltip says the op
    //      is running and re-invoking cancels it.
    //   3. Failed (last op returned null, not cancelled): an alert glyph;
    //      tooltip invites a retry. Transient — cleared on the next viable
    //      Available refresh.
    //   4. Available: the heuristic "sparkle" recommendation badge (or no
    //      icon) exactly as before.
    // Idempotent; called whenever the active document changes, the scorer
    // finishes a pass, or the op lifecycle advances.
    void updateRemoveBackgroundBadge(IDocument *doc);

    // Transient status of the Remove Background op, surfaced as the menu
    // entry's glyph (DR 2026-07-21). Unavailable is not tracked here — it is
    // derived from the action's enabled state at refresh time.
    enum class BgRemovalStatus { Available, Calculating, Failed };

    // GUI-thread teardown for a finished Remove Background op. Clears the
    // in-flight state, sets the next status (success/cancel → Available,
    // failure → Failed), and refreshes the menu glyph. Idempotent.
    void finishBackgroundRemoval(bool cancelled, bool succeeded);

    Application *m_app;
    DocumentView *m_documentView = nullptr;
    // Forced outcome for the unsaved-changes close prompt. Prompt (the
    // default) shows the real modal; the UAT harness overrides it to
    // drive Save / Discard / Cancel deterministically. See
    // setCloseResponseForTesting / confirmCloseDirtyDoc.
    CloseResponse m_closeResponseForTesting = CloseResponse::Prompt;
    // Forced Save-As destination for tests; empty means "show the real
    // file dialog". See setSaveAsPathForTesting / chooseSaveAsPath.
    QString m_saveAsPathForTesting;
    // Empty-state window model: the central column is a QStackedWidget
    // with two pages — the document page (document view + animation bar)
    // and the empty-state welcome surface. updateEmptyState() swaps
    // between them based on documentCount().
    EmptyStateWidget *m_emptyState = nullptr;
    QStackedWidget *m_centerStack = nullptr;
    QWidget *m_documentPage = nullptr;
    AnimationBar *m_animationBar = nullptr;
    // External-file-change handling (ADR 2026-07-19). The monitor watches the
    // CURRENT document's file (+ its parent dir) for external modification /
    // deletion / atomic replacement; the banner is the non-modal conflict
    // surface shown above the document view. One of each tracks the active
    // document — see onCurrentDocumentChanged / the on*ExternalFile* slots.
    ExternalChangeMonitor *m_externalChangeMonitor = nullptr;
    FileChangeBanner *m_fileChangeBanner = nullptr;
    // Primary read-only degradation signal for Two-Pages mode: a compact
    // in-context badge (a lock pill) in the permanent status-bar area, next to
    // the zoom indicator, shown only in Two-Pages mode. It recedes rather than
    // occupying a full-width strip, per the minimal-UI guideline
    // (docs/ux-guidelines.md, #116): a state badge on the relevant surface, not
    // a banner. The full "switch to Single or Continuous to edit" sentence lives
    // in its tooltip; the per-control disabled tooltips remain the G3 floor
    // (decision record 2026-07-21-two-page-layout, D2-A). Shown/hidden by
    // onCurrentDocumentChanged.
    QLabel *m_readOnlyBadge = nullptr;
    Inspector *m_inspector = nullptr;
    Magnifier *m_magnifier = nullptr;
    MarkupToolbar *m_markupToolbar = nullptr;
    FormToolbar *m_formToolbar = nullptr;
    SearchBar *m_searchBar = nullptr;
    // Records the window's themed toolbar/menu icons so refreshThemedIcons()
    // can re-tint them after a live theme (colour-scheme) change — a palette
    // swap does not touch the fixed-colour pixmaps themedActionIcon bakes.
    ThemedIconBinder m_themedIcons;
    // Collapsed-state proxy for the search bar in the main toolbar.
    // Clicking the button expands m_searchBar inline and hides the
    // button; dismissing the bar (Esc, empty query) does the reverse.
    QToolButton *m_searchButton = nullptr;
    // The QWidgetAction that QToolBar::addWidget wraps around m_searchBar.
    // Toggling the inner widget's visibility alone leaves this action's
    // toolbar slot occupied, so hide/show paths must drive this too.
    QAction *m_searchBarAction = nullptr;
    Sidebar *m_sidebar = nullptr;
    QMenu *m_recentMenu = nullptr;

    QAction *m_quitAction = nullptr;
    // "Quit and Keep Windows" (⌥⌘Q). Created cross-platform so the model is
    // headless-testable; on macOS QuitMenu adds the native in-place Option
    // swap on top. Routes to Application::requestQuit(QuitMode::KeepWindows).
    QAction *m_quitKeepWindowsAction = nullptr;
    QAction *m_saveAction = nullptr;
    QAction *m_saveAsAction = nullptr;
    QAction *m_rotateLeftAction = nullptr;
    QAction *m_rotateRightAction = nullptr;
    QAction *m_flipHorizontalAction = nullptr;
    QAction *m_flipVerticalAction = nullptr;
    QAction *m_adjustSizeAction = nullptr;
    QAction *m_adjustColourAction = nullptr;
    QAction *m_removeBackgroundAction = nullptr;
    QAction *m_instantAlphaAction = nullptr;
    QAction *m_smartLassoAction = nullptr;
    QAction *m_recognizeTextAction = nullptr;
    // Single-page force-rerun entry (backlog 2026-07-15-single-page-force-
    // rerun) that doubles as the recognised-text status glyph (owner HITL on
    // #114). Checkable: the native menu checkmark is on when the current page
    // carries recognised, selectable text — the subtle in-context glyph that
    // replaced the former transient status-bar cue. Enabled only when the
    // current doc is single-page, supports selectable text, and already has
    // OCR results to replace; otherwise disabled with a why/where-to-go
    // tooltip (G3). Refreshed by refreshRerunRecognizeAction() both at
    // doc-change and when OCR lands.
    QAction *m_rerunRecognizeAction = nullptr;
    QAction *m_exportAsAction = nullptr;
    QAction *m_exportPasswordProtectedAction = nullptr;
    QAction *m_reduceFileSizeAction = nullptr;
    QAction *m_screenshotAction = nullptr;
    QAction *m_cropImageAction = nullptr;
    QAction *m_insertPagesAction = nullptr;
    QAction *m_cropPagesAction = nullptr;
    QAction *m_cropPagesDragAction = nullptr;
    QAction *m_fillFormsAction = nullptr;
    // Documents we've already auto-enabled Fill Forms for. Tracked so
    // that toggling Fill Forms off, switching docs, and switching back
    // does not re-enable it. Pointers may dangle when docs are closed,
    // which is harmless: dangling entries are never dereferenced.
    QSet<const IDocument *> m_autoEnabledFormDocs;
    // Auto-save loop: every kAutoSaveIntervalMs the timer fires and
    // saves any dirty document that already has a file path
    // (untitled documents are excluded — auto-save shouldn't pick a
    // location for the user). The timer is started lazily once a
    // document with a path is in the window and stops when no doc
    // is open. Honours `Settings::autoSave()`; flipping that off
    // pauses the timer.
    QTimer *m_autoSaveTimer = nullptr;
    // The document whose explicit Save is running on a QtConcurrent worker
    // (saveDocumentAsync, PDF path). The worker mutates the document's
    // non-thread-safe qpdf editor; the auto-save tick must NOT touch the same
    // editor (writeRecoverySnapshot) concurrently, so it skips this doc while a
    // save is in flight. Only compared, never dereferenced. Cleared when the
    // worker's finished slot runs and defensively when the doc is removed.
    // (IDocument is not a QObject, so this is a raw pointer, not a QPointer.)
    IDocument *m_docSaveInFlight = nullptr;
    // Documents whose recent-file view state has already been
    // restored on focus. Tracked per-document so a tab switch
    // doesn't bounce the user back to the saved page mid-session.
    QSet<const IDocument *> m_restoredViewStateDocs;
    // First-open documents whose content-aware sidebar decision was deferred
    // because form detection had not yet completed (async since PR #63).
    // onDocumentCapabilitiesChanged re-evaluates and removes them once the
    // AcroForm answer lands. Pointers may dangle after close, which is
    // harmless: entries are removed on close and never dereferenced stale.
    QSet<const IDocument *> m_contentAwareFormSidebarPending;
    // One-shot guard for applyInitialWindowSize. True after the
    // first opened doc has driven a resize so opening additional
    // tabs in the same window doesn't bounce the geometry around.
    bool m_initialSizingApplied = false;
    QAction *m_autoFillFormAction = nullptr;
    QAction *m_myCardAction = nullptr;
    QAction *m_manageSignaturesAction = nullptr;
    QAction *m_printAction = nullptr;
    QAction *m_shareAction = nullptr; // macOS-only; null on other platforms
    QToolBar *m_mainToolbar = nullptr;
    QMenu *m_windowMenu = nullptr;
    // Maximize/restore toggle in the Window menu (macOS "Zoom"). Stored
    // so changeEvent() can retitle it to match the current window state
    // on Win/Linux; on macOS it keeps its static native "Zoom" label.
    QAction *m_maximizeAction = nullptr;
    // Sentinel separator: the dynamic window list is rebuilt by
    // removing every action AFTER this separator before each show.
    QAction *m_windowMenuListSeparator = nullptr;
    QAction *m_findAction = nullptr;
    QAction *m_findNextAction = nullptr;
    QAction *m_findPreviousAction = nullptr;
    QAction *m_selectAllAction = nullptr;
    QAction *m_copyPageAction = nullptr;
    QAction *m_undoAction = nullptr;
    QAction *m_redoAction = nullptr;

    // Sidebar picker's "Table of Contents" entry. Enabled per-doc
    // based on hasOutline() so PDFs without an /Outlines tree get
    // a greyed-out entry instead of an empty tree.
    QAction *m_tocSidebarAction = nullptr;
    // "Highlights & Notes" entry. Enabled iff the doc has at least
    // one text-content annotation (highlights, underlines, sticky
    // notes, free text, speech bubbles). Refreshed on store
    // mutations via onActiveAnnotationStoreChanged.
    QAction *m_highlightsAndNotesSidebarAction = nullptr;
    QAction *m_zoomInAction = nullptr;
    QAction *m_zoomOutAction = nullptr;
    QAction *m_zoomActualAction = nullptr;
    QAction *m_zoomFitAction = nullptr;
    QAction *m_zoomFitPageAction = nullptr;
    QAction *m_magnifierAction = nullptr;
    QAction *m_markupToolbarAction = nullptr;
    QAction *m_formToolbarAction = nullptr;
    QAction *m_inspectorAction = nullptr;

    QActionGroup *m_viewModeGroup = nullptr;
    QAction *m_singlePageAction = nullptr;
    QAction *m_twoPagesAction = nullptr;
    QAction *m_continuousAction = nullptr;
    // True while the markup / form toolbar was auto-hidden because the document
    // entered Two-Pages mode (honest degradation). Lets us restore the user's
    // prior toolbar visibility when they leave Two-Pages, instead of leaving a
    // toolbar they had open closed.
    bool m_markupHiddenForTwoPage = false;
    bool m_formToolbarHiddenForTwoPage = false;

    QAction *m_previousPageAction = nullptr;
    QAction *m_nextPageAction = nullptr;

    // Permanent status-bar widget that shows whenever the
    // MlScheduler has a non-Idle task running. Tooltip carries the
    // running task's label (e.g. "Recognizing text…"). Hidden when
    // the scheduler is idle so the status bar stays clean. PHILOSOPHY:
    // no modal — this is the canonical "background ML work is
    // happening" affordance for the user.
    QLabel *m_mlIndicator = nullptr;

    // TRANSIENT zoom-% HUD (e.g. "120%") -- DR 2026-07-31-transient-
    // zoom-readout. A FLOATING overlay label parented directly to the
    // MainWindow, NOT a status-bar permanent widget (G10 spatial
    // constancy: a status-bar box widget's visibility change reflows
    // every OTHER permanent widget in that same box, since Qt keeps the
    // box right-anchored regardless of insertion order -- confirmed by
    // uat_zoom_ind_070's measured sibling-shift before this was fixed).
    // Hidden at rest; an explicit zoom action reveals it and (re)starts
    // the fade-out hold. Hidden unconditionally when the active document
    // doesn't support zoom. Repositioned on resizeEvent() via
    // repositionZoomIndicator(). Updated via updateZoomIndicator() from
    // the zoom actions and the doc-changed path.
    QLabel *m_zoomIndicator = nullptr;
    // Opacity effect the fade animation drives; owned by m_zoomIndicator
    // via setGraphicsEffect().
    QGraphicsOpacityEffect *m_zoomIndicatorOpacity = nullptr;
    QPropertyAnimation *m_zoomIndicatorFadeAnim = nullptr;
    // Single-shot: fires kZoomIndicatorVisibleMs after a reveal, starting
    // the fade (or, with Reduce Motion, an instant hide).
    QTimer *m_zoomIndicatorFadeTimer = nullptr;
    // Hand-tuned hold/fade durations, set from kZoomIndicatorVisibleMs /
    // kZoomIndicatorFadeMs (MainWindow.cpp constructor) and overridable by
    // setZoomIndicatorTimingForTesting() for deterministic UAT coverage.
    int m_zoomIndicatorVisibleMs = 0;
    int m_zoomIndicatorFadeMs = 0;

    // ADR 0002 status-bar affordances. m_mlProgress is the richer
    // progress+cancel widget for foreground ML ops (OCR batches;
    // background removal can adopt it later). m_cancelMlAction is the
    // ⌘. keyboard cancel, enabled only while a foreground cancellable op
    // is running. m_ocrModelMissingHint is the non-modal in-context
    // "install language pack" prompt shown when auto-OCR can't run
    // because the language model is absent. The m_ocr* scalars coordinate
    // the reveal delay: progress is buffered until the widget reveals so
    // sub-threshold batches never flicker it.
    MlProgressWidget *m_mlProgress = nullptr;
    QAction *m_cancelMlAction = nullptr;
    QLabel *m_ocrModelMissingHint = nullptr;
    int m_ocrPendingTotal = 0;
    int m_ocrPendingCompleted = 0;
    bool m_ocrRevealed = false;
    // ADR 0002 §1 elapsed-time reassurance. While an INDETERMINATE reveal
    // is showing, this 1s timer ticks and feeds setElapsedSeconds() so a
    // slow single-page op appends "· Ns" past 10s. Stopped on finish/idle.
    QTimer *m_ocrElapsedTimer = nullptr;
    int m_ocrElapsedSecs = 0;
    // ADR 0002 §3 page-change re-derivation. Driven by the active document's
    // PageChangeNotifier (a real page-changed signal) so the missing-model
    // hint re-derives when scrolling between text and scanned pages — no
    // polling. m_lastOcrPage is the last page pushed to the controller;
    // onActivePageChanged() dedups against it.
    int m_lastOcrPage = -1;
    // Test seam for the missing-model hint's download-consent routing.
    std::function<bool()> m_ocrModelDownloadHook;

    // Auto-OCR pump. Owns an OcrEngine and tracks the in-flight
    // submissions for the current document; signals from the document
    // view feed visible-page changes into it. The controller is
    // parented to this MainWindow so it dies with the window.
    OcrController *m_ocrController = nullptr;
    // SAM (Instant Alpha / Smart Lasso) controller. Owns a shared
    // SamSession + an LRU cache of prepared encoder embeddings. Wired
    // into the AnnotationOverlay for the active doc so click/drag
    // events on the document drive segmentation directly.
    SamController *m_samController = nullptr;

    // Status-bar "Text isn't selectable here. Recognize text on this
    // page." offer for large documents that we skipped auto-OCR for.
    // Shown only when the active doc is large + non-OCR'd; hidden
    // otherwise so the status bar stays clean.
    QWidget *m_largeDocOcrHint = nullptr;
    // Per-document dismissal for the large-doc recognize notice (ADR
    // 0006). Keyed by IDocument* so a notice dismissed on doc A stays
    // hidden when the user tab-switches away and back, independently of
    // other documents. Pointers are used only for identity membership,
    // never dereferenced.
    QSet<IDocument *> m_largeDocOcrHintDismissed;
    // Single-entry cache for the pageHasText() probe, which re-extracts
    // the full page text. Only re-probed when the (document, page) changes.
    IDocument *m_pageHasTextCacheDoc = nullptr;
    int m_pageHasTextCachePage = -1;
    bool m_pageHasTextCacheValue = false;

    // Decoration applied to the Remove Background menu entry when the
    // current image is a strong candidate for background removal. The
    // scoring runs once per image-document open through MlScheduler at
    // Prefetch priority; the resulting flag flips the action's icon to
    // a "sparkle" glyph and adjusts the tooltip. Per-document so a tab
    // switch between an image and a PDF reverts the badge cleanly.
    // Plain pointer key — entries are removed when the doc closes
    // (DocumentView::documentClosed signal in onCurrentDocumentChanged).
    QSet<const IDocument *> m_backgroundCandidateDocs;
    // Pending scoring jobs keyed by document pointer so we can cancel
    // them if the doc closes mid-compute. Maps to the MlScheduler task
    // id returned by submit(); zero means no in-flight job.
    QHash<const IDocument *, std::uint64_t> m_pendingCandidateJobs;

    // Remove Background op lifecycle (DR 2026-07-21-bg-removal-menu-status-
    // glyph). The op is surfaced ONLY through the menu entry's status glyph
    // (plus the ambient m_mlIndicator dot per CONVENTIONS §12) — no progress
    // bar or spinner widget. m_bgRemovalActive guards re-entrancy: a second
    // trigger while an op is in flight cancels it via m_bgRemovalToken (the
    // running op's cooperative cancellation token) rather than stacking a
    // second submission. m_bgRemovalStatus drives the glyph the refresh
    // function paints. Only one ML op runs at a time (single MlScheduler
    // worker), so a single token/flag pair is sufficient.
    bool m_bgRemovalActive = false;
    std::shared_ptr<CancellationToken> m_bgRemovalToken;
    BgRemovalStatus m_bgRemovalStatus = BgRemovalStatus::Available;
    // Test seam: replaces the real BackgroundRemover inference and skips the
    // download pre-flight (see setBackgroundRemoveFnForTesting). nullptr in
    // production.
    std::function<QImage(const QImage &, const CancellationToken *)> m_bgRemoveFnOverride;
    // Item A on-demand search OCR: images whose page 0 we have already
    // asked to OCR because the user typed a search query while the OCR
    // store was still empty. Guards against re-submitting (and thus
    // cancel/restarting) the same page on every keystroke. Pointers are
    // identity-only; entries are purged when the document closes.
    QSet<IDocument *> m_searchOcrKicked;
    // Kick page-0 OCR for an image the user is searching before it has any
    // OCR results, so Find works even without a prior manual Recognize run.
    // No-op for non-images (PDFs search native text), empty queries, and
    // pages that already have results. Uses the same OcrController path the
    // menu uses; when the model is absent it silently no-ops (no modal).
    void maybeKickSearchOcr(IDocument *doc, const QString &query);
    // Re-evaluate the single-page re-run affordance's enabled state, checkmark
    // status glyph, and why/where-to-go tooltip (G3). Shared by
    // updateActionStates (at doc-change) and onSelectableTextPageChanged (when
    // OCR lands after open). Self-contained: recomputes the ML-policy gate
    // inline.
    void refreshRerunRecognizeAction();
};

} // namespace trailer
