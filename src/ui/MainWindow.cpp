#include "MainWindow.h"

#include "TrailerVersion.h"
#include "AnimationBar.h"
#include "DocumentView.h"
#include "EmptyStateWidget.h"
#include "AnnotationOverlay.h"
#include "ContentAwareDefaults.h"
#include "Inspector.h"
#include "Magnifier.h"
#include "FormToolbar.h"
#include "CropPagesDialog.h"
#include "IconHelper.h"
#include "MarkupToolbar.h"
#include "MyCardDialog.h"
#include "PreferencesDialog.h"
#include "SignaturePicker.h"
#include "SignaturesDialog.h"
#include "cards/CardStore.h"
#include "cards/MyCard.h"
#include "document/PdfAdapter.h"
#include "document/PdfEditor.h" // FormField definition for AutoFill
#include "SearchBar.h"
#include "Sidebar.h"
#include "annotation/AnnotationStore.h"
#include "app/Application.h"
#include "filters/ImageFilter.h"
#include "ml/BackgroundCandidateScorer.h"
#include "ml/BackgroundRemover.h"
#include "ml/CancellationToken.h"
#include "ml/MlScheduler.h"
#include "ml/ModelRegistry.h"
#include "ml/OcrEngine.h"
#include "platform/ScreenCapturePermission.h"
#include "platform/Share.h"
#include "settings/AppPaths.h"
#include "ml/SamSession.h"
#include "recent/RecentFiles.h"
#include "MlProgressWidget.h"
#include "ModelManagerDialog.h"
#include "OcrController.h"
#include "OcrResultsDialog.h"
#include "SamController.h"
#include "SelectableTextLayer.h"
#include "document/ImageAdapter.h"
#include "document/SelectableTextStore.h"

#include <QAction>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QToolButton>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImageWriter>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QPixmap>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QRadioButton>
#include <QScreen>
#include <QSlider>
#include <QSpinBox>
#include <QCloseEvent>
#include <QFuture>
#include <QFutureWatcher>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTemporaryFile>
#include <QTimer>
#include <QtConcurrent>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>

namespace trailer {

// Defined in the anonymous namespace further down; forward-declared so the
// missing-model hint's Install link (wired in the constructor) can reach the
// shared one-time-consent download flow (ADR 0002 §3).
namespace {
bool ensureOcrModelsReady(MainWindow *parent, OcrEngine &engine);

// Class-targeted stylesheet that pins the built-in QToolBar overflow
// chevron (objectName qt_toolbar_ext_button) to a fixed width so
// toggling the "show more" popup never reflows adjacent widgets
// (ADR 0007, Option A, R2). 20px matches the toolbars' 18px iconSize.
QString kToolbarExtensionPinStyle() {
    return QStringLiteral(
        "QToolBarExtension#qt_toolbar_ext_button { min-width: 20px; max-width: 20px; }");
}
}

MainWindow::MainWindow(Application *app, QWidget *parent) : QMainWindow(parent), m_app(app) {
    setAcceptDrops(true);
    setWindowTitle(tr("Trailer"));
    resize(1100, 750);

    auto *center = new QWidget(this);
    auto *centerLayout = new QVBoxLayout(center);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    centerLayout->setSpacing(0);

    // SearchBar is constructed here without a parent layout; the
    // main toolbar reparents it (buildMainToolbar). It stays alive
    // for the window's lifetime and is always visible.
    m_searchBar = new SearchBar(center);

    // The PDF search model populates asynchronously; poll the
    // document's match count periodically and refresh the
    // SearchBar's "X of Y" indicator. The timer only ticks while
    // there's an active query so it doesn't burn cycles when the
    // user isn't searching.
    auto *searchPoll = new QTimer(this);
    searchPoll->setInterval(150);
    connect(searchPoll, &QTimer::timeout, this, [this]() {
        if (m_searchBar->query().isEmpty())
            return;
        auto *doc = m_documentView->currentDocument();
        if (!doc) {
            m_searchBar->setMatchCounter(0, 0);
            return;
        }
        m_searchBar->setMatchCounter(doc->currentSearchMatchIndex(), doc->searchMatchCount());
        // Push the search-result page list into the sidebar so
        // SearchResults mode shows just those pages. Fast — the
        // list is computed on demand from the cached search model.
        m_sidebar->setSearchMatchPages(doc->pagesWithSearchMatches());
    });
    searchPoll->start();

    connect(m_searchBar, &SearchBar::queryChanged, this, [this](const QString &q) {
        if (auto *doc = m_documentView->currentDocument()) {
            doc->setSearchQuery(q);
            m_searchBar->setMatchCounter(doc->currentSearchMatchIndex(), doc->searchMatchCount());
            // Item A: an image the user searches before OCR has run has an
            // empty store, so setSearchQuery finds nothing. Kick page-0 OCR
            // so results (and highlights) appear once recognition lands.
            maybeKickSearchOcr(doc, q);
        }
    });
    connect(m_searchBar, &SearchBar::findNextRequested, this, [this]() {
        if (auto *doc = m_documentView->currentDocument()) {
            doc->findNext();
            m_searchBar->setMatchCounter(doc->currentSearchMatchIndex(), doc->searchMatchCount());
        }
    });
    connect(m_searchBar, &SearchBar::findPreviousRequested, this, [this]() {
        if (auto *doc = m_documentView->currentDocument()) {
            doc->findPrevious();
            m_searchBar->setMatchCounter(doc->currentSearchMatchIndex(), doc->searchMatchCount());
        }
    });
    connect(m_searchBar, &SearchBar::dismissed, this, &MainWindow::hideSearchBar);

    m_documentView = new DocumentView(center);
    m_ocrController = new OcrController(m_app, this);
    m_samController = new SamController(m_app, this);
    connect(m_documentView, &DocumentView::currentDocumentChanged, this,
            &MainWindow::onCurrentDocumentChanged);
    // Window-per-file: when the last document in this window is
    // closed, close the window too rather than leaving a ghost frame
    // behind. For the legacy tab mode this still fires correctly —
    // closing the final tab discards the now-empty window, which is
    // also what the user expects.
    connect(m_documentView, &DocumentView::allTabsClosed, this, &MainWindow::onAllTabsClosed);
    // Unsaved-changes veto for tab closes. onTabCloseRequested emits this
    // synchronously before tearing the tab down; we prompt (reusing the
    // same Save/Discard/Cancel flow as closeEvent) only for dirty docs
    // and set *veto when the user aborts. A CLEAN doc is never prompted
    // — the isDirty() guard keeps never-worry-save and auto-saved docs
    // (which report isDirty()==false) from nagging when nothing is lost.
    connect(m_documentView, &DocumentView::documentCloseRequested, this,
            [this](IDocument *doc, bool *veto) {
                if (doc && doc->isDirty())
                    *veto = !confirmCloseDirtyDoc(doc);
            });
    // Flush per-document state keyed by raw IDocument pointer before
    // the document is destroyed. The MlScheduler owns the cancellation
    // tokens for in-flight work; cancelling here keeps the worker
    // from dereferencing a stale doc in its result-application step.
    connect(m_documentView, &DocumentView::documentAboutToBeRemoved, this, [this](IDocument *doc) {
        if (!doc)
            return;
        m_backgroundCandidateDocs.remove(doc);
        m_searchOcrKicked.remove(doc);
        auto it = m_pendingCandidateJobs.find(doc);
        if (it != m_pendingCandidateJobs.end()) {
            m_app->mlScheduler().cancel(it.value());
            m_pendingCandidateJobs.erase(it);
        }
        m_autoEnabledFormDocs.remove(doc);
        m_restoredViewStateDocs.remove(doc);
        // Drop the large-doc recognize-notice dismissal + the pageHasText
        // probe cache for this doc. Both are keyed by the raw IDocument*;
        // without this a closed document's address could be recycled by
        // the allocator and a fresh document at the same address would
        // inherit the prior dismissal or a stale pageHasText cache hit.
        m_largeDocOcrHintDismissed.remove(doc);
        if (m_pageHasTextCacheDoc == doc) {
            m_pageHasTextCacheDoc = nullptr;
            m_pageHasTextCachePage = -1;
            m_pageHasTextCacheValue = false;
        }
        m_contentAwareFormSidebarPending.remove(doc);
        // Drop the SAM encoder cache + cancel in-flight tasks for this
        // doc. Without this a closed document's address could be
        // recycled by the allocator and a fresh document at the same
        // address would inherit the stale cache.
        if (m_samController) {
            m_samController->purgeDocument(doc);
        }
    });

    m_animationBar = new AnimationBar(center);
    m_animationBar->hide();

    // The search bar moved into the main toolbar (built later) so
    // it's always visible at the top right. The central column is a
    // QStackedWidget that swaps between the document page (document
    // view + animation bar) and the empty-state welcome surface shown
    // when no document is open (empty-state window model). The
    // addWidget calls below reparent m_documentView / m_animationBar
    // into the document page.
    m_centerStack = new QStackedWidget(center);

    m_documentPage = new QWidget(m_centerStack);
    auto *documentLayout = new QVBoxLayout(m_documentPage);
    documentLayout->setContentsMargins(0, 0, 0, 0);
    documentLayout->setSpacing(0);
    documentLayout->addWidget(m_documentView, 1);
    documentLayout->addWidget(m_animationBar);

    m_emptyState = new EmptyStateWidget(m_centerStack);
    connect(m_emptyState, &EmptyStateWidget::openRequested, this, &MainWindow::onOpen);
    connect(m_emptyState, &EmptyStateWidget::filesDropped, this, [this](const QStringList &p) {
        if (m_app)
            m_app->openFiles(p);
    });

    m_centerStack->addWidget(m_documentPage);
    m_centerStack->addWidget(m_emptyState);

    centerLayout->addWidget(m_centerStack);
    setCentralWidget(center);

    m_sidebar = new Sidebar(this);
    addDockWidget(Qt::LeftDockWidgetArea, m_sidebar);
    // Sidebar is hidden by default — the user opens it via the
    // top-bar's sidebar mode picker (or View → Toggle Sidebar).
    // Always-visible chrome is loud for a document-first workflow.
    m_sidebar->hide();
    m_inspector = new Inspector(this);
    addDockWidget(Qt::RightDockWidgetArea, m_inspector);
    m_inspector->hide();
    connect(m_sidebar, &Sidebar::annotationSelected, this, [this](int id) {
        auto *doc = m_documentView->currentDocument();
        if (!doc)
            return;
        m_inspector->setAnnotation(doc->annotations(), id);
        if (!m_inspector->isVisible())
            m_inspector->show();
    });
    connect(m_sidebar, &Sidebar::deletePagesRequested, this, [this](const std::vector<int> &rows) {
        auto *doc = m_documentView->currentDocument();
        if (!doc || !doc->supportsEditing())
            return;
        doc->deletePages(rows);
        m_sidebar->refreshThumbnails();
        onCurrentDocumentChanged(doc);
    });
    connect(m_sidebar, &Sidebar::movePageRequested, this, [this](int from, int to) {
        auto *doc = m_documentView->currentDocument();
        if (!doc || !doc->supportsEditing())
            return;
        doc->movePage(from, to);
        m_sidebar->refreshThumbnails();
        onCurrentDocumentChanged(doc);
    });

    m_magnifier = new Magnifier(this);

    m_markupToolbar = new MarkupToolbar(this);
    // Pin the overflow chevron to a fixed width (ADR 0007, Option A,
    // R2). A class-targeted stylesheet is creation-timing-independent
    // and, unlike setFixedSize on the instance, survives
    // QToolBarLayout's per-relayout setGeometry — so toggling the
    // "show more" popup never reflows the chevron's neighbours. 20px
    // matches the toolbars' 18px iconSize.
    m_markupToolbar->setStyleSheet(kToolbarExtensionPinStyle());
    addToolBar(Qt::TopToolBarArea, m_markupToolbar);
    m_markupToolbar->hide();
    connect(m_markupToolbar, &MarkupToolbar::activeToolChanged, this, [this](AnnotationTool tool) {
        if (tool == AnnotationTool::Redaction && !confirmRedactionFirstUse()) {
            // User declined the warning — bounce back to the
            // Select tool so no redaction rectangle is
            // accidentally placed. setActiveTool() updates
            // both the toolbar check-state and our own view.
            m_markupToolbar->setActiveTool(AnnotationTool::Select);
            return;
        }
        if (auto *doc = m_documentView->currentDocument()) {
            doc->setAnnotationTool(tool);
            doc->setAnnotationStyle(m_markupToolbar->style());
            // Warm the MobileSAM encoder the moment the user picks a
            // segmentation tool so the first stroke doesn't pay the
            // encode latency. Opt-out via Settings; only fires for image
            // documents (the only place SAM tools are offered) and only
            // when the models are already on disk — we never kick a
            // download from here. prepareForActive() is a no-op on a
            // cache hit, so re-activating the tool stays cheap.
            if ((tool == AnnotationTool::InstantAlpha || tool == AnnotationTool::SmartLasso) &&
                m_samController && m_samController->isModelReady() &&
                m_app->settings().mlPreloadSegmentationOnToolActivation()) {
                if (auto *imgDoc = dynamic_cast<ImageDocument *>(doc)) {
                    const QImage image = imgDoc->image();
                    if (!image.isNull()) {
                        // prepareForActive() hashes the image once and
                        // short-circuits synchronously on a cache hit, so we
                        // call it unconditionally — an isCachedForActive()
                        // pre-check would just hash the image a second time.
                        m_samController->prepareForActive(image, [](bool) {});
                    }
                }
            }
        }
    });
    // When the toolbar is hidden, reset the active tool to Select so the
    // annotation overlay stops capturing click-drags to draw shapes.
    // Select routes drag events to QPdfDocument::getSelection (text
    // selection); any other tool would consume the drag and paint a
    // rectangle in the last-used stroke colour, which is what a user
    // with a hidden toolbar would experience as "I can't select text
    // any more". QToolBar::visibilityChanged fires for both the
    // user-toggled hide and any programmatic one.
    connect(m_markupToolbar, &QToolBar::visibilityChanged, this, [this](bool visible) {
        if (visible)
            return;
        if (m_markupToolbar->activeTool() != AnnotationTool::Select) {
            m_markupToolbar->setActiveTool(AnnotationTool::Select);
        }
    });
    connect(m_markupToolbar, &MarkupToolbar::styleChanged, this,
            [this](const AnnotationStyle &style) {
                if (auto *doc = m_documentView->currentDocument()) {
                    doc->setAnnotationStyle(style);
                }
            });

    m_formToolbar = new FormToolbar(this);
    // Pin the form toolbar's overflow chevron too (ADR 0007, R2).
    m_formToolbar->setStyleSheet(kToolbarExtensionPinStyle());
    addToolBar(Qt::TopToolBarArea, m_formToolbar);
    // Force the form toolbar onto its own row so it never shares a
    // line with the markup bar (mutual exclusion below should keep
    // both from being visible at once, but this is the belt-and-
    // suspenders against a future caller that shows them anyway).
    insertToolBarBreak(m_formToolbar);
    m_formToolbar->hide();
    // Markup and form-filling are different workflows: a user
    // marking up annotations isn't filling form fields, and vice
    // versa. Treat them as mutually exclusive so they never pile up
    // and obscure each other. We listen on visibilityChanged rather
    // than the toggle action so the auto-show paths
    // (onCurrentDocumentChanged → m_markupToolbar->show()) get the
    // same exclusion treatment.
    connect(m_markupToolbar, &QToolBar::visibilityChanged, this, [this](bool visible) {
        if (visible && m_formToolbar->isVisible()) {
            m_formToolbar->hide();
        }
    });
    connect(m_formToolbar, &QToolBar::visibilityChanged, this, [this](bool visible) {
        if (visible && m_markupToolbar->isVisible()) {
            m_markupToolbar->hide();
        }
    });
    connect(m_formToolbar, &FormToolbar::toolChanged, this,
            [this](AnnotationTool tool, const QString &pendingText) {
                if (auto *doc = m_documentView->currentDocument()) {
                    doc->setAnnotationTool(tool);
                    doc->setPendingAnnotationText(pendingText);
                }
            });
    connect(m_formToolbar, &FormToolbar::autoFillRequested, this,
            &MainWindow::onAutoFillCurrentForm);
    connect(m_formToolbar, &FormToolbar::signHereRequested, this, &MainWindow::onSignHere);

    buildMenus();
    rebuildRecentMenu();
    onCurrentDocumentChanged(nullptr);

    // Tiny permanent widget on the right edge of the status bar that
    // shows whenever MlScheduler has a non-Idle task running. The
    // refresh closure lives next to the widget so we don't pay the
    // bookkeeping cost of a member slot. Connected via the
    // scheduler's statsChanged() signal which is queued from the
    // worker thread; the lambda runs on the GUI thread and is safe
    // to touch QLabel state. No-op when the scheduler is idle.
    m_mlIndicator = new QLabel(QStringLiteral("ML"), this);
    m_mlIndicator->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    m_mlIndicator->setMargin(2);
    m_mlIndicator->setVisible(false);
    statusBar()->addPermanentWidget(m_mlIndicator);
    auto refreshMlIndicator = [this]() {
        const auto stats = m_app->mlScheduler().stats();
        // Idle priority is reserved for "we don't care if this never
        // runs" speculative work — the user doesn't need feedback for
        // it, so leave the indicator hidden. Any priority above Idle
        // shows up.
        const bool show = stats.running > 0 && stats.runningPriority != MlPriority::Idle;
        m_mlIndicator->setVisible(show);
        if (show) {
            m_mlIndicator->setToolTip(stats.runningLabel);
        } else {
            m_mlIndicator->setToolTip(QString());
        }
    };
    connect(&m_app->mlScheduler(), &MlScheduler::statsChanged, this, refreshMlIndicator,
            Qt::QueuedConnection);

    // Large-doc OCR hint chip. Hidden by default; shown when the
    // current document has more pages than the auto-OCR pump will
    // handle (>50) and the visible page has no cached OCR results.
    // The chip's link triggers a UserAction OCR for the visible
    // page. PHILOSOPHY: no popup that says "no" — this is the
    // affordance to *do* the thing, not the modal that says we
    // didn't.
    auto *hint = new QWidget(this);
    hint->setObjectName(QStringLiteral("largeDocOcrHint"));
    auto *hintLayout = new QHBoxLayout(hint);
    hintLayout->setContentsMargins(0, 0, 0, 0);
    hintLayout->setSpacing(4);
    // Benefit-first wording (ADR 0002 §3 / ADR 0006): the link routes
    // through the same one-time-consent download flow the compliant
    // paths use — no "model"/"OCR" jargon, no silent no-op.
    auto *hintLabel = new QLabel(tr("This page's text isn't selectable yet."), hint);
    auto *hintLink = new QLabel(
        tr("<a href=\"#recognize\">Recognize text on this page</a>"), hint);
    hintLink->setObjectName(QStringLiteral("largeDocOcrHintLink"));
    hintLink->setTextFormat(Qt::RichText);
    hintLink->setOpenExternalLinks(false);
    connect(hintLink, &QLabel::linkActivated, this, [this](const QString &) {
        auto *doc = m_documentView->currentDocument();
        if (!doc || !doc->supportsSelectableText())
            return;
        // Route through the sanctioned consent/download gate (ADR 0006):
        // mirror onRecognizeText and the compliant m_ocrModelMissingHint
        // handler rather than calling submitUserPages() directly (which
        // silently no-ops when the language model is absent). The test
        // seam intercepts the gate so the routing is verifiable without a
        // real modal.
        bool ready;
        if (m_ocrModelDownloadHook) {
            ready = m_ocrModelDownloadHook();
        } else {
            OcrEngine gateEngine(&m_app->modelRegistry());
            ready = ensureOcrModelsReady(this, gateEngine);
        }
        if (!ready)
            return;
        const int page = doc->currentPage();
        m_ocrController->submitUserPages(doc, {page}, /*forceRerun=*/false);
    });
    // Dismiss (×) affordance — a permanent status-bar widget with no way
    // out reads as a stuck control (ADR 0006). Dismissal is per-document
    // (reset on document change) so it stays hidden for the doc the user
    // dismissed it on but returns for the next document.
    auto *hintDismiss = new QToolButton(hint);
    hintDismiss->setText(QStringLiteral("×"));
    hintDismiss->setAutoRaise(true);
    hintDismiss->setToolTip(tr("Dismiss"));
    hintDismiss->setObjectName(QStringLiteral("largeDocOcrHintDismiss"));
    connect(hintDismiss, &QToolButton::clicked, this, [this]() {
        if (auto *doc = m_documentView->currentDocument())
            m_largeDocOcrHintDismissed.insert(doc);
        if (m_largeDocOcrHint)
            m_largeDocOcrHint->setVisible(false);
    });
    hintLayout->addWidget(hintLabel);
    hintLayout->addWidget(hintLink);
    hintLayout->addWidget(hintDismiss);
    hint->setVisible(false);
    statusBar()->addPermanentWidget(hint);
    m_largeDocOcrHint = hint;
    refreshMlIndicator();

    // ADR 0002: richer progress+cancel widget for foreground ML ops.
    // Sits next to the ambient m_mlIndicator dot (which stays untouched).
    // OcrController's batch signals drive it; the reveal is delayed so
    // sub-threshold batches never flicker it. See wiring below.
    m_mlProgress = new MlProgressWidget(this);
    statusBar()->addPermanentWidget(m_mlProgress);
    // ADR 0002 §1: elapsed-time reassurance for INDETERMINATE reveals.
    // Ticks once a second while a single-page / unknown-length op is
    // revealed and appends "· Ns" past 10s. Started in the indeterminate
    // branch below; stopped on finish/abort so it never runs while idle.
    m_ocrElapsedTimer = new QTimer(this);
    m_ocrElapsedTimer->setInterval(1000);
    connect(m_ocrElapsedTimer, &QTimer::timeout, this, [this]() {
        ++m_ocrElapsedSecs;
        m_mlProgress->setElapsedSeconds(m_ocrElapsedSecs);
    });
    connect(m_ocrController, &OcrController::ocrBatchStarted, this, [this](int total) {
        m_ocrPendingTotal = total;
        m_ocrPendingCompleted = 0;
        m_ocrRevealed = false;
        if (m_cancelMlAction)
            m_cancelMlAction->setEnabled(true);
    });
    connect(m_ocrController, &OcrController::ocrBatchProgress, this,
            [this](int completed, int total) {
                m_ocrPendingTotal = total;
                m_ocrPendingCompleted = completed;
                if (m_ocrRevealed)
                    m_mlProgress->setProgress(completed);
            });
    connect(m_ocrController, &OcrController::ocrBatchShouldReveal, this, [this]() {
        m_ocrRevealed = true;
        if (m_ocrPendingTotal >= 2) {
            m_ocrElapsedTimer->stop();
            m_mlProgress->beginDeterminate(tr("Recognising text"), m_ocrPendingTotal);
            m_mlProgress->setProgress(m_ocrPendingCompleted);
        } else {
            m_mlProgress->beginIndeterminate(tr("Recognising text"));
            m_ocrElapsedSecs = 0;
            m_ocrElapsedTimer->start();
        }
    });
    connect(m_ocrController, &OcrController::ocrBatchFinished, this,
            [this](bool cancelled, int blockCount) {
        m_ocrElapsedTimer->stop();
        if (m_cancelMlAction)
            m_cancelMlAction->setEnabled(false);
        if (m_ocrRevealed) {
            // Honest completion: a zero-block run must not claim success — it
            // says "No text found" (Item C). blockCount is the store's block
            // total across the batch's pages, reported by the controller.
            m_mlProgress->finishWithMessage(recognizeCompletionMessage(cancelled, blockCount));
        }
        m_ocrRevealed = false;
    });
    // Silent teardown (supersede / document switch or close): drive the
    // widget straight back to idle with NO terminal message and disable the
    // scoped cancel action so ⌘. never points at a dead batch (ADR 0002
    // review items 2/7).
    connect(m_ocrController, &OcrController::ocrBatchAborted, this, [this]() {
        m_ocrElapsedTimer->stop();
        m_ocrRevealed = false;
        m_mlProgress->goIdle();
        if (m_cancelMlAction)
            m_cancelMlAction->setEnabled(false);
    });
    connect(m_mlProgress, &MlProgressWidget::cancelRequested, m_ocrController,
            &OcrController::cancelActiveBatch);

    // ADR 0002: Esc is intentionally NOT globally bound to cancel — it is
    // overloaded (find bar, popovers) and a bare-Esc global cancel is the
    // accidental-loss trap flagged in the persona review. The ✕ button and
    // ⌘. (Ctrl+.) cover the cancel gesture (B6); ⌘. is scoped to a running
    // foreground op via setEnabled() above.
    m_cancelMlAction = new QAction(tr("Cancel ML Operation"), this);
    m_cancelMlAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Period));
    m_cancelMlAction->setEnabled(false);
    connect(m_cancelMlAction, &QAction::triggered, m_ocrController,
            &OcrController::cancelActiveBatch);
    addAction(m_cancelMlAction);

    // ADR 0002 §3: non-modal in-context hint shown when auto-OCR would run
    // but the language model is absent. Benefit-first wording, no jargon;
    // the link enters the sanctioned one-time-consent download flow (the
    // only popup allowed here). State-driven via autoOcrModelMissing().
    m_ocrModelMissingHint = new QLabel(
        tr("This document's text isn't searchable — "
           "<a href=\"#install\">install language pack</a> to recognise it."),
        this);
    m_ocrModelMissingHint->setObjectName(QStringLiteral("ocrModelMissingHint"));
    m_ocrModelMissingHint->setTextFormat(Qt::RichText);
    m_ocrModelMissingHint->setOpenExternalLinks(false);
    m_ocrModelMissingHint->setVisible(false);
    connect(m_ocrModelMissingHint, &QLabel::linkActivated, this, [this](const QString &) {
        // The hint link routes into the sanctioned one-time download-consent
        // flow (ensureOcrModelsReady → requestModelDownload). A test seam may
        // intercept it so the routing can be verified without a real modal.
        bool ready;
        if (m_ocrModelDownloadHook) {
            ready = m_ocrModelDownloadHook();
        } else {
            OcrEngine gateEngine(&m_app->modelRegistry());
            ready = ensureOcrModelsReady(this, gateEngine);
        }
        if (ready) {
            // Model now present — re-derive so the hint hides and auto-OCR
            // resumes for the visible page.
            auto *doc = m_documentView->currentDocument();
            if (doc && doc->supportsSelectableText())
                m_ocrController->onVisiblePageChanged(doc->currentPage());
        }
    });
    statusBar()->addPermanentWidget(m_ocrModelMissingHint);
    connect(m_ocrController, &OcrController::autoOcrModelMissing, this,
            [this](bool missing) { m_ocrModelMissingHint->setVisible(missing); });

    // ADR 0002 §3: re-derive auto-OCR / the missing-model hint on PAGE
    // change, not just document change. IDocument is not a QObject and
    // exposes no page-changed signal, so — mirroring Sidebar's
    // m_pageSyncTimer — poll the current page at a light cadence and notify
    // the controller only when it actually changes (scrolling from a text
    // page to a scanned page must surface the hint).
    m_ocrPagePoll = new QTimer(this);
    m_ocrPagePoll->setInterval(150);
    connect(m_ocrPagePoll, &QTimer::timeout, this, [this]() {
        auto *doc = m_documentView->currentDocument();
        if (!doc || !m_ocrController)
            return;
        // Re-derive the large-doc recognize notice every tick so it self-
        // clears the moment the visible page gains text / OCR results
        // (ADR 0006). The helper short-circuits on the cheap guards before
        // it ever probes pageHasText(), so this stays light.
        updateLargeDocOcrHint();
        const int page = doc->currentPage();
        if (page == m_lastOcrPage)
            return;
        m_lastOcrPage = page;
        m_ocrController->onVisiblePageChanged(page);
    });
    m_ocrPagePoll->start();

    // Auto-save loop. Tick every 30 s; each tick saves any document
    // that is dirty AND has been saved at least once (filePath() is
    // non-empty). Untitled documents are skipped — auto-save
    // shouldn't pick a destination for the user. The timer respects
    // Settings::autoSave and pauses while the user has it disabled.
    constexpr int kAutoSaveIntervalMs = 30 * 1000;
    m_autoSaveTimer = new QTimer(this);
    m_autoSaveTimer->setInterval(kAutoSaveIntervalMs);
    connect(m_autoSaveTimer, &QTimer::timeout, this, &MainWindow::autoSaveDirtyDocs);
    m_autoSaveTimer->start();

    // Initial central-stack state: a freshly-spawned window holds no
    // document, so show the empty-state welcome surface.
    updateEmptyState();
}

void MainWindow::autoSaveDirtyDocs() {
    if (!m_app->settings().autoSave())
        return;
    const int total = m_documentView->documentCount();
    bool savedAny = false;
    for (int i = 0; i < total; ++i) {
        IDocument *doc = nullptr;
        if (!m_documentView->documentAt(i, &doc) || !doc)
            continue;
        if (!doc->isDirty() || doc->filePath().isEmpty())
            continue;
        if (doc->save()) {
            savedAny = true;
        }
    }
    if (savedAny) {
        updateTitleForDocument(m_documentView->currentDocument());
        flashSuccess(tr("Auto-saved."));
    }
}

void MainWindow::buildMenus() {
    auto *fileMenu = menuBar()->addMenu(tr("&File"));
    // The disabled Share… item's tooltip is made visible by
    // makeDisabledAction() at its creation below (it calls
    // fileMenu->setToolTipsVisible(true)), so no explicit call is needed here.

    auto *openAction = fileMenu->addAction(tr("&Open…"));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::onOpen);

    m_recentMenu = fileMenu->addMenu(tr("Open &Recent"));
    fileMenu->addSeparator();

    m_saveAction = fileMenu->addAction(tr("&Save"));
    m_saveAction->setShortcut(QKeySequence::Save);
    connect(m_saveAction, &QAction::triggered, this, &MainWindow::onSave);

    m_saveAsAction = fileMenu->addAction(tr("Save &As…"));
    m_saveAsAction->setShortcut(QKeySequence::SaveAs);
    connect(m_saveAsAction, &QAction::triggered, this, &MainWindow::onSaveAs);

    m_exportPasswordProtectedAction = fileMenu->addAction(tr("Export as &Password-Protected PDF…"));
    connect(m_exportPasswordProtectedAction, &QAction::triggered, this,
            &MainWindow::onExportPasswordProtected);

    m_reduceFileSizeAction = fileMenu->addAction(tr("Reduce File &Size…"));
    connect(m_reduceFileSizeAction, &QAction::triggered, this, &MainWindow::onReduceFileSize);
    fileMenu->addSeparator();

    m_printAction = fileMenu->addAction(tr("&Print…"));
    m_printAction->setShortcut(QKeySequence::Print);
    connect(m_printAction, &QAction::triggered, this, [this]() {
        if (auto *doc = m_documentView->currentDocument()) {
            doc->print(this);
        }
    });

    // File → Share routes through the OS share-sheet (macOS native
    // NSSharingServicePicker). The Linux/Windows stub returns
    // isAvailable() == false until xdg-email / WinShare are wired
    // up. When available, the action is gated on a saved file because
    // the share picker needs a real file path on disk.
    // The action is always created so the capability is visible. On
    // platforms without a working share implementation it is shown
    // disabled with a tooltip pointing at the real alternative, rather
    // than silently hidden (owner policy: unavailable capabilities are
    // disabled + explained, never dropped from the menu).
    // Created via makeDisabledAction so the File menu is guaranteed to
    // render this disabled-state tooltip (G3). On platforms where sharing
    // works we clear the explanation and re-enable; updateActionStates()
    // then gates it on whether the document has a file path.
    m_shareAction = makeDisabledAction(
        fileMenu, tr("&Share…"),
        tr("Sharing isn't available on this platform yet — use File → "
           "Save As… to save a copy you can share."));
    if (ShareService::isAvailable()) {
        m_shareAction->setToolTip(QString());
        m_shareAction->setEnabled(true);
        connect(m_shareAction, &QAction::triggered, this, [this]() {
            auto *doc = m_documentView->currentDocument();
            if (!doc)
                return;
            const QString path = doc->filePath();
            if (path.isEmpty() || doc->isDirty()) {
                flashStatus(tr("Save the document before sharing."));
                return;
            }
            ShareService::shareFile(path, this);
        });
    }

    fileMenu->addSeparator();

    auto *closeAction = fileMenu->addAction(tr("&Close Window"));
    closeAction->setShortcut(QKeySequence::Close);
    connect(closeAction, &QAction::triggered, this, &QMainWindow::close);

    auto *quitAction = fileMenu->addAction(tr("&Quit"));
    quitAction->setShortcut(QKeySequence::Quit);
    quitAction->setMenuRole(QAction::QuitRole);
    connect(quitAction, &QAction::triggered, qApp, &QCoreApplication::quit);

    auto *editMenu = menuBar()->addMenu(tr("&Edit"));
    buildEditMenu(editMenu);

    auto *viewMenu = menuBar()->addMenu(tr("&View"));
    buildViewMenu(viewMenu);

    auto *goMenu = menuBar()->addMenu(tr("&Go"));
    buildGoMenu(goMenu);

    auto *toolsMenu = menuBar()->addMenu(tr("&Tools"));
    buildToolsMenu(toolsMenu);

    auto *windowMenu = menuBar()->addMenu(tr("&Window"));
    buildWindowMenu(windowMenu);

    auto *helpMenu = menuBar()->addMenu(tr("&Help"));
    auto *aboutAction = helpMenu->addAction(tr("&About Trailer"));
    aboutAction->setMenuRole(QAction::AboutRole);
    connect(aboutAction, &QAction::triggered, this, &MainWindow::onAbout);

    buildMainToolbar();
}

void MainWindow::buildMainToolbar() {
    // Slim always-visible toolbar that hosts document chrome:
    // sidebar mode picker, zoom, rotate, markup / forms toggles,
    // and an embedded search field. Sits above any markup or form
    // toolbar (insertToolBarBreak gives those their own row when
    // they're shown). The toolbar is non-movable / non-floatable
    // because letting the user drag it produces an empty band of
    // chrome that's just visual noise.
    m_mainToolbar = new QToolBar(tr("Main"), this);
    m_mainToolbar->setObjectName(QStringLiteral("MainToolbar"));
    m_mainToolbar->setMovable(false);
    m_mainToolbar->setFloatable(false);
    // Suppress the per-toolbar dock context menu; we never want the
    // user to right-click and hide the chrome that hosts sidebar /
    // zoom / search.
    m_mainToolbar->setContextMenuPolicy(Qt::PreventContextMenu);
    m_mainToolbar->setIconSize(QSize(18, 18));
    // Icon-only is the screen-real-estate win. Action text labels are
    // still set (and surface as hover tooltips via QAction's default
    // behaviour); they just don't render next to the glyph. The menu
    // bar remains the discoverable surface for unfamiliar users.
    m_mainToolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    // Pin the main toolbar's overflow chevron too (ADR 0007, R2).
    m_mainToolbar->setStyleSheet(kToolbarExtensionPinStyle());
    // Anchor the main toolbar top-left on its own row (ADR 0007,
    // Option A, R1). The top area was appended markup, form, main —
    // so main, added last, ended up as a tenant on the form toolbar's
    // row and got shoved right whenever the form bar was shown.
    // insertToolBar puts main at the FRONT of the top-area order
    // (main, markup, form); with a break before markup (below) and
    // before form (insertToolBarBreak(m_formToolbar), ~:359) and NO
    // break before main, main owns row 1
    // permanently while markup or form take row 2.
    insertToolBar(m_markupToolbar, m_mainToolbar);
    insertToolBarBreak(m_markupToolbar);

    // Sidebar mode picker. Each entry calls Sidebar::setMode; the
    // checked state mirrors back via Sidebar::modeChanged.
    auto *sidebarBtn = new QToolButton(m_mainToolbar);
    sidebarBtn->setText(tr("Sidebar"));
    sidebarBtn->setIcon(
        themedActionIcon(QStringLiteral(":/icons/actions/panel-sidebar.svg"), m_mainToolbar));
    sidebarBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    sidebarBtn->setToolTip(tr("Sidebar"));
    sidebarBtn->setPopupMode(QToolButton::InstantPopup);
    auto *sidebarMenu = new QMenu(sidebarBtn);
    auto *sidebarGroup = new QActionGroup(sidebarMenu);
    sidebarGroup->setExclusive(true);
    auto addModeAction = [this, sidebarMenu, sidebarGroup](const QString &label, Sidebar::Mode mode,
                                                           bool enabled = true) {
        QAction *a = sidebarMenu->addAction(label);
        a->setCheckable(true);
        a->setEnabled(enabled);
        sidebarGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, mode]() { m_sidebar->setMode(mode); });
        return a;
    };
    QAction *hideAction = addModeAction(tr("Hide Sidebar"), Sidebar::Mode::Hidden);
    addModeAction(tr("Page Thumbnails"), Sidebar::Mode::Pages);
    addModeAction(tr("Search Results"), Sidebar::Mode::SearchResults);
    sidebarMenu->addSeparator();
    // Table of Contents is enabled on documents whose outline model
    // has rows. The picker entry's enabled state is refreshed on
    // every doc change in onCurrentDocumentChanged.
    m_tocSidebarAction = addModeAction(tr("Table of Contents"), Sidebar::Mode::TableOfContents,
                                       /*enabled=*/false);
    m_highlightsAndNotesSidebarAction = addModeAction(
        tr("Highlights && Notes"), Sidebar::Mode::HighlightsAndNotes, /*enabled=*/false);
    hideAction->setChecked(true); // Hidden is the launch default
    sidebarBtn->setMenu(sidebarMenu);
    m_mainToolbar->addWidget(sidebarBtn);
    // Keep the picker's check-state in sync if the user closes the
    // dock via its X button or the View → Toggle Sidebar action.
    connect(m_sidebar, &Sidebar::modeChanged, this, [sidebarGroup](Sidebar::Mode m) {
        for (QAction *a : sidebarGroup->actions()) {
            if (a->isCheckable() && a->data().value<Sidebar::Mode>() == m) {
                a->setChecked(true);
                return;
            }
        }
        // Fallback: just clear all checks.
        for (QAction *a : sidebarGroup->actions())
            a->setChecked(false);
    });
    // Tag each action with its Mode so the lookup above works.
    {
        const QList<QAction *> actions = sidebarGroup->actions();
        const Sidebar::Mode modes[] = {Sidebar::Mode::Hidden, Sidebar::Mode::Pages,
                                       Sidebar::Mode::SearchResults, Sidebar::Mode::TableOfContents,
                                       Sidebar::Mode::HighlightsAndNotes};
        for (int i = 0; i < actions.size() && i < int(sizeof(modes) / sizeof(modes[0])); ++i) {
            actions[i]->setData(QVariant::fromValue(modes[i]));
        }
    }

    m_mainToolbar->addSeparator();

    m_mainToolbar->addAction(m_zoomOutAction);
    m_mainToolbar->addAction(m_zoomActualAction);
    m_mainToolbar->addAction(m_zoomInAction);
    m_mainToolbar->addSeparator();
    m_mainToolbar->addAction(m_rotateLeftAction);
    m_mainToolbar->addAction(m_rotateRightAction);
    m_mainToolbar->addSeparator();
    m_mainToolbar->addAction(m_markupToolbarAction);
    m_mainToolbar->addAction(m_formToolbarAction);

    // Stretchable spacer pushes the search field to the right edge.
    auto *spacer = new QWidget(m_mainToolbar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_mainToolbar->addWidget(spacer);

    // Search collapses to an icon button by default. The button sits
    // on the right of the main toolbar; clicking it (or Ctrl+F)
    // expands the SearchBar in place and hides the button. Esc /
    // empty query restore the button. Keeping the bar reparented to
    // the toolbar (rather than swapping widgets) preserves the
    // signals and counter state wired up in the constructor.
    m_searchButton = new QToolButton(m_mainToolbar);
    m_searchButton->setIcon(
        themedActionIcon(QStringLiteral(":/icons/actions/view-search.svg"), m_mainToolbar));
    m_searchButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_searchButton->setToolTip(
        tr("Search (%1)").arg(QKeySequence(QKeySequence::Find).toString(QKeySequence::NativeText)));
    // Icon-only with no text would read as a bare "button" to a screen
    // reader; give it an explicit accessible name (audit A-CRIT-1).
    m_searchButton->setAccessibleName(tr("Search"));
    connect(m_searchButton, &QToolButton::clicked, this, &MainWindow::showSearchBar);
    m_mainToolbar->addWidget(m_searchButton);

    m_searchBar->setParent(m_mainToolbar);
    m_searchBar->setMaximumWidth(360);
    m_searchBarAction = m_mainToolbar->addWidget(m_searchBar);
    // hide() after addWidget so the explicit-hidden flag is set
    // against the just-reparented widget. QToolBar::addWidget wraps
    // the bar in a QWidgetAction that drives visibility from its
    // parent, so the hide() has to follow the addWidget call to
    // win the race on first show. The wrapping action also has to be
    // hidden, otherwise its toolbar slot stays reserved (empty gap).
    m_searchBar->hide();
    if (m_searchBarAction) {
        m_searchBarAction->setVisible(false);
    }

    // Guarantee the primary row never overflows its trailing search
    // into its own chevron (ADR 0007, Option A, R3). Qt overflows the
    // trailing-most items first, and search is the last widget on the
    // main row, so without a floor a narrow-enough window would hide
    // search behind the main toolbar's own "show more" menu — breaking
    // the HIG rule that trailing items stay visible at every window
    // size. Measuring sizeHint() with the search collapsed to its icon
    // button (its action is hidden above) UNDER-counts the footprint the
    // row needs once the user OPENS Find: showSearchBar() hides the
    // ~icon-sized button and expands the far wider SearchBar (maxWidth
    // 360). To pin the floor to the OPENED-search footprint, temporarily
    // reveal the search-bar action (and hide the button) exactly as
    // showSearchBar() does, re-measure the toolbar's sizeHint(), then
    // restore the collapsed state. Using the opened sizeHint (not the
    // SearchBar's bare minimumSizeHint) accounts for the toolbar's own
    // layout margins/spacing, so the primary row fits the opened search
    // at the minimum width with no off-by-margin overflow. This stays
    // well under the launch resize(1100, 750) so it never fights it, and
    // above the sidebar's 240px minimum.
    m_mainToolbar->ensurePolished();
    m_searchBar->ensurePolished();
    const int primaryRowWidth = m_mainToolbar->sizeHint().width();
    if (primaryRowWidth > 0) {
        m_searchButton->setVisible(false);
        if (m_searchBarAction)
            m_searchBarAction->setVisible(true);
        m_searchBar->setVisible(true);
        m_mainToolbar->layout()->activate();
        const int openedSearchFloor = m_mainToolbar->sizeHint().width();
        // Restore the collapsed default (button shown, bar hidden).
        m_searchBar->hide();
        if (m_searchBarAction)
            m_searchBarAction->setVisible(false);
        m_searchButton->setVisible(true);
        m_mainToolbar->layout()->activate();
        setMinimumWidth(qMax(primaryRowWidth, openedSearchFloor));
    }
}

void MainWindow::buildEditMenu(QMenu *editMenu) {
    // Tooltips on disabled entries (e.g. Copy Page as Image) are enabled by
    // makeDisabledAction() when that action is created below, so the greyed-
    // out reason stays discoverable on hover without an explicit call here.

    m_undoAction = editMenu->addAction(tr("&Undo"));
    m_undoAction->setShortcut(QKeySequence::Undo);
    connect(m_undoAction, &QAction::triggered, this, [this]() {
        if (auto *doc = m_documentView->currentDocument()) {
            // A false return while canUndo() promised an entry means a
            // log/stack desync guard refused and dropped the orphaned
            // entry (see IDocument::undo()). Without this flash the
            // only trace is a qWarning and the keypress is silently
            // dead; onCurrentDocumentChanged below re-syncs the action
            // enabled state either way.
            const bool promised = doc->canUndo();
            if (!doc->undo() && promised) {
                flashError(tr("Undo history was out of sync; nothing was undone."));
            }
            m_sidebar->refreshThumbnails();
            updateTitleForDocument(doc);
            onCurrentDocumentChanged(doc);
        }
    });

    m_redoAction = editMenu->addAction(tr("&Redo"));
    m_redoAction->setShortcut(QKeySequence::Redo);
    connect(m_redoAction, &QAction::triggered, this, [this]() {
        if (auto *doc = m_documentView->currentDocument()) {
            // Mirror of the undo handler's desync flash above.
            const bool promised = doc->canRedo();
            if (!doc->redo() && promised) {
                flashError(tr("Redo history was out of sync; nothing was redone."));
            }
            m_sidebar->refreshThumbnails();
            updateTitleForDocument(doc);
            onCurrentDocumentChanged(doc);
        }
    });

    editMenu->addSeparator();

    m_selectAllAction = editMenu->addAction(tr("Select &All"));
    m_selectAllAction->setShortcut(QKeySequence::SelectAll);
    connect(m_selectAllAction, &QAction::triggered, this, [this]() {
        if (auto *overlay = findChild<AnnotationOverlay *>()) {
            overlay->selectAll();
        }
    });

    // Starts disabled with an explanatory tooltip via makeDisabledAction
    // (which also enables tooltips on the Edit menu); updateActionStates()
    // enables it whenever the document can render a page raster.
    m_copyPageAction = makeDisabledAction(
        editMenu, tr("Copy Page as &Image"),
        tr("Copy the current page to the clipboard as an image — available "
           "when the document can render a page raster."));
    connect(m_copyPageAction, &QAction::triggered, this, &MainWindow::onCopyPageAsImage);

    editMenu->addSeparator();

    m_findAction = editMenu->addAction(tr("&Find…"));
    m_findAction->setShortcut(QKeySequence::Find);
    connect(m_findAction, &QAction::triggered, this, &MainWindow::showSearchBar);

    m_findNextAction = editMenu->addAction(tr("Find &Next"));
    m_findNextAction->setShortcut(QKeySequence::FindNext);
    connect(m_findNextAction, &QAction::triggered, this, [this]() {
        if (auto *doc = m_documentView->currentDocument())
            doc->findNext();
    });

    m_findPreviousAction = editMenu->addAction(tr("Find &Previous"));
    m_findPreviousAction->setShortcut(QKeySequence::FindPrevious);
    connect(m_findPreviousAction, &QAction::triggered, this, [this]() {
        if (auto *doc = m_documentView->currentDocument())
            doc->findPrevious();
    });

    editMenu->addSeparator();

    auto *prefsAction = editMenu->addAction(tr("&Preferences…"));
    prefsAction->setShortcut(QKeySequence::Preferences);
    prefsAction->setMenuRole(QAction::PreferencesRole);
    connect(prefsAction, &QAction::triggered, this, &MainWindow::onOpenPreferences);
}

void MainWindow::showSearchBar() {
    // Expand the collapsed SearchBar inline in the main toolbar. The
    // search button hides; the bar (parented at toolbar build time)
    // unhides and takes focus. supportsSearch gate stays — for
    // documents that can't search, focusing the field would invite
    // the user to type into a no-op.
    auto *doc = m_documentView->currentDocument();
    if (!doc || !doc->supportsSearch()) {
        return;
    }
    if (m_searchButton) {
        m_searchButton->setVisible(false);
    }
    if (m_searchBarAction) {
        m_searchBarAction->setVisible(true);
    }
    m_searchBar->setVisible(true);
    m_searchBar->focusInput();
    // Open the sidebar in Search Results mode so the user sees
    // pages-with-matches as they type. The polling timer pushes
    // the current pagesWithSearchMatches() list into the sidebar
    // every 150 ms while a query is active.
    if (m_sidebar->mode() == Sidebar::Mode::Hidden) {
        m_sidebar->setMode(Sidebar::Mode::SearchResults);
    }
}

void MainWindow::hideSearchBar() {
    // Collapse the bar back to the icon button. Clears any active
    // query so the highlighted matches go away and the document gets
    // focus back. Triggered by SearchBar::dismissed (Esc, X button)
    // and by a document switch that doesn't support search.
    if (auto *doc = m_documentView->currentDocument()) {
        doc->clearSearch();
    }
    m_searchBar->setMatchCounter(0, 0);
    m_searchBar->setVisible(false);
    if (m_searchBarAction) {
        m_searchBarAction->setVisible(false);
    }
    if (m_searchButton) {
        m_searchButton->setVisible(true);
    }
    m_documentView->setFocus();
}

void MainWindow::buildViewMenu(QMenu *viewMenu) {
    // The disabled Two Pages item carries an explanatory tooltip; its
    // creation via makeDisabledAction() below enables tooltips on this
    // menu, so no explicit setToolTipsVisible() call is needed here.
    auto *toggleSidebar = m_sidebar->toggleViewAction();
    toggleSidebar->setText(tr("Toggle &Sidebar"));
    toggleSidebar->setShortcut(QKeySequence(tr("Ctrl+Shift+D")));
    viewMenu->addAction(toggleSidebar);

    m_markupToolbarAction = m_markupToolbar->toggleViewAction();
    m_markupToolbarAction->setText(tr("Toggle &Markup Toolbar"));
    m_markupToolbarAction->setIcon(
        themedActionIcon(QStringLiteral(":/icons/actions/panel-markup.svg"), this));
    m_markupToolbarAction->setShortcut(QKeySequence(tr("Ctrl+Shift+A")));
    viewMenu->addAction(m_markupToolbarAction);

    m_formToolbarAction = m_formToolbar->toggleViewAction();
    m_formToolbarAction->setText(tr("Show Form Filling &Toolbar"));
    m_formToolbarAction->setIcon(
        themedActionIcon(QStringLiteral(":/icons/actions/panel-form.svg"), this));
    m_formToolbarAction->setShortcut(QKeySequence(tr("Ctrl+Shift+B")));
    viewMenu->addAction(m_formToolbarAction);

    m_inspectorAction = m_inspector->toggleViewAction();
    m_inspectorAction->setText(tr("&Inspector"));
    // Cmd+I on macOS / Ctrl+I elsewhere — the convention in every
    // Mac app of this shape (Preview, Pages, Numbers). The longer
    // ⌘⇧I form was a holdover from an earlier draft and conflicted
    // with the muscle memory the reference user already had.
    m_inspectorAction->setShortcut(QKeySequence(tr("Ctrl+I")));
    viewMenu->addAction(m_inspectorAction);

    viewMenu->addSeparator();

    // Page-layout shortcuts live on Cmd-1/2/3; the zoom commands below
    // moved off the digit row (Cmd-0 Actual Size, Cmd-9 Fit Page) to
    // make room. Cmd-1 → Continuous (Trailer's default mode), Cmd-2 →
    // Single Page, Cmd-3 → Two Pages.
    m_singlePageAction = viewMenu->addAction(tr("Single Page"));
    m_singlePageAction->setCheckable(true);
    m_singlePageAction->setShortcut(QKeySequence(tr("Ctrl+2")));
    connect(m_singlePageAction, &QAction::triggered, this, [this]() {
        if (auto *doc = m_documentView->currentDocument()) {
            doc->setViewMode(ViewMode::SinglePage);
        }
    });

    // Cmd-3 is reserved here but the action stays disabled: Qt's
    // QPdfView::PageMode only exposes SinglePage and MultiPage — there is
    // no facing/two-up layout. PdfDocument::applyViewMode warns and no-ops
    // on ViewMode::TwoPages (it deliberately does not alias Continuous), so
    // enabling this action would be a no-op. A real side-by-side layout
    // needs a custom view (tracked as a larger follow-up); once it lands
    // and the action is enabled, Cmd-3 starts working.
    // makeDisabledAction starts it disabled with the explanatory tooltip and
    // enables tooltips on the View menu.
    m_twoPagesAction = makeDisabledAction(
        viewMenu, tr("Two Pages"),
        tr("Two-page layout is not yet available."));
    m_twoPagesAction->setCheckable(true);
    m_twoPagesAction->setShortcut(QKeySequence(tr("Ctrl+3")));

    m_continuousAction = viewMenu->addAction(tr("Continuous"));
    m_continuousAction->setCheckable(true);
    m_continuousAction->setShortcut(QKeySequence(tr("Ctrl+1")));
    connect(m_continuousAction, &QAction::triggered, this, [this]() {
        if (auto *doc = m_documentView->currentDocument()) {
            doc->setViewMode(ViewMode::Continuous);
        }
    });

    m_viewModeGroup = new QActionGroup(this);
    m_viewModeGroup->setExclusive(true);
    m_viewModeGroup->addAction(m_singlePageAction);
    m_viewModeGroup->addAction(m_twoPagesAction);
    m_viewModeGroup->addAction(m_continuousAction);

    viewMenu->addSeparator();

    m_previousPageAction = viewMenu->addAction(tr("&Previous Page"));
    m_previousPageAction->setShortcut(QKeySequence(Qt::Key_PageUp));
    connect(m_previousPageAction, &QAction::triggered, this, [this]() {
        if (auto *doc = m_documentView->currentDocument()) {
            doc->goToPage(doc->currentPage() - 1);
        }
    });

    m_nextPageAction = viewMenu->addAction(tr("&Next Page"));
    m_nextPageAction->setShortcut(QKeySequence(Qt::Key_PageDown));
    connect(m_nextPageAction, &QAction::triggered, this, [this]() {
        if (auto *doc = m_documentView->currentDocument()) {
            doc->goToPage(doc->currentPage() + 1);
        }
    });

    viewMenu->addSeparator();

    m_zoomInAction = viewMenu->addAction(
        themedActionIcon(QStringLiteral(":/icons/actions/view-zoom-in.svg"), this), tr("Zoom &In"));
    m_zoomInAction->setShortcuts({
        QKeySequence::ZoomIn,
        QKeySequence(Qt::CTRL | Qt::Key_Equal),
    });
    connect(m_zoomInAction, &QAction::triggered, this, [this]() {
        if (auto *doc = m_documentView->currentDocument())
            doc->zoomIn();
    });

    m_zoomOutAction = viewMenu->addAction(
        themedActionIcon(QStringLiteral(":/icons/actions/view-zoom-out.svg"), this),
        tr("Zoom &Out"));
    m_zoomOutAction->setShortcut(QKeySequence::ZoomOut);
    connect(m_zoomOutAction, &QAction::triggered, this, [this]() {
        if (auto *doc = m_documentView->currentDocument())
            doc->zoomOut();
    });

    // Zoom shortcuts keep the digit row clear for the page-layout
    // commands above. The set is browser-like rather than Acrobat's
    // ⌘0/1/2 triple:
    //   ⌘0 → Actual Size (100%, the "reset zoom" key)
    //   ⌘9 → Fit Page (whole page in viewport)
    //   ⌘+ / ⌘- → Zoom In / Out
    // Fit to Width stays in the menu but no longer carries a digit
    // shortcut (⌘2 is now Single Page).
    m_zoomFitPageAction = viewMenu->addAction(
        themedActionIcon(QStringLiteral(":/icons/actions/view-fit-page.svg"), this),
        tr("Fit &Page"));
    m_zoomFitPageAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_9));
    connect(m_zoomFitPageAction, &QAction::triggered, this, [this]() {
        if (auto *doc = m_documentView->currentDocument())
            doc->zoomFitPage();
    });

    m_zoomActualAction = viewMenu->addAction(
        themedActionIcon(QStringLiteral(":/icons/actions/view-zoom-actual.svg"), this),
        tr("&Actual Size"));
    m_zoomActualAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    connect(m_zoomActualAction, &QAction::triggered, this, [this]() {
        if (auto *doc = m_documentView->currentDocument())
            doc->zoomActual();
    });

    m_zoomFitAction = viewMenu->addAction(
        themedActionIcon(QStringLiteral(":/icons/actions/view-fit-width.svg"), this),
        tr("&Fit to Width"));
    connect(m_zoomFitAction, &QAction::triggered, this, [this]() {
        if (auto *doc = m_documentView->currentDocument())
            doc->zoomFitWidth();
    });

    viewMenu->addSeparator();

    m_magnifierAction = viewMenu->addAction(tr("&Magnifier"));
    m_magnifierAction->setCheckable(true);
    m_magnifierAction->setShortcut(QKeySequence(Qt::Key_QuoteLeft));
    // Cmd-Tab / app-deactivate clears Magnifier so the lens
    // doesn't linger when the user comes back to a different
    // workflow. Esc is handled in keyPressEvent.
    connect(qApp, &QGuiApplication::applicationStateChanged, this,
            [this](Qt::ApplicationState state) {
                if (state != Qt::ApplicationActive && m_magnifierAction &&
                    m_magnifierAction->isChecked()) {
                    m_magnifierAction->setChecked(false);
                }
            });
    connect(m_magnifierAction, &QAction::toggled, this, [this](bool on) {
        if (on) {
            m_magnifier->setTarget(m_documentView->currentWidget());
            m_magnifier->activate();
        } else {
            m_magnifier->deactivate();
        }
    });
}

void MainWindow::buildGoMenu(QMenu *goMenu) {
    // Page navigation. Mirrors Preview.app's Go menu so Mac users
    // bring their muscle memory with them. Each action is gated on
    // doc->pageCount() in onCurrentDocumentChanged.
    auto withCurrentDoc = [this](auto &&fn) {
        return [this, fn = std::forward<decltype(fn)>(fn)]() {
            if (auto *doc = m_documentView->currentDocument()) {
                fn(doc);
            }
        };
    };

    auto *firstPage = goMenu->addAction(tr("&First Page"));
    firstPage->setShortcut(QKeySequence(tr("Ctrl+Home")));
    connect(firstPage, &QAction::triggered, this,
            withCurrentDoc([](IDocument *doc) { doc->goToPage(0); }));

    auto *prevPage = goMenu->addAction(tr("&Previous Page"));
    prevPage->setShortcut(QKeySequence(tr("Ctrl+Left")));
    connect(prevPage, &QAction::triggered, this, withCurrentDoc([](IDocument *doc) {
                doc->goToPage(std::max(0, doc->currentPage() - 1));
            }));

    auto *nextPage = goMenu->addAction(tr("&Next Page"));
    nextPage->setShortcut(QKeySequence(tr("Ctrl+Right")));
    connect(nextPage, &QAction::triggered, this, withCurrentDoc([](IDocument *doc) {
                doc->goToPage(std::min(doc->pageCount() - 1, doc->currentPage() + 1));
            }));

    auto *lastPage = goMenu->addAction(tr("&Last Page"));
    lastPage->setShortcut(QKeySequence(tr("Ctrl+End")));
    connect(lastPage, &QAction::triggered, this,
            withCurrentDoc([](IDocument *doc) { doc->goToPage(doc->pageCount() - 1); }));

    goMenu->addSeparator();

    // ⌥⌘G — same shortcut Preview.app uses. Pops a small dialog
    // asking for the page number.
    auto *gotoPage = goMenu->addAction(tr("&Go to Page…"));
    gotoPage->setShortcut(QKeySequence(tr("Ctrl+Alt+G")));
    connect(gotoPage, &QAction::triggered, this, [this]() {
        auto *doc = m_documentView->currentDocument();
        if (!doc || doc->pageCount() <= 0)
            return;
        bool ok = false;
        const int picked =
            QInputDialog::getInt(this, tr("Go to Page"), tr("Page (1–%1):").arg(doc->pageCount()),
                                 doc->currentPage() + 1, 1, doc->pageCount(), 1, &ok);
        if (ok)
            doc->goToPage(picked - 1);
    });
}

void MainWindow::buildWindowMenu(QMenu *windowMenu) {
    // Standard macOS Window-menu items. Qt fills the default
    // shortcut on macOS (⌘M for Minimize); we set it explicitly so
    // it shows up on Linux / Windows too.
    m_windowMenu = windowMenu;

    auto *minimize = windowMenu->addAction(tr("&Minimize"));
    minimize->setShortcut(QKeySequence(tr("Ctrl+M")));
    connect(minimize, &QAction::triggered, this, &QWidget::showMinimized);

    auto *zoom = windowMenu->addAction(tr("&Zoom"));
    // "Zoom" is the native macOS Window-menu term. On other platforms it
    // collides with the app's content zoom (View → Zoom In/Out) and isn't a
    // platform convention, so relabel to the honest term for what it does
    // (maximize/restore toggle). Behavior is unchanged on all platforms.
    // On macOS the action keeps its creation text ("&Zoom"); only the
    // non-mac relabel is meaningful.
#ifndef Q_OS_MACOS
    zoom->setText(tr("&Maximize"));
#endif
    connect(zoom, &QAction::triggered, this, [this]() {
        // macOS "Zoom" toggles between user-sized and the OS's
        // ideal-for-content size. QWidget doesn't expose that
        // directly; toggling maximized is the closest portable
        // approximation.
        if (isMaximized())
            showNormal();
        else
            showMaximized();
    });

    windowMenu->addSeparator();

    auto *bringAll = windowMenu->addAction(tr("Bring All to &Front"));
    connect(bringAll, &QAction::triggered, this, [this]() {
        for (MainWindow *w : m_app->windows()) {
            if (!w)
                continue;
            w->showNormal();
            w->raise();
            w->activateWindow();
        }
    });

    m_windowMenuListSeparator = windowMenu->addSeparator();

    // Refresh the dynamic per-window list each time the menu
    // shows. Cheap (linear in window count, typically 1–3).
    connect(windowMenu, &QMenu::aboutToShow, this, &MainWindow::refreshWindowMenuList);

    refreshWindowMenuList();
}

void MainWindow::refreshWindowMenuList() {
    if (!m_windowMenu || !m_windowMenuListSeparator)
        return;
    // Drop every action after the sentinel separator and rebuild.
    const auto actions = m_windowMenu->actions();
    bool past = false;
    for (QAction *a : actions) {
        if (past) {
            m_windowMenu->removeAction(a);
            a->deleteLater();
        }
        if (a == m_windowMenuListSeparator)
            past = true;
    }
    for (MainWindow *w : m_app->windows()) {
        if (!w)
            continue;
        const QString title = w->windowTitle().isEmpty() ? tr("Untitled") : w->windowTitle();
        QAction *entry = m_windowMenu->addAction(title);
        entry->setCheckable(true);
        entry->setChecked(w == this);
        connect(entry, &QAction::triggered, w, [w]() {
            w->showNormal();
            w->raise();
            w->activateWindow();
        });
    }
}

void MainWindow::buildToolsMenu(QMenu *toolsMenu) {
    // QAction::toolTip() on a menu item is only rendered as a hover
    // tooltip when the parent QMenu opts in. ML actions use this to
    // explain a policy block ("Set to Never Download in Manage ML
    // Models…"). The opt-in is guaranteed here because Recognize Text is
    // created below via makeDisabledAction(), which calls
    // toolsMenu->setToolTipsVisible(true) at attach time — so no separate
    // explicit call is needed.

    m_rotateLeftAction = toolsMenu->addAction(
        themedActionIcon(QStringLiteral(":/icons/actions/page-rotate-left.svg"), this),
        tr("Rotate &Left"));
    m_rotateLeftAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_L));
    connect(m_rotateLeftAction, &QAction::triggered, this, &MainWindow::onRotateLeft);

    m_rotateRightAction = toolsMenu->addAction(
        themedActionIcon(QStringLiteral(":/icons/actions/page-rotate-right.svg"), this),
        tr("Rotate &Right"));
    m_rotateRightAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    connect(m_rotateRightAction, &QAction::triggered, this, &MainWindow::onRotateRight);

    m_flipHorizontalAction = toolsMenu->addAction(tr("Flip &Horizontal"));
    connect(m_flipHorizontalAction, &QAction::triggered, this, &MainWindow::onFlipHorizontal);

    m_flipVerticalAction = toolsMenu->addAction(tr("Flip &Vertical"));
    connect(m_flipVerticalAction, &QAction::triggered, this, &MainWindow::onFlipVertical);

    toolsMenu->addSeparator();

    m_adjustSizeAction = toolsMenu->addAction(tr("Adjust Si&ze…"));
    m_adjustSizeAction->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_I));
    connect(m_adjustSizeAction, &QAction::triggered, this, &MainWindow::onAdjustSize);

    m_adjustColourAction = toolsMenu->addAction(tr("Adjust Co&lour…"));
    m_adjustColourAction->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_C));
    connect(m_adjustColourAction, &QAction::triggered, this, &MainWindow::onAdjustColour);

    m_removeBackgroundAction = toolsMenu->addAction(tr("Remove &Background"));
    m_removeBackgroundAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_K));
    // The icon slot stays empty by default — only the
    // BackgroundCandidateScorer's "this looks promising" hint flips it
    // on (see updateRemoveBackgroundBadge / scheduleBackgroundCandidateScore).
    // Leaving the QAction iconless means the menu entry stays visually
    // quiet for documents where the heuristic decides there's nothing
    // to surface.
    connect(m_removeBackgroundAction, &QAction::triggered, this, &MainWindow::onRemoveBackground);

    m_instantAlphaAction = toolsMenu->addAction(tr("&Instant Alpha…"));
    connect(m_instantAlphaAction, &QAction::triggered, this, &MainWindow::onInstantAlpha);

    m_smartLassoAction = toolsMenu->addAction(tr("Smart &Lasso…"));
    connect(m_smartLassoAction, &QAction::triggered, this, &MainWindow::onSmartLasso);

    // Starts disabled (no document can be OCR'd yet) with an explanatory
    // tooltip; makeDisabledAction also enables tooltips on the Tools menu so
    // the ML-policy explanations set in updateActionStates() render on hover.
    // updateActionStates() re-evaluates the enabled state and tooltip live.
    m_recognizeTextAction = makeDisabledAction(
        toolsMenu, tr("Reco&gnize Text…"),
        tr("Open a document to recognize text."));
    connect(m_recognizeTextAction, &QAction::triggered, this, &MainWindow::onRecognizeText);

    auto *modelsAction = toolsMenu->addAction(tr("Manage &ML Models…"));
    connect(modelsAction, &QAction::triggered, this, [this]() {
        showModelManagerDialog(this, m_app);
        // Policy may have flipped — re-evaluate which ML actions are
        // enabled, so a "Never download" toggle is reflected without
        // needing to switch documents.
        onCurrentDocumentChanged(m_documentView->currentDocument());
    });

    toolsMenu->addSeparator();

    m_exportAsAction = toolsMenu->addAction(tr("&Export As…"));
    m_exportAsAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E));
    connect(m_exportAsAction, &QAction::triggered, this, &MainWindow::onExportAs);

    m_cropImageAction = toolsMenu->addAction(tr("Crop &Image…"));
    connect(m_cropImageAction, &QAction::triggered, this, &MainWindow::onCropImage);

    m_insertPagesAction = toolsMenu->addAction(tr("&Insert Pages from File…"));
    connect(m_insertPagesAction, &QAction::triggered, this, &MainWindow::onInsertPages);

    m_cropPagesAction = toolsMenu->addAction(tr("&Crop Pages…"));
    connect(m_cropPagesAction, &QAction::triggered, this, &MainWindow::onCropPages);

    // Fill Forms stays at the top level — it's the primary affordance
    // for working with a fillable PDF and the action that auto-toggles
    // when an AcroForm is opened. AutoFill (My Card → field matcher)
    // is a side feature and lives in a Forms submenu so the Tools menu
    // doesn't crowd the form-filling area with two peers of unequal
    // importance. My Card management lives next to AutoFill since it's
    // only relevant if AutoFill is being used.
    m_fillFormsAction = toolsMenu->addAction(tr("&Fill Forms"));
    m_fillFormsAction->setCheckable(true);
    m_fillFormsAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F));
    connect(m_fillFormsAction, &QAction::toggled, this, [this](bool on) {
        if (auto *doc = m_documentView->currentDocument()) {
            doc->setFormFillingActive(on);
        }
    });

    QMenu *formsSubmenu = toolsMenu->addMenu(tr("&Forms"));
    m_autoFillFormAction = formsSubmenu->addAction(tr("&AutoFill from My Card"));
    connect(m_autoFillFormAction, &QAction::triggered, this, &MainWindow::onAutoFillCurrentForm);

    m_myCardAction = formsSubmenu->addAction(tr("My &Card…"));
    connect(m_myCardAction, &QAction::triggered, this, &MainWindow::onManageMyCard);

    m_manageSignaturesAction = toolsMenu->addAction(tr("Manage &Signatures…"));
    connect(m_manageSignaturesAction, &QAction::triggered, this, &MainWindow::onManageSignatures);

    toolsMenu->addSeparator();

    m_screenshotAction = toolsMenu->addAction(tr("&Take Screenshot"));
    m_screenshotAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_3));
    connect(m_screenshotAction, &QAction::triggered, this, &MainWindow::onTakeScreenshot);

    toolsMenu->addSeparator();

    // Diagnostic / "is this stale state" action. Wipes everything
    // Trailer keeps on disk under AppPaths::*: settings.toml,
    // recent.json, cards.toml, signatures dir, model cache. The
    // user is asked to confirm with a destructive button label
    // because this is genuinely destructive.
    auto *resetAction = toolsMenu->addAction(tr("&Reset Trailer Settings…"));
    connect(resetAction, &QAction::triggered, this, &MainWindow::onResetTrailerSettings);
}

void MainWindow::onCropPages() {
    auto *doc = m_documentView->currentDocument();
    if (!doc || !doc->supportsEditing())
        return;

    CropPagesDialog dialog(doc->pageCount(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    constexpr double kMmToPt = 72.0 / 25.4;
    const double l = dialog.leftMm() * kMmToPt;
    const double t = dialog.topMm() * kMmToPt;
    const double r = dialog.rightMm() * kMmToPt;
    const double b = dialog.bottomMm() * kMmToPt;
    if (l == 0.0 && t == 0.0 && r == 0.0 && b == 0.0)
        return;

    bool anyApplied = false;
    if (dialog.applyToAllPages()) {
        const int pages = doc->pageCount();
        std::vector<int> all;
        all.reserve(static_cast<size_t>(pages));
        for (int i = 0; i < pages; ++i)
            all.push_back(i);
        anyApplied = doc->cropPages(all, l, t, r, b);
    } else {
        anyApplied = doc->cropPage(doc->currentPage(), l, t, r, b);
    }

    if (!anyApplied) {
        flashError(tr("Crop failed — margins may be too large."));
        return;
    }
    m_sidebar->refreshThumbnails();
    onCurrentDocumentChanged(doc);
}

void MainWindow::onInsertPages() {
    auto *doc = m_documentView->currentDocument();
    if (!doc || !doc->supportsEditing())
        return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Insert Pages from File"), QString(),
                                                      tr("PDF documents (*.pdf)"));
    if (path.isEmpty())
        return;
    const int insertAt = doc->currentPage() + 1;
    if (!doc->insertPagesFrom(path, insertAt)) {
        flashError(tr("Insert failed — could not insert pages from %1").arg(path));
        return;
    }
    m_sidebar->refreshThumbnails();
    onCurrentDocumentChanged(doc);
}

int MainWindow::selectedPageForEdit(IDocument *doc) const {
    if (!doc)
        return -1;
    const int page = doc->currentPage();
    if (page >= 0 && page < doc->pageCount())
        return page;
    return -1;
}

void MainWindow::onRotateLeft() {
    auto *doc = m_documentView->currentDocument();
    if (!doc || !doc->supportsEditing())
        return;
    const int page = selectedPageForEdit(doc);
    if (page < 0)
        return;
    doc->rotatePage(page, -90);
    m_sidebar->refreshThumbnails();
    updateTitleForDocument(doc);
}

void MainWindow::onRotateRight() {
    auto *doc = m_documentView->currentDocument();
    if (!doc || !doc->supportsEditing())
        return;
    const int page = selectedPageForEdit(doc);
    if (page < 0)
        return;
    doc->rotatePage(page, 90);
    m_sidebar->refreshThumbnails();
    updateTitleForDocument(doc);
}

void MainWindow::onFlipHorizontal() {
    auto *doc = m_documentView->currentDocument();
    if (!doc || !doc->supportsEditing())
        return;
    doc->flipHorizontal();
    m_sidebar->refreshThumbnails();
    updateTitleForDocument(doc);
}

void MainWindow::onFlipVertical() {
    auto *doc = m_documentView->currentDocument();
    if (!doc || !doc->supportsEditing())
        return;
    doc->flipVertical();
    m_sidebar->refreshThumbnails();
    updateTitleForDocument(doc);
}

void MainWindow::onAdjustSize() {
    auto *doc = m_documentView->currentDocument();
    if (!doc || !doc->supportsEditing())
        return;
    const QSize current = doc->imagePixelSize();
    if (current.isEmpty())
        return;

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Adjust Size"));
    auto *form = new QFormLayout(&dialog);

    auto *widthSpin = new QSpinBox(&dialog);
    widthSpin->setRange(1, 32768);
    widthSpin->setValue(current.width());
    widthSpin->setSuffix(tr(" px"));

    auto *heightSpin = new QSpinBox(&dialog);
    heightSpin->setRange(1, 32768);
    heightSpin->setValue(current.height());
    heightSpin->setSuffix(tr(" px"));

    auto *aspectCheck = new QCheckBox(tr("Keep aspect ratio"), &dialog);
    aspectCheck->setChecked(true);
    auto *smoothCheck = new QCheckBox(tr("Smooth scaling"), &dialog);
    smoothCheck->setChecked(true);

    const double aspect =
        static_cast<double>(current.width()) / static_cast<double>(current.height());
    connect(widthSpin, QOverload<int>::of(&QSpinBox::valueChanged), &dialog,
            [aspectCheck, heightSpin, aspect](int w) {
                if (aspectCheck->isChecked() && aspect > 0.0) {
                    QSignalBlocker b(heightSpin);
                    heightSpin->setValue(qMax(1, qRound(w / aspect)));
                }
            });
    connect(heightSpin, QOverload<int>::of(&QSpinBox::valueChanged), &dialog,
            [aspectCheck, widthSpin, aspect](int h) {
                if (aspectCheck->isChecked() && aspect > 0.0) {
                    QSignalBlocker b(widthSpin);
                    widthSpin->setValue(qMax(1, qRound(h * aspect)));
                }
            });

    form->addRow(tr("Width"), widthSpin);
    form->addRow(tr("Height"), heightSpin);
    form->addRow(aspectCheck);
    form->addRow(smoothCheck);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return;
    if (!doc->resizeImage(widthSpin->value(), heightSpin->value(), smoothCheck->isChecked())) {
        flashError(tr("Resize failed — could not resize this document."));
        return;
    }
    m_sidebar->refreshThumbnails();
    updateTitleForDocument(doc);
}

void MainWindow::onAdjustColour() {
    auto *doc = m_documentView->currentDocument();
    if (!doc || !doc->supportsEditing())
        return;
    auto *imgDoc = dynamic_cast<ImageDocument *>(doc);

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Adjust Colour"));
    auto *form = new QFormLayout(&dialog);

    auto makeSlider = [&]() {
        auto *s = new QSlider(Qt::Horizontal, &dialog);
        s->setRange(-100, 100);
        s->setValue(0);
        s->setTickPosition(QSlider::TicksBelow);
        s->setTickInterval(50);
        s->setMinimumWidth(240);
        return s;
    };
    auto *brightness = makeSlider();
    auto *contrast = makeSlider();
    auto *saturation = makeSlider();

    form->addRow(tr("Brightness"), brightness);
    form->addRow(tr("Contrast"), contrast);
    form->addRow(tr("Saturation"), saturation);

    auto updatePreview = [imgDoc, brightness, contrast, saturation]() {
        if (!imgDoc)
            return;
        imgDoc->previewColour(brightness->value() / 100.0, contrast->value() / 100.0,
                              saturation->value() / 100.0);
    };
    connect(brightness, &QSlider::valueChanged, &dialog, updatePreview);
    connect(contrast, &QSlider::valueChanged, &dialog, updatePreview);
    connect(saturation, &QSlider::valueChanged, &dialog, updatePreview);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);

    const int result = dialog.exec();
    if (imgDoc)
        imgDoc->clearColourPreview();
    if (result != QDialog::Accepted)
        return;
    const double b = brightness->value() / 100.0;
    const double c = contrast->value() / 100.0;
    const double s = saturation->value() / 100.0;
    if (b == 0.0 && c == 0.0 && s == 0.0)
        return;
    if (!doc->adjustColour(b, c, s)) {
        flashError(tr("Adjust Colour failed — could not adjust this document."));
        return;
    }
    m_sidebar->refreshThumbnails();
    updateTitleForDocument(doc);
}

void MainWindow::onRemoveBackground() {
    auto *doc = m_documentView->currentDocument();
    if (!doc || !doc->supportsEditing())
        return;
    auto *imgDoc = dynamic_cast<ImageDocument *>(doc);
    if (!imgDoc)
        return;

    // Heap-allocate the remover so its lifetime spans the worker
    // thread that captures this shared_ptr in the inference lambda
    // below.
    auto remover = std::make_shared<BackgroundRemover>(&m_app->modelRegistry());

    // Pre-flight: confirm download + policy via the shared helper. If
    // anything goes wrong (user cancel, policy block, failed download)
    // bail without falling through to the inference step.
    ModelDownloadRequest req;
    req.app = m_app;
    req.parent = this;
    req.required = {ModelId::U2NetP};
    req.featureName = tr("Background Removal");
    req.modelLabel = tr("U²-Net Portable");
    req.licenseLabel = tr("Apache 2.0");
    req.progressMessage = tr("Downloading U\u00b2-Net Portable…");
    req.failureSubject = tr("the background-removal model");
    req.isReady = [remover]() { return remover->isModelReady(); };
    req.kickoff = [remover]() { remover->ensureModelAvailable(); };
    req.wireSignals = [remover](QProgressDialog *progress, bool *ready, bool *failed,
                                QString *failureMessage) {
        connect(remover.get(), &BackgroundRemover::downloadProgress, progress,
                [progress](qint64 received, qint64 total) {
                    if (total <= 0) {
                        progress->setRange(0, 0);
                        return;
                    }
                    progress->setRange(0, 100);
                    progress->setValue(static_cast<int>(received * 100 / total));
                });
        connect(remover.get(), &BackgroundRemover::modelReady, progress, [progress, ready]() {
            *ready = true;
            progress->setValue(progress->maximum());
            progress->close();
        });
        connect(remover.get(), &BackgroundRemover::modelUnavailable, progress,
                [progress, failed, failureMessage](const QString &msg) {
                    *failed = true;
                    *failureMessage = msg;
                    progress->close();
                });
    };
    if (!requestModelDownload(req))
        return;

    // Inference runs through MlScheduler at UserAction priority so:
    //
    //   - cancellation propagates when the document closes or the
    //     window is destroyed (Application owns the scheduler — on
    //     shutdown its destructor blocks until the worker drains, and
    //     in-flight tokens flip via cancelAll() if a future caller
    //     wires that into the close path);
    //   - the wave-2 status-bar indicator shows "Removing background…"
    //     while the work is in flight, replacing the modal
    //     QProgressDialog the previous implementation used (the user
    //     gets non-blocking feedback at the bottom-right of the window
    //     instead of a centred modal that steals focus);
    //   - the battery-policy reactor sees this is UserAction and lets
    //     it run regardless of mlRunOnBattery — a no-op behaviourally
    //     for this priority, but the right plumbing for consistency
    //     with other features in this wave.
    //
    // The result-application step lands back on the GUI thread via
    // QMetaObject::invokeMethod so we can safely touch ImageDocument
    // and the sidebar. PHILOSOPHY: on null/failure we simply do not
    // apply — no popup, no flashError. Transient inference failure is
    // rare; the user can re-run from the menu, or open
    // Tools → Manage ML Models…  to verify the cache (which is also
    // what the disabled-entry tooltip points at when the
    // "Never download" policy is the cause).
    const QImage source = imgDoc->image();
    auto *self = this;
    auto handle =
        m_app->mlScheduler().submit(MlPriority::UserAction, tr("Removing background…"),
                                    [self, source, doc, imgDoc, remover](CancellationToken &token) {
                                        const QImage result = remover->remove(source, &token);
                                        if (token.isCancelled() || result.isNull()) {
                                            // Cancellation or transient failure: bail without
                                            // touching the document. The status-bar indicator
                                            // clears automatically when the scheduler reports
                                            // idle.
                                            return;
                                        }
                                        // Apply on the GUI thread. We snapshot the doc pointer
                                        // and re-check it against the active document — if the
                                        // user closed or switched away between submission and
                                        // completion, drop the result so we don't mutate a
                                        // different document underfoot.
                                        QMetaObject::invokeMethod(
                                            self,
                                            [self, doc, imgDoc, result]() {
                                                auto *current =
                                                    self->m_documentView->currentDocument();
                                                if (current != doc)
                                                    return;
                                                if (!imgDoc->replaceImage(result))
                                                    return;
                                                self->m_sidebar->refreshThumbnails();
                                                self->updateTitleForDocument(doc);
                                            },
                                            Qt::QueuedConnection);
                                    });
    Q_UNUSED(handle);
}

// Pre-flights for MobileSAM (Instant Alpha / Smart Lasso) and PP-OCR
// (Recognize Text). Both reduce to the shared requestModelDownload
// helper — these wrappers just glue the feature-class signals onto it.
namespace {

bool ensureSamModelsReady(MainWindow *parent, SamSession &session) {
    ModelDownloadRequest req;
    req.app = parent->app();
    req.parent = parent;
    req.required = {ModelId::MobileSamEncoder, ModelId::MobileSamDecoder};
    req.featureName = QObject::tr("Instant Alpha / Smart Lasso");
    req.modelLabel = QObject::tr("MobileSAM");
    req.licenseLabel = QObject::tr("Apache 2.0 / MIT");
    req.progressMessage = QObject::tr("Downloading MobileSAM models…");
    req.failureSubject = QObject::tr("MobileSAM");
    req.isReady = [&session]() { return session.isModelReady(); };
    req.kickoff = [&session]() { session.ensureModelsAvailable(); };
    req.wireSignals = [&session](QProgressDialog *progress, bool *ready, bool *failed,
                                 QString *failureMessage) {
        QObject::connect(&session, &SamSession::downloadProgress, progress,
                         [progress](qint64 received, qint64 total) {
                             if (total <= 0) {
                                 progress->setRange(0, 0);
                                 return;
                             }
                             progress->setRange(0, 100);
                             progress->setValue(static_cast<int>(received * 100 / total));
                         });
        QObject::connect(&session, &SamSession::modelsReady, progress, [progress, ready]() {
            *ready = true;
            progress->setValue(progress->maximum());
            progress->close();
        });
        QObject::connect(&session, &SamSession::modelsUnavailable, progress,
                         [progress, failed, failureMessage](const QString &msg) {
                             *failed = true;
                             *failureMessage = msg;
                             progress->close();
                         });
    };
    return requestModelDownload(req);
}

// Tools-menu shortcut for Instant Alpha / Smart Lasso. The Workstream G
// rework moved the segmentation interaction out of a modal dialog and
// onto the document overlay. The menu entries stay so existing
// keyboard shortcuts and the Tools menu surface still trigger the
// feature; both reduce to "show the markup toolbar, activate the
// right tool, run the encoder download dialog once if needed".
void activateSamTool(MainWindow *parent, MarkupToolbar *toolbar, AnnotationTool tool) {
    if (!parent || !toolbar)
        return;
    // Ensure the models are on disk before flipping the tool — the
    // download progress dialog is reachable through the existing
    // ModelDownloadRequest plumbing. PHILOSOPHY: this is the only
    // popup in the SAM flow (one-time consent for a download); the
    // rest of the interaction is inline.
    SamSession session(&parent->app()->modelRegistry());
    if (!ensureSamModelsReady(parent, session))
        return;
    // Show the markup toolbar so the user can see which tool is now
    // active, then flip the radio.
    toolbar->show();
    toolbar->setActiveTool(tool);
}

bool ensureOcrModelsReady(MainWindow *parent, OcrEngine &engine) {
    ModelDownloadRequest req;
    req.app = parent->app();
    req.parent = parent;
    req.required = {ModelId::PpOcrDetector, ModelId::PpOcrRecognizerLatin};
    req.featureName = QObject::tr("Recognize Text");
    req.modelLabel = QObject::tr("PP-OCRv3 detector + recognizer");
    req.licenseLabel = QObject::tr("Apache 2.0");
    req.progressMessage = QObject::tr("Downloading text-recognition models…");
    req.failureSubject = QObject::tr("text-recognition models");
    req.isReady = [&engine]() { return engine.isModelReady(); };
    req.kickoff = [&engine]() { engine.ensureModelsAvailable(); };
    req.wireSignals = [&engine](QProgressDialog *progress, bool *ready, bool *failed,
                                QString *failureMessage) {
        QObject::connect(&engine, &OcrEngine::downloadProgress, progress,
                         [progress](qint64 received, qint64 total) {
                             if (total <= 0) {
                                 progress->setRange(0, 0);
                                 return;
                             }
                             progress->setRange(0, 100);
                             progress->setValue(static_cast<int>(received * 100 / total));
                         });
        QObject::connect(&engine, &OcrEngine::modelsReady, progress, [progress, ready]() {
            *ready = true;
            progress->setValue(progress->maximum());
            progress->close();
        });
        QObject::connect(&engine, &OcrEngine::modelsUnavailable, progress,
                         [progress, failed, failureMessage](const QString &msg) {
                             *failed = true;
                             *failureMessage = msg;
                             progress->close();
                         });
    };
    return requestModelDownload(req);
}

} // namespace

void MainWindow::onInstantAlpha() {
    auto *doc = m_documentView->currentDocument();
    if (!doc || !doc->supportsEditing())
        return;
    if (!dynamic_cast<ImageDocument *>(doc))
        return;
    // Workstream G: the modal SamSegmentDialog is gone. The menu entry
    // now just activates the InstantAlpha tool mode on the markup
    // toolbar (showing the toolbar first if it was hidden), and the
    // user paints on the document directly. The toolbar's tool radio
    // is the source of truth.
    activateSamTool(this, m_markupToolbar, AnnotationTool::InstantAlpha);
}

void MainWindow::onSmartLasso() {
    auto *doc = m_documentView->currentDocument();
    if (!doc || !doc->supportsEditing())
        return;
    if (!dynamic_cast<ImageDocument *>(doc))
        return;
    activateSamTool(this, m_markupToolbar, AnnotationTool::SmartLasso);
}

QString MainWindow::recognizeCompletionMessage(bool cancelled, int blockCount) {
    if (cancelled)
        return tr("Text recognition cancelled — no changes saved");
    return blockCount > 0 ? tr("Text recognition complete") : tr("No text found");
}

std::optional<std::vector<int>> resolveRecognizePages(const IDocument &doc) {
    // A one-page document offers no page-range choice, so the dialog would
    // only add a click. Resolve straight to the current (only) page —
    // behaviour-equivalent to RecognizeTextDialog::resolvedPages() for a
    // single-page doc. Multi-page docs defer to the dialog (nullopt).
    if (doc.pageCount() == 1)
        return std::vector<int>{doc.currentPage()};
    return std::nullopt;
}

void MainWindow::maybeKickSearchOcr(IDocument *doc, const QString &query) {
    if (!doc || query.isEmpty() || !m_ocrController)
        return;
    // Only images need this: PDFs search their native text layer, and the
    // menu-driven Recognize flow covers explicit multi-page runs.
    if (!dynamic_cast<ImageDocument *>(doc))
        return;
    if (!doc->supportsSelectableText())
        return;
    auto *store = doc->selectableText();
    if (!store || store->hasResults(0))
        return; // already OCR'd — setSearchQuery already searched it
    if (m_searchOcrKicked.contains(doc))
        return; // already kicked for this doc; don't churn on each keystroke
    // Only claim the once-per-doc guard when the model is actually ready
    // to run. If the first search races the model download, submitting now
    // would silently no-op (resolve before the reveal delay) yet still burn
    // the guard — and the guard is never reset until the doc closes, so
    // search-driven OCR would never retry even after the model lands.
    // Bailing without inserting lets the next keystroke re-try once the
    // model is present. The menu path remains the place that offers the
    // model download; this path never prompts, so there is nothing to do
    // here while the model is absent.
    if (!m_ocrController->modelReady())
        return;
    m_searchOcrKicked.insert(doc);
    // Same mechanism the Recognize Text menu uses.
    m_ocrController->submitUserPages(doc, {0}, /*forceRerun=*/false);
}

void MainWindow::onRecognizeText() {
    auto *doc = m_documentView->currentDocument();
    if (!doc || !doc->supportsSelectableText())
        return;

    // Single-page documents (every image, single-page PDFs) offer no
    // page-range choice, so skip the dialog entirely and resolve to the
    // current page. Multi-page docs still get the picker. forceRerun is
    // false on the skipped path — there is no dialog control to set it.
    std::vector<int> pages;
    bool forceRerun = false;
    if (const auto resolved = resolveRecognizePages(*doc)) {
        pages = *resolved;
    } else {
        // Show the parameter-supply dialog. The dialog itself does not
        // run OCR — results stream into the document's
        // SelectableTextStore via OcrController, and the user reads them
        // in-place via SelectableTextLayer.
        // Language options: shipped manifest currently has only the
        // Latin recognizer. We pass an empty list so the dialog hides
        // the row; once the CJK recognizer is on the manifest, expand
        // this list and the row appears automatically.
        QStringList languageOptions;
        RecognizeTextDialog dialog(doc->pageCount(), doc->currentPage(), doc->hasTextLayer(),
                                   languageOptions, this);
        if (dialog.exec() != QDialog::Accepted)
            return;
        pages = dialog.resolvedPages();
        forceRerun = dialog.forceRerun();
    }
    if (pages.empty())
        return;

    // Ensure models are present. ensureOcrModelsReady runs an
    // async-with-progress consent / download flow on the very first
    // call; on subsequent calls (cache hit) it returns immediately.
    // The engine instance here is short-lived — only used to gate
    // the model-download step. The OcrController owns the long-lived
    // engine used for actual inference.
    OcrEngine gateEngine(&m_app->modelRegistry());
    if (!ensureOcrModelsReady(this, gateEngine))
        return;

    const auto pageCount = pages.size();
    m_ocrController->submitUserPages(doc, std::move(pages), forceRerun);
    flashStatus(tr("Recognizing text for %1 page(s)…").arg(pageCount));
}

void MainWindow::onExportAs() {
    auto *doc = m_documentView->currentDocument();
    if (!doc)
        return;

    // Pre-dialog: let the user pick a Quartz-equivalent filter
    // (DESIGN §6.3.7). Default is "None" so the old one-step flow is
    // still a single Cancel-or-Enter away — users who don't know
    // about filters pay no extra clicks.
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Export As"));
    auto *form = new QFormLayout(&dialog);

    auto *filterCombo = new QComboBox(&dialog);
    for (ImageFilter f : allFilters()) {
        filterCombo->addItem(filterDisplayName(f), filterId(f));
    }
    filterCombo->setCurrentIndex(0); // None
    form->addRow(tr("Filter:"), filterCombo);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const QString chosenFilterId = filterCombo->currentData().toString();

    QStringList filters;
    filters << tr("PNG image (*.png)") << tr("JPEG image (*.jpg *.jpeg)")
            << tr("TIFF image (*.tif *.tiff)") << tr("BMP image (*.bmp)")
            << tr("WebP image (*.webp)")
            // Single-page PDF wrapping the (filtered, flattened)
            // image. The "I want to email this photo as a PDF"
            // workflow people expect from Preview.
            << tr("PDF document (*.pdf)");
    QString selected;
    const QString suggested = doc->filePath().isEmpty()
                                  ? doc->displayName()
                                  : QFileInfo(doc->filePath()).completeBaseName() + ".png";
    const QString path = QFileDialog::getSaveFileName(this, tr("Export As"), suggested,
                                                      filters.join(";;"), &selected);
    if (path.isEmpty())
        return;
    QString format = QFileInfo(path).suffix().toLower();
    if (format.isEmpty())
        format = "png";
    if (!doc->exportAs(path, format, -1, chosenFilterId)) {
        flashError(tr("Export failed — could not write to %1").arg(path));
    }
}

void MainWindow::onCropImage() {
    auto *doc = m_documentView->currentDocument();
    if (!doc || !doc->supportsEditing())
        return;
    const QSize size = doc->imagePixelSize();
    if (size.isEmpty())
        return;

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Crop Image"));
    auto *form = new QFormLayout(&dialog);

    auto makeSpin = [&](int maxVal) {
        auto *s = new QSpinBox(&dialog);
        s->setRange(0, maxVal);
        s->setSuffix(tr(" px"));
        return s;
    };
    auto *leftSpin = makeSpin(size.width() - 1);
    auto *topSpin = makeSpin(size.height() - 1);
    auto *rightSpin = makeSpin(size.width() - 1);
    auto *bottomSpin = makeSpin(size.height() - 1);

    form->addRow(tr("Left"), leftSpin);
    form->addRow(tr("Top"), topSpin);
    form->addRow(tr("Right"), rightSpin);
    form->addRow(tr("Bottom"), bottomSpin);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return;
    const int x = leftSpin->value();
    const int y = topSpin->value();
    const int w = size.width() - x - rightSpin->value();
    const int h = size.height() - y - bottomSpin->value();
    if (w <= 0 || h <= 0 || !doc->cropToRect(x, y, w, h)) {
        flashError(tr("Crop failed — margins are too large."));
        return;
    }
    m_sidebar->refreshThumbnails();
    updateTitleForDocument(doc);
}

namespace {

QString screenshotTargetPath() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    return QDir(dir).filePath(QStringLiteral("trailer-screenshot-%1.png").arg(stamp));
}

enum class ShotMode { Screen, Window, Region };

} // namespace

void MainWindow::onTakeScreenshot() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Take Screenshot"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *screenRadio = new QRadioButton(tr("Whole screen"), &dialog);
    auto *windowRadio = new QRadioButton(tr("Single window (click to select)"), &dialog);
    auto *regionRadio = new QRadioButton(tr("Region (drag to select)"), &dialog);
    screenRadio->setChecked(true);
    layout->addWidget(screenRadio);
    layout->addWidget(windowRadio);
    layout->addWidget(regionRadio);

#ifndef Q_OS_MACOS
    windowRadio->setEnabled(false);
    regionRadio->setEnabled(false);
    auto *note = new QLabel(tr("Only whole-screen capture is supported on this platform. "
                               "Window and region capture are tracked in TODO.md."),
                            &dialog);
    note->setWordWrap(true);
    layout->addWidget(note);
#endif

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return;

    ShotMode mode = ShotMode::Screen;
    if (windowRadio->isChecked())
        mode = ShotMode::Window;
    else if (regionRadio->isChecked())
        mode = ShotMode::Region;

    const QString path = screenshotTargetPath();

#ifdef Q_OS_MACOS
    // First use only: explain that macOS will prompt for "Screen Recording"
    // permission (its name even for a still screenshot) before we shell to
    // screencapture. Deferred to first actual use — never at launch.
    if (!maybeShowScreenCaptureExplainer(m_app->settings(), this)) {
        // User cancelled the pre-permission explainer — do not capture.
        return;
    }
    // Hide our window so it doesn't occlude the target, then use the native
    // macOS capture tool for proper DPI handling and interactive selection.
    hide();
    QStringList args;
    args << "-x"; // silent (no capture sound)
    switch (mode) {
    case ShotMode::Screen:
        break;
    case ShotMode::Window:
        args << "-iW";
        break;
    case ShotMode::Region:
        args << "-i"
             << "-s";
        break;
    }
    args << path;
    QProcess proc;
    proc.start("/usr/sbin/screencapture", args);
    proc.waitForFinished(-1);
    show();
    raise();
    activateWindow();
    if (proc.exitCode() != 0) {
        // Non-zero exit means the user cancelled (Esc) — not an error.
        flashStatus(tr("Screen capture cancelled."));
        return;
    }
    if (!QFileInfo(path).exists() || QFileInfo(path).size() == 0) {
        // Exit 0 but no output — screencapture produced nothing, which can
        // silently mean Screen Recording permission was denied. Surface a
        // graceful hint pointing at the permission setting.
        flashStatus(tr("No image was captured. If you denied Screen Recording, "
                       "grant it in System Settings ▸ Privacy & Security ▸ "
                       "Screen Recording."));
        return;
    }
#else
    if (mode != ShotMode::Screen) {
        flashStatus(tr("Window/region capture is not yet supported on this "
                       "platform."));
        return;
    }
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen)
        return;
    const QPixmap shot = screen->grabWindow(0);
    if (shot.isNull() || !shot.save(path, "PNG")) {
        flashError(tr("Screenshot failed — could not capture the screen."));
        return;
    }
#endif

    m_app->openFiles({path});
}

void MainWindow::onSave() {
    auto *doc = m_documentView->currentDocument();
    if (!doc || !doc->supportsEditing())
        return;
    if (doc->filePath().isEmpty()) {
        onSaveAs();
        return;
    }
    saveDocumentAsync(doc, doc->filePath());
}

void MainWindow::saveDocumentAsync(IDocument *doc, const QString &targetPath) {
    if (!doc)
        return;

    // Image saves are fast (QImage::save encodes a frame). Synchronous
    // is fine — wrapping it adds latency without benefit. PDF saves
    // can be 5-15 s on heavy redactions, so they go through the
    // two-phase split so the UI thread stays responsive.
    auto *pdfDoc = dynamic_cast<PdfDocument *>(doc);
    if (!pdfDoc) {
        if (!doc->save(targetPath)) {
            flashError(tr("Save failed — could not write to %1").arg(targetPath));
            return;
        }
        m_app->settings().setLastSaveDir(QFileInfo(targetPath).absolutePath());
        m_app->settings().save();
        updateTitleForDocument(doc);
        flashSuccess(tr("Saved."));
        return;
    }

    auto *progress = new QProgressDialog(tr("Saving…"), tr("Cancel"), 0, 0, this);
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->setAutoClose(false);
    progress->setAutoReset(false);

    using SaveResult = std::optional<PdfDocument::SaveContext>;
    auto *watcher = new QFutureWatcher<SaveResult>(this);
    connect(progress, &QProgressDialog::canceled, watcher, &QFutureWatcherBase::cancel);
    connect(watcher, &QFutureWatcher<SaveResult>::finished, this,
            [this, watcher, progress, pdfDoc, doc, targetPath]() {
                const bool wasCanceled = progress->wasCanceled();
                progress->close();
                progress->deleteLater();
                SaveResult result;
                if (!wasCanceled)
                    result = watcher->result();
                watcher->deleteLater();
                if (wasCanceled)
                    return;
                if (!result) {
                    flashError(tr("Save failed — could not write to %1").arg(targetPath));
                    return;
                }
                // Worker phase succeeded; commit on the UI thread.
                if (!pdfDoc->saveCommitOnUi(*result)) {
                    flashError(tr("Save failed — could not finalise %1").arg(targetPath));
                    return;
                }
                m_app->settings().setLastSaveDir(QFileInfo(targetPath).absolutePath());
                m_app->settings().save();
                updateTitleForDocument(doc);
                flashSuccess(tr("Saved."));
            });

    QFuture<SaveResult> future = QtConcurrent::run(
        [pdfDoc, targetPath]() -> SaveResult { return pdfDoc->saveBeginQpdfPhase(targetPath); });
    watcher->setFuture(future);
}

void MainWindow::onSaveAs() {
    auto *doc = m_documentView->currentDocument();
    if (!doc || !doc->supportsEditing())
        return;
    const QString path = chooseSaveAsPath(doc);
    if (path.isEmpty())
        return;
    // saveDocumentAsync handles the success path: status bar, title
    // refresh, lastSaveDir bookkeeping. PDFs go through the two-
    // phase worker; images stay synchronous (fast).
    saveDocumentAsync(doc, path);
}

QString MainWindow::chooseSaveAsPath(IDocument *doc) {
    if (!doc)
        return {};
    // Test seam: the offscreen UAT harness cannot drive the native file
    // dialog, so it pre-seeds the destination via setSaveAsPathForTesting.
    if (!m_saveAsPathForTesting.isEmpty())
        return m_saveAsPathForTesting;
    const bool isImage = dynamic_cast<ImageDocument *>(doc) != nullptr;

    // Suggest a filename that hints at what's been done to the
    // document. A signature or any other annotation present means
    // the user is producing a derivative — `_signed` or `_marked` —
    // not a wholesale rewrite of the original. Plain saves keep the
    // basename so an idempotent re-save doesn't pollute the picker
    // history.
    const QString basePath = doc->filePath().isEmpty() ? doc->displayName() : doc->filePath();
    QFileInfo bi(basePath);
    const QString stem = bi.completeBaseName().isEmpty() ? basePath : bi.completeBaseName();
    const QString ext = bi.suffix().isEmpty()
                            ? (isImage ? QStringLiteral("png") : QStringLiteral("pdf"))
                            : bi.suffix();

    QString suffix;
    if (auto *store = doc->annotations()) {
        bool hasSignature = false;
        bool hasOther = false;
        for (const auto &a : store->annotations()) {
            if (a.type == AnnotationType::Signature)
                hasSignature = true;
            else
                hasOther = true;
        }
        if (hasSignature)
            suffix = QStringLiteral("_signed");
        else if (hasOther)
            suffix = QStringLiteral("_marked");
    }

    // Compose the full default path. Anchor to the user's last-saved
    // directory so successive saves of related docs land together.
    QString suggested;
    if (!suffix.isEmpty()) {
        suggested = stem + suffix + QLatin1Char('.') + ext;
    } else {
        suggested = bi.fileName().isEmpty() ? basePath : bi.fileName();
    }
    const QString lastDir = m_app->settings().lastSaveDir();
    if (!lastDir.isEmpty() && QDir(lastDir).exists()) {
        suggested = QDir(lastDir).filePath(QFileInfo(suggested).fileName());
    } else if (!doc->filePath().isEmpty()) {
        // First save with no recorded dir: anchor next to the
        // original file rather than wherever Qt thinks the cwd is.
        suggested =
            QFileInfo(doc->filePath()).absoluteDir().filePath(QFileInfo(suggested).fileName());
    }

    const QString filter = isImage ? tr("Images (*.png *.jpg *.jpeg *.bmp *.tiff *.tif *.webp)")
                                   : tr("PDF documents (*.pdf)");
    return QFileDialog::getSaveFileName(this, tr("Save As"), suggested, filter);
}

void MainWindow::onExportPasswordProtected() {
    auto *doc = m_documentView->currentDocument();
    if (!doc || !doc->supportsPasswordExport())
        return;

    // --- Step 1: pick a destination path ---
    const QString suggested =
        doc->filePath().isEmpty()
            ? doc->displayName()
            : QFileInfo(doc->filePath()).completeBaseName() + QStringLiteral("_protected.pdf");
    const QString destPath = QFileDialog::getSaveFileName(
        this, tr("Export as Password-Protected PDF"), suggested, tr("PDF documents (*.pdf)"));
    if (destPath.isEmpty())
        return;

    // --- Step 2: pick the password (two matching fields) ---
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Set PDF Password"));
    auto *form = new QFormLayout(&dialog);

    auto *pwEdit = new QLineEdit(&dialog);
    pwEdit->setEchoMode(QLineEdit::Password);
    pwEdit->setPlaceholderText(tr("Enter password"));

    auto *confirmEdit = new QLineEdit(&dialog);
    confirmEdit->setEchoMode(QLineEdit::Password);
    confirmEdit->setPlaceholderText(tr("Confirm password"));

    form->addRow(tr("Password:"), pwEdit);
    form->addRow(tr("Confirm:"), confirmEdit);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString password = pwEdit->text();
    if (password != confirmEdit->text()) {
        QMessageBox::warning(this, tr("Password mismatch"),
                             tr("The passwords do not match. Please try again."));
        return;
    }
    if (password.isEmpty()) {
        QMessageBox::warning(this, tr("Empty password"),
                             tr("A password is required to protect the PDF."));
        return;
    }

    // --- Step 3: write the encrypted PDF ---
    if (!doc->exportWithPassword(destPath, password)) {
        flashError(tr("Export failed — could not write password-protected "
                      "PDF to %1")
                       .arg(destPath));
    }
}

void MainWindow::onReduceFileSize() {
    auto *doc = m_documentView->currentDocument();
    if (!doc || !doc->supportsFileSizeReduction())
        return;

    const QString suggested =
        doc->filePath().isEmpty()
            ? doc->displayName()
            : QFileInfo(doc->filePath()).completeBaseName() + QStringLiteral("_reduced.pdf");
    const QString destPath = QFileDialog::getSaveFileName(this, tr("Reduce File Size"), suggested,
                                                          tr("PDF documents (*.pdf)"));
    if (destPath.isEmpty())
        return;

    const qint64 originalSize = doc->filePath().isEmpty() ? 0 : QFileInfo(doc->filePath()).size();

    if (!doc->reduceFileSize(destPath)) {
        flashError(tr("Reduce File Size failed — could not write to %1").arg(destPath));
        return;
    }

    const qint64 newSize = QFileInfo(destPath).size();
    if (originalSize > 0 && newSize > 0) {
        const double pctDelta = 100.0 * (static_cast<double>(originalSize - newSize) /
                                         static_cast<double>(originalSize));
        const QString message = pctDelta > 0.5
                                    ? tr("Reduced from %1 to %2 (%3% smaller).")
                                          .arg(QLocale().formattedDataSize(originalSize),
                                               QLocale().formattedDataSize(newSize))
                                          .arg(pctDelta, 0, 'f', 1)
                                    : tr("Output is %1. The source was already well-compressed "
                                         "— further reduction isn't possible without dropping "
                                         "content or down-sampling images.")
                                          .arg(QLocale().formattedDataSize(newSize));
        flashSuccess(message);
    }
}

void MainWindow::updateUndoRedoActions(IDocument *doc) {
    m_undoAction->setEnabled(doc && doc->canUndo());
    m_redoAction->setEnabled(doc && doc->canRedo());
}

void MainWindow::onCopyPageAsImage() {
    auto *doc = m_documentView->currentDocument();
    if (!doc || !doc->supportsThumbnails())
        return;
    // Render the current page / image at a generous size — enough to
    // paste a crisp copy into a chat app. renderThumbnail scales to fit
    // (aspect-preserved) and composites PDF pages over opaque white.
    const QImage image = doc->renderThumbnail(doc->currentPage(), QSize(2200, 2200));
    if (image.isNull()) {
        flashError(tr("Couldn't render this page to copy."));
        return;
    }
    QApplication::clipboard()->setImage(image);
    flashSuccess(tr("Page copied to the clipboard as an image."));
}

void MainWindow::updateTitleForDocument(IDocument *doc) {
    updateUndoRedoActions(doc);
    if (!doc) {
        setWindowTitle(tr("Trailer"));
        setWindowFilePath(QString());
        return;
    }
    const QString name = doc->displayName();
    const QString marker = doc->isDirty() ? QStringLiteral("• ") : QString();
    setWindowTitle(tr("%1%2 — Trailer").arg(marker, name));
    // setWindowFilePath bridges to NSWindow::representedFilename on
    // macOS — the title bar gets a clickable folder icon for "Show
    // in Finder", drag-out, tags, and locked-state toggles. On
    // Linux / Windows it's a no-op outside Qt's internal bookkeeping.
    setWindowFilePath(doc->filePath());

    const int idx = m_documentView->currentIndex();
    if (idx >= 0) {
        m_documentView->setTabText(idx, marker + name);
    }
}

void MainWindow::applyInitialWindowSize(IDocument *doc) {
    if (m_initialSizingApplied)
        return;
    if (!doc)
        return;
    const QSize content = doc->contentSizeHint();
    if (content.isEmpty())
        return;
    m_initialSizingApplied = true;

    QScreen *scr = screen();
    if (!scr) {
        scr = QGuiApplication::primaryScreen();
    }
    if (!scr)
        return;

    const QRect avail = scr->availableGeometry();
    if (avail.isEmpty())
        return;

    // 90%-of-screen ceiling so the window doesn't fill the display
    // edge-to-edge. The user always sees some background to drag
    // the window or click another app.
    const QSize maxSize(static_cast<int>(avail.width() * 0.9),
                        static_cast<int>(avail.height() * 0.9));
    constexpr int kMinW = 1100;
    constexpr int kMinH = 750;
    const QSize minSize(kMinW, kMinH);

    // Estimate chrome (everything around the document viewport):
    // status bar, menu bar on non-mac, main / markup toolbars, plus
    // window decorations. Use frameGeometry() vs the central widget
    // size if the central widget already laid out; otherwise fall
    // back to a fixed margin.
    QSize chrome;
    if (m_documentView && m_documentView->size().isValid() && m_documentView->width() > 0 &&
        m_documentView->height() > 0) {
        chrome = size() - m_documentView->size();
    } else {
        chrome = QSize(32, 120);
    }
    if (chrome.width() < 0)
        chrome.setWidth(0);
    if (chrome.height() < 0)
        chrome.setHeight(0);

    // Aim for the viewport to host the content at 100% — fit-to-
    // content = actual size, which is the most readable default.
    QSize wantViewport = content;
    // Available viewport space within the screen ceiling.
    const QSize maxViewport(std::max(1, maxSize.width() - chrome.width()),
                            std::max(1, maxSize.height() - chrome.height()));

    // If the doc doesn't fit at 100%, scale the viewport target down,
    // but not below 75% of content. A scale lower than 0.75 means
    // the doc is bigger than the screen can comfortably hold — use
    // the screen ceiling and let fit-to-content pick whatever zoom
    // it gives.
    if (wantViewport.width() > maxViewport.width() ||
        wantViewport.height() > maxViewport.height()) {
        const double scaleW =
            static_cast<double>(maxViewport.width()) / static_cast<double>(content.width());
        const double scaleH =
            static_cast<double>(maxViewport.height()) / static_cast<double>(content.height());
        const double rawScale = std::min(scaleW, scaleH);
        const double scale = std::max(0.75, std::min(1.0, rawScale));
        wantViewport = QSize(static_cast<int>(content.width() * scale),
                             static_cast<int>(content.height() * scale));
    }

    QSize windowTarget(wantViewport.width() + chrome.width(),
                       wantViewport.height() + chrome.height());
    windowTarget = windowTarget.expandedTo(minSize).boundedTo(maxSize);
    resize(windowTarget);
}

void MainWindow::scheduleBackgroundCandidateScore(IDocument *doc) {
    // Only meaningful for image documents — PDFs can't have background
    // removed, so we never even consider badging the menu entry for
    // them. We also bail early if there's no live menu entry yet (the
    // function can be reached on synthetic doc-changes during startup).
    auto *imgDoc = dynamic_cast<ImageDocument *>(doc);
    if (!imgDoc || !m_removeBackgroundAction)
        return;
    // Idempotent: if we've already scored this doc pointer, skip. The
    // verdict is keyed by raw pointer so closing and reopening the same
    // file rescores — adapter pointers don't survive close().
    if (m_backgroundCandidateDocs.contains(doc))
        return;
    // If a job is already in flight for this doc, don't queue a second
    // one. The pending-jobs map is the source of truth.
    if (m_pendingCandidateJobs.contains(doc))
        return;

    // Snapshot a thumbnail off the GUI thread before submitting. The
    // scorer must NOT touch IDocument from the worker — adapters may
    // mutate state in response to GUI events, and renderThumbnail
    // creates Qt widgets along its hot path on some adapters. We do
    // the render here on the GUI thread (cheap — image scaled down to
    // 128 px on the short side) and hand a fully-detached QImage to
    // the worker.
    constexpr int kScoringThumb = 128;
    const QImage thumb = imgDoc->renderThumbnail(0, QSize(kScoringThumb, kScoringThumb));
    if (thumb.isNull()) {
        // No thumbnail available — treat as "no badge" and remember it
        // so we don't poll repeatedly on tab switches.
        m_backgroundCandidateDocs.remove(doc);
        return;
    }

    auto *self = this;
    auto handle = m_app->mlScheduler().submit(
        MlPriority::Prefetch, tr("Scoring image…"), [self, doc, thumb](CancellationToken &token) {
            const bool recommend = BackgroundCandidateScorer::isRecommended(thumb, &token);
            if (token.isCancelled())
                return;
            // Apply the verdict back on the GUI thread. The doc
            // pointer is a raw observer that may dangle if the user
            // closed the document while scoring; the lambda checks
            // identity against the current document before touching
            // any member state. Closing tabs cancels the pending
            // jobs map up-front so this lambda body should not fire
            // for a stale doc.
            QMetaObject::invokeMethod(
                self,
                [self, doc, recommend]() {
                    // Drop the in-flight entry first — even if the
                    // doc was swapped out, the slot is no longer
                    // pending.
                    self->m_pendingCandidateJobs.remove(doc);
                    if (recommend) {
                        self->m_backgroundCandidateDocs.insert(doc);
                    } else {
                        self->m_backgroundCandidateDocs.remove(doc);
                    }
                    // Refresh the badge only when the verdict applies
                    // to the document the user is currently looking at.
                    if (self->m_documentView->currentDocument() == doc) {
                        self->updateRemoveBackgroundBadge(doc);
                    }
                },
                Qt::QueuedConnection);
        });
    m_pendingCandidateJobs.insert(doc, handle.id);
}

void MainWindow::updateRemoveBackgroundBadge(IDocument *doc) {
    if (!m_removeBackgroundAction)
        return;
    const bool recommended = doc && m_backgroundCandidateDocs.contains(doc);
    // QAction::toolTip() returns text() when no explicit tooltip has
    // been set, so an "is it empty?" check would always be false.
    // Compare against the policy-blocked tooltip string instead:
    // applyMlPolicy() either sets that exact string (when the policy
    // is blocking) or clears it with QString() (the default).
    const QString policyTip = tr("This model is set to Never Download. "
                                 "Open Tools → Manage ML Models… to allow it.");
    const QString badgeTip = tr("Background removal works well for this kind of image.");
    const bool policyBlocked = m_removeBackgroundAction->toolTip() == policyTip;
    if (recommended) {
        // Sparkle glyph signals "this image looks like a good
        // candidate." We use the same themedActionIcon helper the rest
        // of the menu uses so the badge follows the active palette
        // (light vs dark theme).
        m_removeBackgroundAction->setIcon(
            themedActionIcon(QStringLiteral(":/icons/actions/badge-sparkle.svg"), this));
        // The policy tooltip always wins — if the user has the model
        // marked Never Download, "this image looks like a good
        // candidate" is misleading next to a disabled menu entry.
        if (!policyBlocked) {
            m_removeBackgroundAction->setToolTip(badgeTip);
        }
    } else {
        m_removeBackgroundAction->setIcon(QIcon());
        // Only clear the tooltip if we'd previously set the positive
        // hint. Don't stomp on a policy-driven tooltip.
        if (m_removeBackgroundAction->toolTip() == badgeTip) {
            m_removeBackgroundAction->setToolTip(QString());
        }
    }
}

void MainWindow::onCurrentDocumentChanged(IDocument *doc) {
    // One-shot fit-to-content window resize. Drives the first frame
    // when the user opens a single file from cold start; later doc
    // changes (tab switches, opening into the same window) leave
    // the size alone so the user's adjustments stick.
    applyInitialWindowSize(doc);

    // Update the auto-OCR controller. It cancels in-flight
    // submissions for the previous doc and starts following the
    // new one. The visible-page enqueue is driven from the page-
    // tracking timer below once the doc has settled.
    if (m_ocrController) {
        m_ocrController->setDocument(doc);
        // Sync the page-poll baseline so the poll doesn't re-fire the same
        // page we push here.
        m_lastOcrPage = doc ? doc->currentPage() : -1;
        if (doc && doc->supportsSelectableText()) {
            m_ocrController->onVisiblePageChanged(doc->currentPage());
        } else {
            // Null / non-OCR document: re-derive so a missing-model hint
            // left over from the previous document hides (ADR 0002 review
            // item 2). onVisiblePageChanged wouldn't run for these.
            m_ocrController->refreshModelHint();
        }
    }

    m_sidebar->setDocument(doc);
    m_animationBar->setDocument(doc);
    m_inspector->setDocument(doc);

    if (doc) {
        doc->setAnnotationStyle(m_markupToolbar->style());
        doc->setAnnotationTool(m_markupToolbar->activeTool());
        if (auto *store = doc->annotations()) {
            // Qt::UniqueConnection only works with pointer-to-member
            // slots — lambdas are silently rejected with a warning.
            // Connecting to a named slot lets this be called
            // repeatedly (every tab switch, every reopen) without
            // accumulating duplicate connections.
            connect(store, &AnnotationStore::changed, this,
                    &MainWindow::onActiveAnnotationStoreChanged, Qt::UniqueConnection);
        }
        // Re-run the forms-toolbar setup when the doc's async form detection
        // completes. Since PR #63 the qpdf parse behind supportsFormFilling()
        // runs on a background worker, so the forms capability is not known at
        // open; the notifier fires once it is. Named-slot + UniqueConnection so
        // repeated tab switches don't accumulate duplicate connections.
        if (auto *notifier = doc->capabilityNotifier()) {
            connect(notifier, &CapabilityNotifier::capabilitiesChanged, this,
                    &MainWindow::onDocumentCapabilitiesChanged, Qt::UniqueConnection);
        }
        // Forward annotation-selection changes from the doc's overlay
        // to the Inspector. The overlay is a child of the doc's view
        // widget; we re-find it on every focus change because the
        // overlay can be torn down and rebuilt by the adapter.
        if (auto *overlay = findChild<AnnotationOverlay *>()) {
            connect(overlay, &AnnotationOverlay::selectionChanged, this,
                    &MainWindow::onAnnotationSelectionChanged, Qt::UniqueConnection);
            // Auto-switch the toolbar back to Select after a one-shot
            // shape commit. Without this the user must manually click
            // Select on the markup toolbar before they can grab the
            // shape they just drew to move or resize it — the 2026-05-20
            // HITL pass flagged the friction. Sticky-draw mode (keep
            // the active tool after commit) would be an opt-in setting.
            // The toolbar's setActiveTool() flips the action's checked
            // state, which fires activeToolChanged → setAnnotationTool
            // on the doc → overlay->setActiveTool(Select), so the
            // overlay and toolbar stay in sync without us touching the
            // overlay's m_tool directly here.
            connect(overlay, &AnnotationOverlay::annotationCommitted, this,
                    &MainWindow::onAnnotationCommitted, Qt::UniqueConnection);
            // Wire SAM plumbing into the overlay so the InstantAlpha /
            // SmartLasso tool branches can fire decoder passes and
            // commit results without going through a modal dialog. The
            // raw IDocument pointer (IDocument is not a QObject) is
            // re-checked against the active doc on every invocation;
            // documentAboutToBeRemoved cancels pending SAM work before
            // the doc is destroyed, so by the time the handlers fire
            // the pointer is either live or the controller has already
            // forgotten it.
            auto *imgDoc = dynamic_cast<ImageDocument *>(doc);
            overlay->setSamController(m_samController);
            if (imgDoc) {
                DocumentView *dvPtr = m_documentView;
                IDocument *expectedDoc = doc;
                overlay->setSamImageProvider([dvPtr, expectedDoc, imgDoc]() -> QImage {
                    if (!dvPtr || dvPtr->currentDocument() != expectedDoc)
                        return {};
                    return imgDoc->image();
                });
                QPointer<MainWindow> mwPtr(this);
                overlay->setInstantAlphaCommitHandler(
                    [mwPtr, dvPtr, expectedDoc, imgDoc](const QImage &result) {
                        if (!mwPtr || !dvPtr || dvPtr->currentDocument() != expectedDoc ||
                            result.isNull())
                            return;
                        if (!imgDoc->replaceImage(result))
                            return;
                        mwPtr->m_sidebar->refreshThumbnails();
                        mwPtr->updateTitleForDocument(expectedDoc);
                    });
                overlay->setSmartLassoCommitHandler(
                    [mwPtr, dvPtr, expectedDoc, imgDoc](const QPolygon &poly) {
                        if (!mwPtr || !dvPtr || dvPtr->currentDocument() != expectedDoc ||
                            poly.isEmpty())
                            return;
                        const QRect bounds =
                            poly.boundingRect().intersected(QRect(QPoint(), imgDoc->image().size()));
                        if (bounds.width() < 2 || bounds.height() < 2)
                            return;
                        if (!imgDoc->cropToRect(bounds.x(), bounds.y(), bounds.width(),
                                                bounds.height()))
                            return;
                        mwPtr->m_sidebar->refreshThumbnails();
                        mwPtr->updateTitleForDocument(expectedDoc);
                    });
            } else {
                overlay->setSamImageProvider({});
                overlay->setInstantAlphaCommitHandler({});
                overlay->setSmartLassoCommitHandler({});
            }
            if (m_samController) {
                m_samController->setDocument(doc, doc->currentPage());
            }
        } else if (m_samController) {
            m_samController->setDocument(nullptr, 0);
        }
    } else if (m_samController) {
        m_samController->setDocument(nullptr, 0);
    }

    const bool hasPrint = doc && doc->supportsPrint();
    m_printAction->setEnabled(hasPrint);
    if (m_shareAction && ShareService::isAvailable()) {
        // Share needs a file on disk; disabled for unsaved /
        // untitled docs. The user is told to save first via
        // flashStatus when they pick the action with no path.
        // When ShareService is unavailable the action stays disabled
        // with its explanatory tooltip (set at creation) — don't touch it.
        m_shareAction->setEnabled(doc && !doc->filePath().isEmpty());
    }

    const bool hasSearch = doc && doc->supportsSearch();
    m_findAction->setEnabled(hasSearch);
    m_findNextAction->setEnabled(hasSearch);
    m_findPreviousAction->setEnabled(hasSearch);
    if (m_searchButton) {
        m_searchButton->setEnabled(hasSearch);
        m_searchButton->setToolTip(
            hasSearch
                ? tr("Search (%1)")
                      .arg(QKeySequence(QKeySequence::Find).toString(QKeySequence::NativeText))
                : tr("Search is not available for this document type."));
    }
    if (!hasSearch && m_searchBar->isVisible()) {
        hideSearchBar();
    }

    const bool hasZoom = doc && doc->supportsZoom();
    m_zoomInAction->setEnabled(hasZoom);
    m_zoomOutAction->setEnabled(hasZoom);
    m_zoomActualAction->setEnabled(hasZoom);
    m_zoomFitAction->setEnabled(hasZoom);
    m_zoomFitPageAction->setEnabled(hasZoom);

    m_magnifierAction->setEnabled(doc != nullptr);
    if (!doc && m_magnifierAction->isChecked()) {
        m_magnifierAction->setChecked(false);
    } else if (m_magnifierAction->isChecked()) {
        m_magnifier->setTarget(m_documentView->currentWidget());
    }

    const bool hasModes = doc && doc->supportsViewModes();
    m_singlePageAction->setEnabled(hasModes);
    m_continuousAction->setEnabled(hasModes);
    // m_twoPagesAction stays disabled pending implementation.

    const bool multiplePages = doc && doc->pageCount() > 1;
    m_previousPageAction->setEnabled(multiplePages);
    m_nextPageAction->setEnabled(multiplePages);

    updateUndoRedoActions(doc);
    // Copy Page as Image works for anything that can render a page raster
    // (PDF pages + images), so it's gated on thumbnail support rather
    // than editability — and greyed out for stub / unsupported docs.
    m_copyPageAction->setEnabled(doc && doc->supportsThumbnails());

    const bool canEdit = doc && doc->supportsEditing();
    const bool isImage = dynamic_cast<ImageDocument *>(doc) != nullptr;
    const bool isPdfLike = canEdit && !isImage;
    m_saveAction->setEnabled(canEdit);
    m_saveAsAction->setEnabled(canEdit);
    m_rotateLeftAction->setEnabled(canEdit);
    m_rotateRightAction->setEnabled(canEdit);
    m_flipHorizontalAction->setEnabled(canEdit);
    m_flipVerticalAction->setEnabled(canEdit);
    m_adjustSizeAction->setEnabled(canEdit && isImage);
    m_adjustColourAction->setEnabled(canEdit && isImage);
    // ML features: grey out when policy *would* prevent a download we
    // need to run. If a model is already cached (manual install, or the
    // user flipped the policy after downloading), the policy is a no-op
    // for that id — the feature stays usable. The tooltip is only set
    // when policy is actually the reason for disablement, so it doesn't
    // appear over actions disabled for unrelated reasons (wrong doc
    // type, can't edit, etc.).
    auto applyMlPolicy = [this](QAction *action, bool baseEnabled,
                                std::initializer_list<ModelId> required) {
        ModelRegistry &reg = m_app->modelRegistry();
        bool policyBlocksPending = false;
        for (ModelId id : required) {
            if (reg.isAvailable(id))
                continue;
            if (ModelPolicy::isNeverDownload(m_app, id)) {
                policyBlocksPending = true;
                break;
            }
        }
        action->setEnabled(baseEnabled && !policyBlocksPending);
        // PHILOSOPHY: when a feature is unavailable because of the
        // ML-policy gate, the menu entry stays disabled and the
        // tooltip points the user at exactly where to fix it. No
        // popup, ever — the user clicks the menu, sees a greyed-out
        // entry with a one-line explanation, and knows where to go.
        action->setToolTip(baseEnabled && policyBlocksPending
                               ? tr("This model is set to Never Download. "
                                    "Open Tools → Manage ML Models… to allow it.")
                               : QString());
    };
    applyMlPolicy(m_removeBackgroundAction, canEdit && isImage, {ModelId::U2NetP});
    // Schedule the background-candidate heuristic for image docs we
    // haven't scored yet; the result feeds updateRemoveBackgroundBadge
    // below (and also lands again from the scorer's completion
    // callback, so a tab switch immediately after a scoring pass
    // doesn't drop the verdict). Non-image docs are no-ops.
    scheduleBackgroundCandidateScore(doc);
    updateRemoveBackgroundBadge(doc);
    applyMlPolicy(m_instantAlphaAction, canEdit && isImage,
                  {ModelId::MobileSamEncoder, ModelId::MobileSamDecoder});
    applyMlPolicy(m_smartLassoAction, canEdit && isImage,
                  {ModelId::MobileSamEncoder, ModelId::MobileSamDecoder});
    // Recognize Text reads pixels for both images and PDFs (Workstream
    // F brought PDFs into scope). Documents that don't expose a
    // SelectableTextStore (StubAdapter) stay disabled.
    const bool canOcr = doc != nullptr && doc->supportsSelectableText();
    applyMlPolicy(m_recognizeTextAction, canOcr,
                  {ModelId::PpOcrDetector, ModelId::PpOcrRecognizerLatin});
    if (canOcr && m_recognizeTextAction && !m_recognizeTextAction->isEnabled()) {
        // ADR 0002 §3 / G6: the base doc type supports OCR but policy is
        // blocking the one-time download — override the shared tooltip
        // with benefit-first wording that names the download path and
        // avoids the "model"/"OCR" jargon token.
        m_recognizeTextAction->setToolTip(
            tr("Text recognition needs a one-time language download "
               "(Tools → Manage ML Models…)."));
    }
    if (!canOcr && m_recognizeTextAction) {
        m_recognizeTextAction->setToolTip(
            doc ? tr("Recognize Text needs a document with selectable raster "
                     "content.")
                : tr("Open a document to recognize text."));
    }
    m_exportAsAction->setEnabled(doc != nullptr && isImage);
    m_exportPasswordProtectedAction->setEnabled(doc && doc->supportsPasswordExport());
    m_reduceFileSizeAction->setEnabled(doc && doc->supportsFileSizeReduction());
    m_cropImageAction->setEnabled(canEdit && isImage);
    m_insertPagesAction->setEnabled(isPdfLike);
    m_cropPagesAction->setEnabled(isPdfLike);
    // Forms-toolbar enable/populate. Extracted so it can run both here (at
    // open) AND when the document's async form detection later completes
    // (capabilitiesChanged → onDocumentCapabilitiesChanged). Since PR #63 the
    // qpdf parse that answers supportsFormFilling() runs on a background
    // worker, so at open this reports "no forms yet" and the block re-runs a
    // moment later when the answer lands.
    refreshFormCapabilities(doc);
    // My Card editor is always available — a user may want to edit
    // their card even without a PDF open.
    m_myCardAction->setEnabled(true);

    // Restore view-state in priority order (Workstream I):
    //   1. Per-file: the RecentEntry for this exact path, if it has
    //      captured state — zoom, scroll, page, sidebar mode,
    //      markup-toolbar visibility, window geometry/state.
    //   2. Per-type: the last-closed defaults for this document's
    //      type (PDF / Image). Applied only when there's no per-file
    //      state and the doc has a recognised type.
    //   3. Otherwise: leave the constructor / hardcoded defaults in
    //      place. The hardcoded defaults are owned by Workstream A
    //      (fit-to-content / sidebar hidden / markup-toolbar hidden);
    //      this path just falls through to whatever they end up being.
    //
    // One-shot per document — switching tabs after manual navigation
    // must not bounce back to the saved page.
    if (doc && !doc->filePath().isEmpty() && !m_restoredViewStateDocs.contains(doc)) {
        m_restoredViewStateDocs.insert(doc);
        const RecentEntry entry = m_app->recentFiles().findByPath(doc->filePath());
        bool restoredPerFileState = false;
        if (!entry.path.isEmpty() && entry.hasViewState()) {
            restoredPerFileState = true;
            if (entry.currentPage >= 0 && doc->pageCount() > entry.currentPage) {
                doc->goToPage(entry.currentPage);
            }
            doc->applyZoomState(entry.zoomMode, entry.zoomFactor);
            if (entry.scrollY != 0) {
                doc->applyScrollY(entry.scrollY);
            }
            m_sidebar->setMode(static_cast<Sidebar::Mode>(static_cast<int>(entry.sidebarMode)));
            // Apply window-level layout first — restoreState() walks
            // every dock/toolbar this MainWindow owns and sets its
            // visibility from the blob, so the explicit show()/hide()
            // calls below need to land *after* it to win.
            if (!entry.windowGeometry.isEmpty()) {
                restoreGeometry(entry.windowGeometry);
            }
            if (!entry.windowState.isEmpty()) {
                restoreState(entry.windowState);
            }
            if (entry.markupToolbarVisible) {
                m_markupToolbar->show();
            } else {
                m_markupToolbar->hide();
            }
        } else if (doc->documentType() != DocumentType::Unknown) {
            // Per-type fallback: apply the last-closed defaults for
            // this document's type, if any.
            const DocumentTypeDefault def =
                m_app->documentTypeDefaults().forType(doc->documentType());
            if (def.hasState()) {
                doc->applyZoomState(def.zoomMode, def.zoomFactor);
                m_sidebar->setMode(static_cast<Sidebar::Mode>(static_cast<int>(def.sidebarMode)));
                if (!def.windowGeometry.isEmpty()) {
                    restoreGeometry(def.windowGeometry);
                }
                if (!def.windowState.isEmpty()) {
                    restoreState(def.windowState);
                }
                if (def.markupToolbarVisible) {
                    m_markupToolbar->show();
                } else {
                    m_markupToolbar->hide();
                }
            }
        }

        // Content-aware first-open defaults (roadmap Now #3): when the
        // user has no saved per-file state, let the document's own
        // contents pick the sidebar — long docs open to page thumbnails
        // for navigation; shorter forms force the sidebar hidden for a
        // clean filling view (the form toolbar surfaces separately).
        // This overrides the per-type / global sidebar choice applied
        // above, but never an explicit per-file choice (which set
        // restoredPerFileState and is honoured exactly as saved).
        if (!restoredPerFileState) {
            const int formFieldCount =
                doc->supportsFormFilling() ? static_cast<int>(doc->formFields().size()) : 0;
            if (const auto mode = contentAwareSidebarMode(
                    doc->pageCount(), doc->supportsFormFilling(), formFieldCount)) {
                m_sidebar->setMode(*mode);
            } else {
                // The form heuristic depends on supportsFormFilling(), which
                // since PR #63 resolves on a background worker and is not known
                // yet. Mark this first-open doc so onDocumentCapabilitiesChanged
                // re-evaluates the content-aware sidebar once detection lands —
                // a short form then correctly hides the sidebar.
                m_contentAwareFormSidebarPending.insert(doc);
            }
        }
    }

    // Markup toolbar is hidden by default — the user surfaces it via
    // View → Toggle Markup Toolbar (Ctrl+Shift+A) or the toolbar
    // toggle on the main toolbar. Per-file / per-type state above
    // restores the user's last-known preference; otherwise the
    // toolbar stays hidden (document-first workflow).
    const bool canAnnotate = doc && doc->annotations() != nullptr;

    // Select All is available whenever there is an annotation store
    // (the overlay exists and the user can place annotations). The
    // action gracefully no-ops when the store is empty.
    m_selectAllAction->setEnabled(canAnnotate);

    // Gate text-aware markup tools on the document's text layer. PDFs
    // always have one; bare images do not until OCR has been run with
    // its results stored back into the document. Redact stays
    // available on plain images — it operates on pixel rectangles, not
    // glyphs.
    const bool hasText = doc && doc->hasTextLayer();
    m_markupToolbar->setToolVisible(AnnotationTool::Underline, hasText);
    m_markupToolbar->setToolVisible(AnnotationTool::Highlight, hasText);
    m_markupToolbar->setToolVisible(AnnotationTool::StrikeOut, hasText);

    // SAM tools (Instant Alpha / Smart Lasso): image-only, and only
    // when the MobileSAM models are reachable (cached on disk, or
    // policy allows downloading them). PHILOSOPHY: a tool the user
    // cannot act on is hidden, not greyed — the markup toolbar
    // shouldn't carry buttons that just pop up "actually no" tooltips.
    const bool samImageEligible = canEdit && isImage;
    bool samPolicyAllows = true;
    if (samImageEligible) {
        ModelRegistry &reg = m_app->modelRegistry();
        for (ModelId id : {ModelId::MobileSamEncoder, ModelId::MobileSamDecoder}) {
            if (!reg.isAvailable(id) && ModelPolicy::isNeverDownload(m_app, id)) {
                samPolicyAllows = false;
                break;
            }
        }
    }
    const bool samToolsVisible = samImageEligible && samPolicyAllows;
    m_markupToolbar->setToolVisible(AnnotationTool::InstantAlpha, samToolsVisible);
    m_markupToolbar->setToolVisible(AnnotationTool::SmartLasso, samToolsVisible);

    // Sidebar TOC picker entry: enabled iff the active document has
    // an outline. If we were already in TableOfContents mode and the
    // new doc has no outline, drop back to Hidden so the dock
    // doesn't show an empty tree.
    if (m_tocSidebarAction) {
        const bool hasOutline = doc && doc->hasOutline();
        m_tocSidebarAction->setEnabled(hasOutline);
        if (!hasOutline && m_sidebar->mode() == Sidebar::Mode::TableOfContents) {
            m_sidebar->setMode(Sidebar::Mode::Hidden);
        }
    }
    // Highlights & Notes picker entry: enabled iff the doc has at
    // least one text-content annotation to list. Falls back to
    // Hidden if we were already in H&N mode and the new doc has
    // none, so the dock doesn't show an empty list.
    if (m_highlightsAndNotesSidebarAction) {
        const int count = m_sidebar->highlightsAndNotesCount();
        m_highlightsAndNotesSidebarAction->setEnabled(count > 0);
        if (count == 0 && m_sidebar->mode() == Sidebar::Mode::HighlightsAndNotes) {
            m_sidebar->setMode(Sidebar::Mode::Hidden);
        }
    }

    syncViewModeActions(doc);
    updateTitleForDocument(doc);

    // Large-doc OCR hint chip. Dismissal is keyed per-document (ADR
    // 0006), so switching documents does not clear another document's
    // dismissal. Visibility is derived by the shared helper, also re-run
    // on page change / after OCR by the m_ocrPagePoll tick so the notice
    // self-clears.
    updateLargeDocOcrHint();
}

void MainWindow::updateLargeDocOcrHint() {
    if (!m_largeDocOcrHint)
        return;
    bool show = false;
    // Cheap guards first; the pageHasText() probe (which re-extracts page
    // text) is reached only for a large, selectable, not-yet-recognised,
    // not-dismissed page — and even then it is served from a per-(doc,
    // page) cache.
    auto *doc = m_documentView->currentDocument();
    if (doc && !m_largeDocOcrHintDismissed.contains(doc) && m_ocrController &&
        m_ocrController->isLargeDoc() && doc->supportsSelectableText()) {
        const int page = doc->currentPage();
        auto *store = doc->selectableText();
        // Show only for a genuinely text-less page: no cached OCR results
        // AND no native text layer (ADR 0006 — the missing real per-page
        // guard was why this fired on born-digital docs).
        if (store && !store->hasResults(page)) {
            bool hasText;
            if (m_pageHasTextCacheDoc == doc && m_pageHasTextCachePage == page) {
                hasText = m_pageHasTextCacheValue;
            } else {
                hasText = doc->pageHasText(page);
                m_pageHasTextCacheDoc = doc;
                m_pageHasTextCachePage = page;
                m_pageHasTextCacheValue = hasText;
            }
            if (!hasText)
                show = true;
        }
    }
    m_largeDocOcrHint->setVisible(show);
}

void MainWindow::onActiveAnnotationStoreChanged() {
    // The changed signal is connected to whichever document is
    // current when it first becomes visible; the document's own
    // lifetime outlives the tab it's shown in, so dispatch through
    // the tab widget rather than capturing a pointer.
    updateTitleForDocument(m_documentView->currentDocument());
    // Keep the Highlights & Notes picker entry's enabled-state in
    // sync as the user adds / removes annotations. The Sidebar's
    // own refreshAnnotations is already connected to the same
    // signal directly; this just gates the menu item.
    if (m_highlightsAndNotesSidebarAction && m_sidebar) {
        const int count = m_sidebar->highlightsAndNotesCount();
        m_highlightsAndNotesSidebarAction->setEnabled(count > 0);
        if (count == 0 && m_sidebar->mode() == Sidebar::Mode::HighlightsAndNotes) {
            m_sidebar->setMode(Sidebar::Mode::Hidden);
        }
    }
}

void MainWindow::refreshFormCapabilities(IDocument *doc) {
    const bool hasForms = doc && doc->supportsFormFilling();
    if (!hasForms && m_fillFormsAction->isChecked()) {
        // Deactivate fill-forms mode when switching to a document that
        // doesn't support it, without triggering the toggled signal.
        QSignalBlocker blk(m_fillFormsAction);
        m_fillFormsAction->setChecked(false);
    }
    m_fillFormsAction->setEnabled(hasForms);
    m_autoFillFormAction->setEnabled(hasForms);
    // Auto-enable Fill Forms the first time we see a fillable document
    // so the user gets visible widgets + click-to-type without having
    // to discover the menu toggle. We only do this once per document
    // pointer — if the user explicitly toggles it off, we respect that
    // for the rest of the document's lifetime.
    if (hasForms && !m_autoEnabledFormDocs.contains(doc) && !m_fillFormsAction->isChecked()) {
        m_autoEnabledFormDocs.insert(doc);
        m_fillFormsAction->setChecked(true); // → setFormFillingActive(true)
    }
}

void MainWindow::onDocumentCapabilitiesChanged() {
    // A document's async capability detection (the qpdf parse + AcroForm
    // scan, now off the GUI thread per PR #63) just completed. Re-run the
    // forms-toolbar setup for whichever document is currently active. The
    // notifier is per-document, but refreshing the current doc is always
    // correct (idempotent) even if the user switched tabs during the load.
    IDocument *doc = m_documentView ? m_documentView->currentDocument() : nullptr;
    refreshFormCapabilities(doc);
    // If this first-open doc deferred its content-aware sidebar decision
    // pending form detection (see onCurrentDocumentChanged), re-evaluate it
    // now that the answer is known — a short form hides the sidebar.
    if (doc && m_sidebar && m_contentAwareFormSidebarPending.remove(doc)) {
        const int formFieldCount =
            doc->supportsFormFilling() ? static_cast<int>(doc->formFields().size()) : 0;
        if (const auto mode = contentAwareSidebarMode(doc->pageCount(), doc->supportsFormFilling(),
                                                      formFieldCount)) {
            m_sidebar->setMode(*mode);
        }
    }
}

void MainWindow::onAnnotationSelectionChanged(int id) {
    auto *doc = m_documentView->currentDocument();
    if (!doc) {
        m_inspector->clearSelection();
        return;
    }
    if (id == 0) {
        m_inspector->clearSelection();
        return;
    }
    // Track the selection in the Inspector's data layer so the next
    // ⌘I open lands on the right annotation, but DO NOT pop the
    // pane open just because the user clicked. Inspector visibility
    // is under the user's control — the auto-open was annoying for
    // the common select-and-delete / select-and-nudge flow.
    m_inspector->setAnnotation(doc->annotations(), id);
}

void MainWindow::onAnnotationCommitted(int /*id*/) {
    // After a shape commit, flip the markup toolbar back to Select so
    // the user can immediately grab the freshly-drawn shape to move /
    // resize / restyle. The toolbar's setActiveTool() propagates
    // through activeToolChanged → doc->setAnnotationTool(Select) →
    // overlay->setActiveTool(Select), keeping all three (toolbar UI,
    // doc tool state, overlay tool state) in sync.
    //
    // Guard against re-entry from this same handler: setActiveTool is
    // a no-op when the requested tool already matches the current one.
    if (!m_markupToolbar)
        return;
    if (m_markupToolbar->activeTool() == AnnotationTool::Select)
        return;
    m_markupToolbar->setActiveTool(AnnotationTool::Select);
}

void MainWindow::syncViewModeActions(IDocument *doc) {
    if (!doc || !doc->supportsViewModes()) {
        m_singlePageAction->setChecked(false);
        m_twoPagesAction->setChecked(false);
        m_continuousAction->setChecked(false);
        return;
    }
    switch (doc->viewMode()) {
    case ViewMode::SinglePage:
        m_singlePageAction->setChecked(true);
        break;
    case ViewMode::TwoPages:
        m_twoPagesAction->setChecked(true);
        break;
    case ViewMode::Continuous:
        m_continuousAction->setChecked(true);
        break;
    }
}

void MainWindow::rebuildRecentMenu() {
    if (!m_recentMenu) {
        return;
    }
    m_recentMenu->clear();

    const auto entries = m_app->recentFiles().entries();
    if (entries.isEmpty()) {
        auto *empty = m_recentMenu->addAction(tr("(Empty)"));
        empty->setEnabled(false);
        return;
    }

    for (const RecentEntry &entry : entries) {
        auto *action = m_recentMenu->addAction(entry.displayName);
        action->setToolTip(entry.path);
        const QString path = entry.path;
        connect(action, &QAction::triggered, this, [this, path]() { m_app->openFiles({path}); });
    }

    m_recentMenu->addSeparator();
    auto *clear = m_recentMenu->addAction(tr("Clear Menu"));
    connect(clear, &QAction::triggered, this, [this]() { m_app->clearRecent(); });
}

void MainWindow::onOpen() {
    const QStringList paths =
        QFileDialog::getOpenFileNames(this, tr("Open"), QString(), tr("All files (*)"));
    if (!paths.isEmpty()) {
        m_app->openFiles(paths);
    }
}

void MainWindow::onAbout() {
    QMessageBox::about(this, tr("About Trailer"),
                       tr("<h3>Trailer</h3>"
                          "<p>Cross-platform PDF and image workbench.</p>"
                          "<p>Version %1 (Phase 1)</p>")
                           .arg(QString::fromLatin1(TRAILER_VERSION_STRING)));
}

void MainWindow::onAutoFillCurrentForm() {
    auto *doc = m_documentView->currentDocument();
    if (!doc || !doc->supportsFormFilling()) {
        flashStatus(tr("AutoFill: this document has no fillable form fields."));
        return;
    }

    CardStore store;
    store.load();

    // If the user has no card yet, open the dialog inline so AutoFill
    // has something to work with on first use. They can still Cancel.
    if (!store.hasActive()) {
        MyCardDialog dialog(this);
        if (dialog.exec() != QDialog::Accepted)
            return;
        MyCard card = dialog.card();
        if (card.label.isEmpty())
            card.label = tr("My Card");
        store.addCard(std::move(card));
        store.save();
    }

    const MyCard card = store.activeCard();
    const AutoFillResult r = autoFillDocument(doc, card);

    // Push the new values into the form overlay so the user sees them
    // immediately. Also turn on form-filling mode — AutoFill is a
    // strong signal that the user wants to interact with the form
    // (either to spot-check what was filled or to finish remaining
    // fields by hand).
    doc->refreshFormView();
    if (r.filled > 0 && !m_fillFormsAction->isChecked()) {
        m_fillFormsAction->setChecked(true); // triggers setFormFillingActive(true)
    }

    // Inline status message instead of a modal — the popup was a
    // context-breaker on every invocation. "Filled 0 of N" still gets
    // surfaced so the user knows nothing matched, but via the status
    // bar which doesn't demand a click-through.
    const QString label = card.label.isEmpty() ? tr("My Card") : card.label;
    QString status;
    if (r.filled == 0 && r.examined > 0) {
        status = tr("AutoFill: \"%1\" matched none of the %2 text field(s). "
                    "The PDF may use non-standard field names.")
                     .arg(label)
                     .arg(r.examined);
    } else if (r.filled == 0) {
        status = tr("AutoFill: no text fields to fill in this document.");
    } else {
        status = tr("AutoFill: filled %1 of %2 text field(s) from \"%3\".")
                     .arg(r.filled)
                     .arg(r.examined)
                     .arg(label);
    }
    statusBar()->showMessage(status, 8000);
}

void MainWindow::onManageMyCard() {
    CardStore store;
    store.load();

    MyCardDialog dialog(this);
    if (store.hasActive()) {
        dialog.setCard(store.activeCard());
    }
    if (dialog.exec() != QDialog::Accepted)
        return;

    MyCard card = dialog.card();
    if (card.label.isEmpty())
        card.label = tr("My Card");
    if (store.hasActive()) {
        store.replaceCard(store.activeIndex(), std::move(card));
    } else {
        store.addCard(std::move(card));
    }
    store.save();
}

void MainWindow::onSignHere(const QPoint &anchorGlobalPos) {
    auto *doc = m_documentView->currentDocument();
    if (!doc)
        return;

    // Popover anchored under the Sign-Here button on the form
    // toolbar (per the 2026-04-24 review: "Signature placement uses
    // a popover, not a dialog"). The picker handles "Add…" inline —
    // user goes through capture and the new signature is auto-armed
    // as if they'd picked it from an existing list.
    const QPoint anchor = anchorGlobalPos.isNull() ? QCursor::pos() : anchorGlobalPos;
    const QString id = SignaturePicker::show(this, anchor);
    if (id.isEmpty())
        return;

    // Resolve the id to an absolute PNG path via a fresh store scan.
    // The picker only hands back the id so the resolution detail
    // stays local to this call site.
    SignatureStore store;
    QString pngPath;
    for (const Signature &s : store.loadAll()) {
        if (s.id == id) {
            pngPath = s.pngPath;
            break;
        }
    }
    if (pngPath.isEmpty())
        return;

    doc->setPendingSignaturePath(pngPath);
    doc->setAnnotationTool(AnnotationTool::Signature);
}

void MainWindow::onManageSignatures() {
    SignaturesDialog dialog(this);
    dialog.exec();
}

bool MainWindow::confirmRedactionFirstUse() {
    return confirmFirstUse(QStringLiteral("redaction"), tr("About redaction"),
                           tr("Redaction in Trailer is not defence-grade.\n\n"
                              "Painting the tool covers content with a black "
                              "block. On save, the affected page is rasterised "
                              "and the original text and glyphs are destroyed, "
                              "not merely hidden. However, Trailer does not "
                              "touch other parts of the document such as "
                              "bookmarks, attachments, encrypted layers, or "
                              "document metadata.\n\n"
                              "For high-stakes redaction (legal discovery, "
                              "government disclosure, journalism involving "
                              "named sources), use a tool that can scrub "
                              "object streams and metadata as well."),
                           tr("Use Redaction"));
}

bool MainWindow::confirmFirstUse(const QString &key, const QString &title, const QString &body,
                                 const QString &acceptText) {
    Settings &s = m_app->settings();
    if (s.firstUseAcknowledged(key))
        return true;

    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(title);
    // The body is shown as informative text so the box automatically
    // wraps wide paragraphs nicely; the dialog title carries the
    // headline.
    box.setText(title);
    box.setInformativeText(body);
    box.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Ok);
    if (!acceptText.isEmpty()) {
        box.button(QMessageBox::Ok)->setText(acceptText);
    }
    if (box.exec() != QMessageBox::Ok)
        return false;

    s.setFirstUseAcknowledged(key, true);
    s.save();
    return true;
}

void MainWindow::flashError(const QString &message) {
    // 12 s gives a reader enough time to notice the message even if
    // their eyes were on the document. The leading glyph differentiates
    // an error from a neutral status without us toggling palette state.
    statusBar()->showMessage(QStringLiteral("⚠ ") + message, 12000);
}

void MainWindow::flashSuccess(const QString &message) {
    statusBar()->showMessage(QStringLiteral("✓ ") + message, 6000);
}

void MainWindow::flashStatus(const QString &message) {
    statusBar()->showMessage(message, 4000);
}

void MainWindow::addDocument(std::unique_ptr<IDocument> document) {
    if (!document) {
        return;
    }
    m_documentView->addDocument(std::move(document));
    // A document is now open — swap the central stack back to the
    // document page (away from the empty state).
    updateEmptyState();
}

void MainWindow::onAllTabsClosed() {
#ifdef Q_OS_MACOS
    // macOS: there is no persistent empty window. Closing the last
    // document closes the window; the global menu bar persists so the
    // user can still open a file or quit.
    close();
#else
    // Win/Linux: closing the last document of a NON-last window closes
    // that window (avoid empty-window pile-up). The last remaining
    // window persists as an empty-state window so the app is never
    // left with zero windows / no way to open a file.
    //
    // windowCount() is only reliable here because we assume window
    // teardowns do not overlap within a single event-loop turn: a
    // closing window is tracked via a QPointer that nulls (dropping the
    // count) only after its deleteLater() is processed on a later turn.
    // If two windows were torn down in the same turn this comparison
    // could momentarily see a stale count; the app's single-threaded,
    // one-close-per-turn UI flow guarantees that does not happen.
    if (m_app && m_app->windowCount() > 1) {
        close();
    } else {
        updateEmptyState();
    }
#endif
}

void MainWindow::updateEmptyState() {
    if (!m_centerStack)
        return;
    const bool empty = documentCount() == 0;
    m_centerStack->setCurrentWidget(empty ? static_cast<QWidget *>(m_emptyState)
                                          : static_cast<QWidget *>(m_documentPage));

    // Gate the toolbar toggle actions on document presence, mirroring the
    // other document-dependent actions (rotate/save/zoom) toggled in
    // onCurrentDocumentChanged(). Over the empty state these toggles would
    // re-summon a toolbar whose tools no-op — a "lying control" — so we
    // disable the toggle actions themselves, which also greys their
    // View-menu entries and inerts the Ctrl/Cmd+Shift+A shortcut. We only
    // touch enabled state here, never visibility: the user's prior toolbar
    // visibility preference is restored from saved state when a document is
    // reopened (onCurrentDocumentChanged), so re-enabling must not force-show.
    if (m_markupToolbarAction)
        m_markupToolbarAction->setEnabled(!empty);
    if (m_formToolbarAction)
        m_formToolbarAction->setEnabled(!empty);

    if (empty) {
        // No lying controls: the document-only toolbars must not linger
        // over the empty state showing annotation / form-fill buttons
        // that would act on the now-closed document. (Their tool
        // handlers already no-op when there is no current document, but
        // leaving them visible reads as an actionable control that does
        // nothing.) Per-document visibility is restored from saved
        // state when a document is reopened into this window
        // (onCurrentDocumentChanged), so hiding here loses no user
        // preference.
        if (m_markupToolbar)
            m_markupToolbar->hide();
        if (m_formToolbar)
            m_formToolbar->hide();
    }
}

int MainWindow::documentCount() const {
    return m_documentView->documentCount();
}

int MainWindow::documentAt(int index, IDocument **out) const {
    return m_documentView->documentAt(index, out);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent *event) {
    QStringList paths;
    for (const QUrl &url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            paths.append(url.toLocalFile());
        }
    }
    if (!paths.isEmpty()) {
        m_app->openFiles(paths);
        event->acceptProposedAction();
    }
}

void MainWindow::onOpenPreferences() {
    PreferencesDialog dlg(m_app->settings(), this);
    dlg.setManageModelsCallback([this]() { showModelManagerDialog(this, m_app); });
    dlg.setResetAllCallback([this]() { onResetTrailerSettings(); });
    // recent_max is consumed once at startup (not read live), so re-apply
    // it to the live RecentFiles cap when the user saves preferences.
    connect(&dlg, &PreferencesDialog::settingsApplied, this, [this]() {
        m_app->recentFiles().setMaxEntries(m_app->settings().recentMax());
    });
    dlg.exec();
}

void MainWindow::onResetTrailerSettings() {
    const QMessageBox::StandardButton ack =
        QMessageBox::warning(this, tr("Reset Trailer"),
                             tr("Reset Trailer to a clean state?\n\n"
                                "This deletes:\n"
                                "  • Preferences (theme, last-saved-dir, autoSave)\n"
                                "  • Recent files history\n"
                                "  • Saved cards (My Card data)\n"
                                "  • Saved signatures\n"
                                "  • Cached ML models (will re-download on next use)\n\n"
                                "Open documents stay open — only Trailer's own state on "
                                "disk is wiped."),
                             QMessageBox::Reset | QMessageBox::Cancel, QMessageBox::Cancel);
    if (ack != QMessageBox::Reset)
        return;

    auto removeFile = [](const QString &path) {
        if (path.isEmpty())
            return;
        QFile::remove(path);
    };
    auto removeDir = [](const QString &path) {
        if (path.isEmpty())
            return;
        QDir(path).removeRecursively();
    };

    removeFile(AppPaths::settingsFile());
    removeFile(AppPaths::recentFile());
    removeFile(AppPaths::cardsFile());
    removeDir(AppPaths::signaturesDir());
    removeDir(AppPaths::modelsDir());

    // Refresh in-memory copies so the rest of the session sees the
    // wipe. Settings reverts to defaults; recent menus rebuild.
    m_app->settings().load();
    m_app->recentFiles().load();
    rebuildRecentMenu();

    flashSuccess(tr("Trailer reset complete."));
}

void MainWindow::keyPressEvent(QKeyEvent *event) {
    // Esc clears transient modes the user might be stuck in:
    //   - Magnifier (the most common one — the lens follows the
    //     cursor and there's no other obvious "exit" affordance).
    //   - Active text-search overlay (handled separately by the
    //     existing SearchBar::dismissed connection, but covering it
    //     here too is harmless).
    if (event->key() == Qt::Key_Escape) {
        if (m_magnifierAction && m_magnifierAction->isChecked()) {
            m_magnifierAction->setChecked(false);
            event->accept();
            return;
        }
    }
    QMainWindow::keyPressEvent(event);
}

QMenu *MainWindow::createPopupMenu() {
    // Disable Qt's default right-click toolbar/dock visibility menu.
    // It's the source of the "I hid a toolbar and can't get it back"
    // frustration: the menu surfaces every toolbar's toggle, including
    // accidental hides from a stray right-click. View → Toggle Markup
    // Toolbar (etc.) and the main toolbar's toggle buttons are the
    // intentional, discoverable controls.
    return nullptr;
}

void MainWindow::closeEvent(QCloseEvent *event) {
    // Capture view-state for every document this window holds so the
    // user picks up where they left off on next reopen. We do this
    // before the save-prompt below — even if the user discards
    // unsaved annotations, the page they were looking at is worth
    // remembering. Sidebar / markup-toolbar visibility, window
    // geometry, and dock layout are per-window, so they get the same
    // value for every doc in this frame.
    const SidebarMode currentSidebar =
        m_sidebar && m_sidebar->isVisible()
            ? static_cast<SidebarMode>(static_cast<int>(m_sidebar->mode()))
            : SidebarMode::Hidden;
    const bool markupVisible = m_markupToolbar && m_markupToolbar->isVisible();
    const QByteArray geometry = saveGeometry();
    const QByteArray winState = saveState();
    const int total = m_documentView->documentCount();
    bool anyCaptured = false;
    DocumentType lastCapturedType = DocumentType::Unknown;
    DocumentTypeDefault typeSnapshot;
    for (int i = 0; i < total; ++i) {
        IDocument *doc = nullptr;
        if (!m_documentView->documentAt(i, &doc) || !doc)
            continue;
        if (doc->filePath().isEmpty())
            continue;
        RecentEntry state;
        state.currentPage = doc->currentPage();
        state.zoomFactor = doc->zoomFactor();
        state.scrollY = doc->scrollY();
        state.zoomMode = doc->zoomMode();
        state.sidebarMode = currentSidebar;
        state.markupToolbarVisible = markupVisible;
        state.windowGeometry = geometry;
        state.windowState = winState;
        m_app->recentFiles().updateViewState(doc->filePath(), state);
        anyCaptured = true;
        // Capture a snapshot for the per-type defaults too. Last-
        // closed-of-type wins; the loop overwrites typeSnapshot
        // each iteration so the final doc's state is what lands.
        if (doc->documentType() != DocumentType::Unknown) {
            lastCapturedType = doc->documentType();
            typeSnapshot.zoomMode = state.zoomMode;
            typeSnapshot.zoomFactor = state.zoomFactor;
            typeSnapshot.sidebarMode = state.sidebarMode;
            typeSnapshot.markupToolbarVisible = state.markupToolbarVisible;
            typeSnapshot.windowGeometry = state.windowGeometry;
            typeSnapshot.windowState = state.windowState;
        }
    }
    if (anyCaptured)
        m_app->recentFiles().save();
    if (lastCapturedType != DocumentType::Unknown) {
        m_app->documentTypeDefaults().setForType(lastCapturedType, typeSnapshot);
        m_app->documentTypeDefaults().save();
    }

    // Headless / test environments (QT_QPA_PLATFORM=offscreen) skip
    // the unsaved-changes prompt: there's no human to click through
    // it, and UAT init slots routinely call w->close() to clean up
    // dirty state between cases. Real users on cocoa/windows/xcb
    // always see the prompt for dirty docs.
    const QString platform = QGuiApplication::platformName();
    if (platform == QLatin1String("offscreen") || platform == QLatin1String("minimal")) {
        event->accept();
        return;
    }

    // Walk every document held by this window. For each dirty one,
    // ask Save / Discard / Cancel. Cancel anywhere aborts the close.
    // The order is current-document-first so the user usually only
    // sees one prompt — the doc they were just looking at.
    std::vector<IDocument *> dirty;
    dirty.reserve(static_cast<size_t>(total));
    if (auto *current = m_documentView->currentDocument()) {
        if (current->isDirty())
            dirty.push_back(current);
    }
    for (int i = 0; i < total; ++i) {
        IDocument *doc = nullptr;
        if (m_documentView->documentAt(i, &doc) && doc && doc->isDirty() &&
            std::find(dirty.begin(), dirty.end(), doc) == dirty.end()) {
            dirty.push_back(doc);
        }
    }
    // NOTE (macOS path (a) — last-tab close): on macOS, closing the last
    // tab routes through DocumentView::onTabCloseRequested → the
    // documentCloseRequested veto (which runs confirmCloseDirtyDoc) →
    // allTabsClosed → close() → here. By the time closeEvent runs the
    // document has already been erased, so this walk finds no dirty docs
    // and does not prompt a second time. The prompt is shown exactly once.
    for (IDocument *doc : dirty) {
        if (!confirmCloseDirtyDoc(doc)) {
            event->ignore();
            return;
        }
        // confirmCloseDirtyDoc returned true: Save succeeded or the user
        // chose Discard. Drop through and let the close proceed.
    }
    event->accept();
}

bool MainWindow::confirmCloseDirtyDoc(IDocument *doc) {
    if (!doc)
        return true;

    // Resolve the outcome. A forced test response bypasses the modal so
    // the offscreen UAT harness can drive Save / Discard / Cancel. With
    // the default (Prompt) response under offscreen / minimal there is no
    // human to click the dialog and no forced choice, so we take the
    // data-loss-first posture: veto (keep the doc) rather than silently
    // discard unsaved edits. The UAT slots always force a response, so
    // they never reach this branch.
    int answer = QMessageBox::Cancel;
    switch (m_closeResponseForTesting) {
    case CloseResponse::Save:
        answer = QMessageBox::Save;
        break;
    case CloseResponse::Discard:
        answer = QMessageBox::Discard;
        break;
    case CloseResponse::Cancel:
        answer = QMessageBox::Cancel;
        break;
    case CloseResponse::Prompt: {
        const QString platform = QGuiApplication::platformName();
        if (platform == QLatin1String("offscreen") || platform == QLatin1String("minimal")) {
            return false;
        }
        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(tr("Unsaved changes"));
        box.setText(tr("Save changes to %1?").arg(doc->displayName()));
        box.setStandardButtons(QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        box.setDefaultButton(QMessageBox::Save);
        answer = box.exec();
        break;
    }
    }

    if (answer == QMessageBox::Cancel)
        return false;
    if (answer == QMessageBox::Save) {
        bool ok = false;
        if (doc->filePath().isEmpty()) {
            // Untitled document: route through the Save-As dialog so the
            // user picks a destination, then save synchronously (like the
            // has-path branch below) so the dirty check reflects the real
            // outcome rather than a still-pending async save.
            const QString path = chooseSaveAsPath(doc);
            if (path.isEmpty()) {
                // The user cancelled Save-As. Honour the cancel: abort the
                // close and keep the unsaved work intact.
                return false;
            }
            ok = doc->save(path);
        } else {
            ok = doc->save();
        }
        if (!ok || doc->isDirty()) {
            // Save failed. Do not lose the user's work; abort the close.
            flashError(tr("Could not save %1; close cancelled.").arg(doc->displayName()));
            return false;
        }
    }
    // Discard, or a successful Save: proceed with closing this doc.
    return true;
}

} // namespace trailer
