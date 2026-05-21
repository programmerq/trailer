#pragma once

#include "document/IDocument.h"

#include <QActionGroup>
#include <QHash>
#include <QMainWindow>
#include <QSet>
#include <QStringList>
#include <cstdint>
#include <memory>

class QCloseEvent;
class QTimer;

class QAction;
class QLabel;
class QMenu;
class QToolButton;

namespace trailer {

class AnimationBar;
class Application;
class DocumentView;
class Inspector;
class FormToolbar;
class Magnifier;
class MarkupToolbar;
class SearchBar;
class Sidebar;

class OcrController;
class SamController;

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
    // Suppress Qt's built-in right-click toolbar/dock context menu.
    // That menu lets the user accidentally hide a toolbar with no
    // discoverable way to bring it back — the source of the
    // "I'm tired of re-enabling toolbars" frustration. Visibility
    // lives in the View menu (and the main toolbar toggles); this
    // is the single source of truth.
    QMenu *createPopupMenu() override;

  private slots:
    void onOpen();
    // Wipe every Trailer-managed file under AppPaths::*. Used by
    // Tools → Reset Trailer Settings…  for the "is this stale
    // state from an older build?" diagnostic. Asks for explicit
    // confirmation because this is destructive.
    void onResetTrailerSettings();
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
    void onExportAs();
    void onExportPasswordProtected();
    void onReduceFileSize();
    void onTakeScreenshot();
    void onCropImage();
    void onInsertPages();
    void onCropPages();
    void onAbout();
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
    // Invoked whenever the active document's annotation store mutates
    // (add / remove / update / undo / redo). Refreshes the window
    // title, dirty marker, and Undo/Redo action state. Must be a
    // member function — not a lambda — so the Qt::UniqueConnection
    // flag in the connect() call actually takes effect.
    void onActiveAnnotationStoreChanged();

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
    // Repopulate the Window menu's dynamic window list before it
    // shows. The static items (Minimize / Zoom / Bring All to
    // Front) stay; the per-window check-actions get rebuilt from
    // Application::windows() each time so freshly-opened or
    // closed frames show up immediately.
    void refreshWindowMenuList();
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
    void syncViewModeActions(IDocument *doc);
    void showSearchBar();
    void hideSearchBar();
    void updateTitleForDocument(IDocument *doc);
    void updateUndoRedoActions(IDocument *doc);
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

    // Apply the cached badge state for `doc` to the Remove Background
    // menu action: a 12px "sparkle" glyph + a positive tooltip when
    // the heuristic says this image looks promising, otherwise no
    // icon and the policy-aware tooltip set by applyMlPolicy(). Idempo-
    // tent; called whenever the active document changes or the scorer
    // finishes a pass.
    void updateRemoveBackgroundBadge(IDocument *doc);

    Application *m_app;
    DocumentView *m_documentView = nullptr;
    AnimationBar *m_animationBar = nullptr;
    Inspector *m_inspector = nullptr;
    Magnifier *m_magnifier = nullptr;
    MarkupToolbar *m_markupToolbar = nullptr;
    FormToolbar *m_formToolbar = nullptr;
    SearchBar *m_searchBar = nullptr;
    // Collapsed-state proxy for the search bar in the main toolbar.
    // Clicking the button expands m_searchBar inline and hides the
    // button; dismissing the bar (Esc, empty query) does the reverse.
    QToolButton *m_searchButton = nullptr;
    Sidebar *m_sidebar = nullptr;
    QMenu *m_recentMenu = nullptr;

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
    QAction *m_exportAsAction = nullptr;
    QAction *m_exportPasswordProtectedAction = nullptr;
    QAction *m_reduceFileSizeAction = nullptr;
    QAction *m_screenshotAction = nullptr;
    QAction *m_cropImageAction = nullptr;
    QAction *m_insertPagesAction = nullptr;
    QAction *m_cropPagesAction = nullptr;
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
    // Documents whose recent-file view state has already been
    // restored on focus. Tracked per-document so a tab switch
    // doesn't bounce the user back to the saved page mid-session.
    QSet<const IDocument *> m_restoredViewStateDocs;
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
    // Sentinel separator: the dynamic window list is rebuilt by
    // removing every action AFTER this separator before each show.
    QAction *m_windowMenuListSeparator = nullptr;
    QAction *m_findAction = nullptr;
    QAction *m_findNextAction = nullptr;
    QAction *m_findPreviousAction = nullptr;
    QAction *m_selectAllAction = nullptr;
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

    QAction *m_previousPageAction = nullptr;
    QAction *m_nextPageAction = nullptr;

    // Permanent status-bar widget that shows whenever the
    // MlScheduler has a non-Idle task running. Tooltip carries the
    // running task's label (e.g. "Recognizing text…"). Hidden when
    // the scheduler is idle so the status bar stays clean. PHILOSOPHY:
    // no modal — this is the canonical "background ML work is
    // happening" affordance for the user.
    QLabel *m_mlIndicator = nullptr;

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
};

} // namespace trailer
