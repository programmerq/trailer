#include "MainWindow.h"

#include "TrailerVersion.h"
#include "AnimationBar.h"
#include "DocumentView.h"
#include "AnnotationOverlay.h"
#include "Inspector.h"
#include "Magnifier.h"
#include "FormToolbar.h"
#include "IconHelper.h"
#include "MarkupToolbar.h"
#include "MyCardDialog.h"
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
#include "ml/BackgroundRemover.h"
#include "ml/ModelRegistry.h"
#include "ml/OcrEngine.h"
#include "platform/Share.h"
#include "settings/AppPaths.h"
#include "ml/SamSession.h"
#include "recent/RecentFiles.h"
#include "ModelManagerDialog.h"
#include "OcrResultsDialog.h"
#include "SamSegmentDialog.h"

#include <QAction>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
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
#include <QVBoxLayout>
#include <QWidget>
#include "document/ImageAdapter.h"

namespace trailer {


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
    connect(m_documentView, &DocumentView::currentDocumentChanged, this,
            &MainWindow::onCurrentDocumentChanged);
    // Window-per-file: when the last document in this window is
    // closed, close the window too rather than leaving a ghost frame
    // behind. For the legacy tab mode this still fires correctly —
    // closing the final tab discards the now-empty window, which is
    // also what the user expects.
    connect(m_documentView, &DocumentView::allTabsClosed, this, &MainWindow::close);

    m_animationBar = new AnimationBar(center);
    m_animationBar->hide();

    // The search bar moved into the main toolbar (built later) so
    // it's always visible at the top right. The central column is
    // now just the document view + the animation bar.
    centerLayout->addWidget(m_documentView, 1);
    centerLayout->addWidget(m_animationBar);
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
    // NSSharingServicePicker). Only shown on platforms that have a
    // working implementation; the Linux/Windows stub returns
    // isAvailable() == false until xdg-email / WinShare are wired
    // up. The action is gated on a saved file because the share
    // picker needs a real file path on disk.
    if (ShareService::isAvailable()) {
        m_shareAction = fileMenu->addAction(tr("&Share…"));
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
    addToolBar(Qt::TopToolBarArea, m_mainToolbar);
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

    // Search field is always visible in the toolbar now. The old
    // floating SearchBar that toggled in/out of the central layout
    // has been removed; m_searchBar is parented to the toolbar.
    // We don't call show() — Qt propagates visibility from the
    // QMainWindow at the next show event, and an explicit show()
    // on a child of a still-hidden ancestor has no effect on the
    // explicitly-hidden flag, so leaving it alone is correct.
    m_searchBar->setParent(m_mainToolbar);
    m_searchBar->setMaximumWidth(360);
    m_mainToolbar->addWidget(m_searchBar);
}

void MainWindow::buildEditMenu(QMenu *editMenu) {
    m_undoAction = editMenu->addAction(tr("&Undo"));
    m_undoAction->setShortcut(QKeySequence::Undo);
    connect(m_undoAction, &QAction::triggered, this, [this]() {
        if (auto *doc = m_documentView->currentDocument()) {
            doc->undo();
            m_sidebar->refreshThumbnails();
            updateTitleForDocument(doc);
            onCurrentDocumentChanged(doc);
        }
    });

    m_redoAction = editMenu->addAction(tr("&Redo"));
    m_redoAction->setShortcut(QKeySequence::Redo);
    connect(m_redoAction, &QAction::triggered, this, [this]() {
        if (auto *doc = m_documentView->currentDocument()) {
            doc->redo();
            m_sidebar->refreshThumbnails();
            updateTitleForDocument(doc);
            onCurrentDocumentChanged(doc);
        }
    });

    editMenu->addSeparator();

    m_selectAllAction = editMenu->addAction(tr("Select &All"));
    m_selectAllAction->setShortcut(QKeySequence::SelectAll);
    connect(m_selectAllAction, &QAction::triggered, this, [this]() {
        if (auto* overlay = findChild<AnnotationOverlay*>()) {
            overlay->selectAll();
        }
    });

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
}

void MainWindow::showSearchBar() {
    // The search bar lives in the main toolbar and is always
    // visible. Cmd-F just focuses the input field so the user can
    // type immediately. The supportsSearch gate stays — for
    // documents that can't search, focusing the field would invite
    // the user to type into a no-op.
    auto *doc = m_documentView->currentDocument();
    if (!doc || !doc->supportsSearch()) {
        return;
    }
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
    // "Hide" used to remove the floating search bar from the
    // central column; with the toolbar embedding it's a soft
    // dismiss that returns focus to the document and clears any
    // active query so the highlighted matches go away.
    if (auto *doc = m_documentView->currentDocument()) {
        doc->clearSearch();
    }
    m_searchBar->setMatchCounter(0, 0);
    m_documentView->setFocus();
}

void MainWindow::buildViewMenu(QMenu *viewMenu) {
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

    m_singlePageAction = viewMenu->addAction(tr("Single Page"));
    m_singlePageAction->setCheckable(true);
    connect(m_singlePageAction, &QAction::triggered, this, [this]() {
        if (auto *doc = m_documentView->currentDocument()) {
            doc->setViewMode(ViewMode::SinglePage);
        }
    });

    m_twoPagesAction = viewMenu->addAction(tr("Two Pages"));
    m_twoPagesAction->setCheckable(true);
    m_twoPagesAction->setEnabled(false); // TODO: implement two-page layout
    m_twoPagesAction->setToolTip(tr("Two-page layout is not yet available."));

    m_continuousAction = viewMenu->addAction(tr("Continuous"));
    m_continuousAction->setCheckable(true);
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

    // Zoom shortcuts follow Adobe Acrobat's PDF-reader convention,
    // which is the muscle memory most users bring to a PDF tool:
    //   ⌘0 → Fit Page (whole page in viewport)
    //   ⌘1 → Actual Size (100%)
    //   ⌘2 → Fit Width
    // This deliberately differs from Preview.app's ⌘0=Actual / ⌘9=Fit
    // pattern — Acrobat's three-zoom mapping is what PDF-heavy users
    // already have in their fingers.
    m_zoomFitPageAction = viewMenu->addAction(
        themedActionIcon(QStringLiteral(":/icons/actions/view-fit-page.svg"), this),
        tr("Fit &Page"));
    m_zoomFitPageAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    connect(m_zoomFitPageAction, &QAction::triggered, this, [this]() {
        if (auto *doc = m_documentView->currentDocument())
            doc->zoomFitPage();
    });

    m_zoomActualAction = viewMenu->addAction(
        themedActionIcon(QStringLiteral(":/icons/actions/view-zoom-actual.svg"), this),
        tr("&Actual Size"));
    m_zoomActualAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_1));
    connect(m_zoomActualAction, &QAction::triggered, this, [this]() {
        if (auto *doc = m_documentView->currentDocument())
            doc->zoomActual();
    });

    m_zoomFitAction = viewMenu->addAction(
        themedActionIcon(QStringLiteral(":/icons/actions/view-fit-width.svg"), this),
        tr("&Fit to Width"));
    m_zoomFitAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_2));
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
    // Models…") — without this call, hovering a disabled item shows
    // nothing.
    toolsMenu->setToolTipsVisible(true);

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
    connect(m_adjustSizeAction, &QAction::triggered, this, &MainWindow::onAdjustSize);

    m_adjustColourAction = toolsMenu->addAction(tr("Adjust Co&lour…"));
    connect(m_adjustColourAction, &QAction::triggered, this, &MainWindow::onAdjustColour);

    m_removeBackgroundAction = toolsMenu->addAction(tr("Remove &Background"));
    connect(m_removeBackgroundAction, &QAction::triggered, this, &MainWindow::onRemoveBackground);

    m_instantAlphaAction = toolsMenu->addAction(tr("&Instant Alpha…"));
    connect(m_instantAlphaAction, &QAction::triggered, this, &MainWindow::onInstantAlpha);

    m_smartLassoAction = toolsMenu->addAction(tr("Smart &Lasso…"));
    connect(m_smartLassoAction, &QAction::triggered, this, &MainWindow::onSmartLasso);

    m_recognizeTextAction = toolsMenu->addAction(tr("Reco&gnize Text…"));
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

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Crop Pages"));
    auto *form = new QFormLayout(&dialog);

    auto makeSpin = [&]() {
        auto *s = new QDoubleSpinBox(&dialog);
        s->setRange(0.0, 500.0);
        s->setDecimals(1);
        s->setSuffix(QStringLiteral(" mm"));
        return s;
    };
    auto *leftSpin = makeSpin();
    auto *topSpin = makeSpin();
    auto *rightSpin = makeSpin();
    auto *bottomSpin = makeSpin();
    form->addRow(tr("Left margin"), leftSpin);
    form->addRow(tr("Top margin"), topSpin);
    form->addRow(tr("Right margin"), rightSpin);
    form->addRow(tr("Bottom margin"), bottomSpin);

    auto *allPagesCheck = new QCheckBox(tr("Apply to all pages"), &dialog);
    allPagesCheck->setChecked(true);
    form->addRow(allPagesCheck);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return;

    constexpr double kMmToPt = 72.0 / 25.4;
    const double l = leftSpin->value() * kMmToPt;
    const double t = topSpin->value() * kMmToPt;
    const double r = rightSpin->value() * kMmToPt;
    const double b = bottomSpin->value() * kMmToPt;
    if (l == 0.0 && t == 0.0 && r == 0.0 && b == 0.0)
        return;

    bool anyApplied = false;
    if (allPagesCheck->isChecked()) {
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

    // Inference goes on a worker thread. u2netp on 320x320 is sub-
    // second on a modern CPU but larger canvases / older hardware can
    // hit a couple of seconds. The dialog is indeterminate; cancel
    // suppresses the result-application step but the worker thread
    // itself runs to completion.
    auto *progress = new QProgressDialog(tr("Removing background…"), tr("Cancel"), 0, 0, this);
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->setAutoClose(false);
    progress->setAutoReset(false);

    auto *watcher = new QFutureWatcher<QImage>(this);
    connect(progress, &QProgressDialog::canceled, watcher, &QFutureWatcherBase::cancel);
    connect(watcher, &QFutureWatcher<QImage>::finished, this,
            [this, watcher, progress, doc, imgDoc, remover]() {
                const bool wasCanceled = progress->wasCanceled();
                progress->close();
                progress->deleteLater();
                QImage result;
                if (!wasCanceled)
                    result = watcher->result();
                watcher->deleteLater();
                if (wasCanceled)
                    return;
                if (result.isNull()) {
                    flashError(tr("Remove Background failed — "
                                  "model may be missing or corrupt; "
                                  "try re-downloading from Manage "
                                  "ML Models."));
                    return;
                }
                if (!imgDoc->replaceImage(result)) {
                    flashError(tr("Remove Background failed — "
                                  "could not apply the result to the "
                                  "document."));
                    return;
                }
                m_sidebar->refreshThumbnails();
                updateTitleForDocument(doc);
            });

    const QImage source = imgDoc->image();
    QFuture<QImage> future =
        QtConcurrent::run([source, remover]() { return remover->remove(source); });
    watcher->setFuture(future);
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
    auto *imgDoc = dynamic_cast<ImageDocument *>(doc);
    if (!imgDoc)
        return;

    SamSession session(&m_app->modelRegistry());
    if (!ensureSamModelsReady(this, session))
        return;

    SamSegmentDialog dialog(SamSegmentDialog::Mode::InstantAlpha, imgDoc->image(), &session, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QImage result = dialog.resultImage();
    if (result.isNull()) {
        flashError(tr("Instant Alpha failed — no selection was produced. "
                      "Try adding more points."));
        return;
    }
    if (!imgDoc->replaceImage(result)) {
        flashError(tr("Instant Alpha failed — could not apply the selection "
                      "to the current image."));
        return;
    }
    m_sidebar->refreshThumbnails();
    updateTitleForDocument(doc);
}

void MainWindow::onSmartLasso() {
    auto *doc = m_documentView->currentDocument();
    if (!doc || !doc->supportsEditing())
        return;
    auto *imgDoc = dynamic_cast<ImageDocument *>(doc);
    if (!imgDoc)
        return;

    SamSession session(&m_app->modelRegistry());
    if (!ensureSamModelsReady(this, session))
        return;

    SamSegmentDialog dialog(SamSegmentDialog::Mode::SmartLasso, imgDoc->image(), &session, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QPolygon poly = dialog.resultPolygon();
    if (poly.isEmpty()) {
        flashError(tr("Smart Lasso failed — no object outline was produced."));
        return;
    }

    // Phase 6C ships Smart Lasso as a crop-to-object: we take the
    // polygon's bounding rectangle and let ImageDocument's existing
    // undo-safe cropToRect do the work. A true polygon mask + feather
    // flow is a later phase; the SAM segmentation already gives us
    // the outline on screen for users to review.
    const QRect bounds = poly.boundingRect().intersected(QRect(QPoint(), imgDoc->image().size()));
    if (bounds.width() < 2 || bounds.height() < 2) {
        flashError(tr("Smart Lasso failed — selection is too small to crop to."));
        return;
    }
    if (!imgDoc->cropToRect(bounds.x(), bounds.y(), bounds.width(), bounds.height())) {
        flashError(tr("Smart Lasso failed — could not crop to the selected "
                      "object."));
        return;
    }
    m_sidebar->refreshThumbnails();
    updateTitleForDocument(doc);
}

void MainWindow::onRecognizeText() {
    auto *doc = m_documentView->currentDocument();
    if (!doc)
        return;
    auto *imgDoc = dynamic_cast<ImageDocument *>(doc);
    if (!imgDoc)
        return;
    const QImage source = imgDoc->image();
    if (source.isNull())
        return;

    // The engine has to live for the duration of the worker thread,
    // so it goes on the heap held by a shared_ptr captured by the
    // lambda. ensureOcrModelsReady is itself an async-with-progress
    // flow — block here until models land or the user cancels that
    // dialog, then kick off the actual inference job below.
    auto engine = std::make_shared<OcrEngine>(&m_app->modelRegistry());
    if (!ensureOcrModelsReady(this, *engine))
        return;

    // Inference is the slow part — 1-5 s on a typical scan — so it
    // runs on a worker thread. The progress dialog is indeterminate
    // (we don't have stage-by-stage progress from OcrEngine yet);
    // Cancel just stops us from opening the results dialog when the
    // future completes — actual mid-inference cancellation is a
    // follow-up.
    auto *progress = new QProgressDialog(tr("Recognising text…"), tr("Cancel"), 0, 0, this);
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->setAutoClose(false);
    progress->setAutoReset(false);

    auto *watcher = new QFutureWatcher<QVector<OcrEngine::TextBlock>>(this);
    connect(progress, &QProgressDialog::canceled, watcher, &QFutureWatcherBase::cancel);
    connect(watcher, &QFutureWatcher<QVector<OcrEngine::TextBlock>>::finished, this,
            [this, watcher, progress, doc]() {
                const bool wasCanceled = progress->wasCanceled();
                progress->close();
                progress->deleteLater();
                QVector<OcrEngine::TextBlock> blocks;
                if (!wasCanceled) {
                    blocks = watcher->result();
                }
                watcher->deleteLater();
                if (wasCanceled)
                    return;
                OcrResultsDialog dialog(
                    doc->filePath().isEmpty() ? doc->displayName() : doc->filePath(), blocks, this);
                dialog.exec();
            });

    QFuture<QVector<OcrEngine::TextBlock>> future =
        QtConcurrent::run([source, engine]() { return engine->recognize(source); });
    watcher->setFuture(future);
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
    if (proc.exitCode() != 0 || !QFileInfo(path).exists() || QFileInfo(path).size() == 0) {
        // User cancelled (Esc) or no output — don't treat as an error.
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
    const QString path = QFileDialog::getSaveFileName(this, tr("Save As"), suggested, filter);
    if (path.isEmpty())
        return;
    // saveDocumentAsync handles the success path: status bar, title
    // refresh, lastSaveDir bookkeeping. PDFs go through the two-
    // phase worker; images stay synchronous (fast).
    saveDocumentAsync(doc, path);
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

void MainWindow::onCurrentDocumentChanged(IDocument *doc) {
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
        // Forward annotation-selection changes from the doc's overlay
        // to the Inspector. The overlay is a child of the doc's view
        // widget; we re-find it on every focus change because the
        // overlay can be torn down and rebuilt by the adapter.
        if (auto *overlay = findChild<AnnotationOverlay *>()) {
            connect(overlay, &AnnotationOverlay::selectionChanged, this,
                    &MainWindow::onAnnotationSelectionChanged, Qt::UniqueConnection);
        }
    }

    const bool hasPrint = doc && doc->supportsPrint();
    m_printAction->setEnabled(hasPrint);
    if (m_shareAction) {
        // Share needs a file on disk; disabled for unsaved /
        // untitled docs. The user is told to save first via
        // flashStatus when they pick the action with no path.
        m_shareAction->setEnabled(doc && !doc->filePath().isEmpty());
    }

    const bool hasSearch = doc && doc->supportsSearch();
    m_findAction->setEnabled(hasSearch);
    m_findNextAction->setEnabled(hasSearch);
    m_findPreviousAction->setEnabled(hasSearch);
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
        action->setToolTip(baseEnabled && policyBlocksPending
                               ? tr("Set to Never Download in Manage ML Models. "
                                    "Open that dialog to allow downloads.")
                               : QString());
    };
    applyMlPolicy(m_removeBackgroundAction, canEdit && isImage, {ModelId::U2NetP});
    applyMlPolicy(m_instantAlphaAction, canEdit && isImage,
                  {ModelId::MobileSamEncoder, ModelId::MobileSamDecoder});
    applyMlPolicy(m_smartLassoAction, canEdit && isImage,
                  {ModelId::MobileSamEncoder, ModelId::MobileSamDecoder});
    // Recognize Text only reads pixels, so it doesn't need
    // supportsEditing() — any opened image qualifies, even a
    // read-only-format one. PDFs are deferred to a later phase.
    applyMlPolicy(m_recognizeTextAction, doc != nullptr && isImage,
                  {ModelId::PpOcrDetector, ModelId::PpOcrRecognizerLatin});
    m_exportAsAction->setEnabled(doc != nullptr && isImage);
    m_exportPasswordProtectedAction->setEnabled(doc && doc->supportsPasswordExport());
    m_reduceFileSizeAction->setEnabled(doc && doc->supportsFileSizeReduction());
    m_cropImageAction->setEnabled(canEdit && isImage);
    m_insertPagesAction->setEnabled(isPdfLike);
    m_cropPagesAction->setEnabled(isPdfLike);
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
        if (!entry.path.isEmpty() && entry.hasViewState()) {
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
            // Mark markup-toolbar auto-show as "already done" so the
            // block below doesn't immediately re-show the bar the
            // user explicitly hid before closing this file.
            if (doc->annotations()) {
                m_autoShownMarkupDocs.insert(doc);
            }
        } else if (doc->documentType() != DocumentType::Unknown) {
            // Per-type fallback: apply the last-closed defaults for
            // this document's type, if any.
            const DocumentTypeDefault def =
                m_app->documentTypeDefaults().forType(doc->documentType());
            if (def.hasState()) {
                doc->applyZoomState(def.zoomMode, def.zoomFactor);
                m_sidebar->setMode(
                    static_cast<Sidebar::Mode>(static_cast<int>(def.sidebarMode)));
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
                if (doc->annotations()) {
                    m_autoShownMarkupDocs.insert(doc);
                }
            }
        }
    }

    // Auto-show the markup toolbar the first time we see a document
    // that supports annotations. Same once-per-doc pattern as the form
    // overlay above — an explicit hide by the user sticks. Documents
    // without an AnnotationStore (Stub adapter) are excluded so we
    // never show a toolbar that would be useless.
    // Skipped when we already restored per-file / per-type state above
    // (m_autoShownMarkupDocs was stamped in that branch) so the
    // explicit "hide" carried in saved state isn't overwritten.
    const bool canAnnotate = doc && doc->annotations() != nullptr;
    if (canAnnotate && !m_autoShownMarkupDocs.contains(doc) && !m_markupToolbar->isVisible()) {
        m_autoShownMarkupDocs.insert(doc);
        m_markupToolbar->show();
    }

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
    for (IDocument *doc : dirty) {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(tr("Unsaved changes"));
        box.setText(tr("Save changes to %1?").arg(doc->displayName()));
        box.setStandardButtons(QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        box.setDefaultButton(QMessageBox::Save);
        const int answer = box.exec();
        if (answer == QMessageBox::Cancel) {
            event->ignore();
            return;
        }
        if (answer == QMessageBox::Save) {
            // If the document has no path yet, route through the
            // Save-As dialog so the user picks one. The current-tab
            // assumption matches onSave's behaviour.
            const bool hasPath = !doc->filePath().isEmpty();
            const bool ok = hasPath ? doc->save() : doc->save();
            if (!ok || doc->isDirty()) {
                // Save failed or user cancelled the Save-As dialog.
                // Do not lose the user's work; abort the close.
                flashError(tr("Could not save %1; close cancelled.").arg(doc->displayName()));
                event->ignore();
                return;
            }
        }
        // Discard: drop through and let the close proceed.
    }
    event->accept();
}

} // namespace trailer
