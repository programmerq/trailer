#include "MainWindow.h"

#include "DocumentView.h"
#include "SearchBar.h"
#include "Sidebar.h"
#include "app/Application.h"
#include "recent/RecentFiles.h"

#include <QAction>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QVBoxLayout>
#include <QWidget>

namespace trailer {

MainWindow::MainWindow(Application* app, QWidget* parent)
    : QMainWindow(parent), m_app(app) {
    setAcceptDrops(true);
    setWindowTitle(tr("Trailer"));
    resize(1100, 750);

    auto* center = new QWidget(this);
    auto* centerLayout = new QVBoxLayout(center);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    centerLayout->setSpacing(0);

    m_searchBar = new SearchBar(center);
    m_searchBar->hide();
    connect(m_searchBar, &SearchBar::queryChanged, this, [this](const QString& q) {
        if (auto* doc = m_documentView->currentDocument()) {
            doc->setSearchQuery(q);
        }
    });
    connect(m_searchBar, &SearchBar::findNextRequested, this, [this]() {
        if (auto* doc = m_documentView->currentDocument()) doc->findNext();
    });
    connect(m_searchBar, &SearchBar::findPreviousRequested, this, [this]() {
        if (auto* doc = m_documentView->currentDocument()) doc->findPrevious();
    });
    connect(m_searchBar, &SearchBar::dismissed, this, &MainWindow::hideSearchBar);

    m_documentView = new DocumentView(center);
    connect(m_documentView, &DocumentView::currentDocumentChanged,
            this, &MainWindow::onCurrentDocumentChanged);

    centerLayout->addWidget(m_searchBar);
    centerLayout->addWidget(m_documentView, 1);
    setCentralWidget(center);

    m_sidebar = new Sidebar(this);
    addDockWidget(Qt::LeftDockWidgetArea, m_sidebar);

    buildMenus();
    rebuildRecentMenu();
    onCurrentDocumentChanged(nullptr);
}

void MainWindow::buildMenus() {
    auto* fileMenu = menuBar()->addMenu(tr("&File"));

    auto* openAction = fileMenu->addAction(tr("&Open…"));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::onOpen);

    m_recentMenu = fileMenu->addMenu(tr("Open &Recent"));
    fileMenu->addSeparator();

    auto* closeAction = fileMenu->addAction(tr("&Close Window"));
    closeAction->setShortcut(QKeySequence::Close);
    connect(closeAction, &QAction::triggered, this, &QMainWindow::close);

    auto* quitAction = fileMenu->addAction(tr("&Quit"));
    quitAction->setShortcut(QKeySequence::Quit);
    quitAction->setMenuRole(QAction::QuitRole);
    connect(quitAction, &QAction::triggered, qApp, &QCoreApplication::quit);

    auto* editMenu = menuBar()->addMenu(tr("&Edit"));
    buildEditMenu(editMenu);

    auto* viewMenu = menuBar()->addMenu(tr("&View"));
    buildViewMenu(viewMenu);

    auto* helpMenu = menuBar()->addMenu(tr("&Help"));
    auto* aboutAction = helpMenu->addAction(tr("&About Trailer"));
    aboutAction->setMenuRole(QAction::AboutRole);
    connect(aboutAction, &QAction::triggered, this, &MainWindow::onAbout);
}

void MainWindow::buildEditMenu(QMenu* editMenu) {
    m_findAction = editMenu->addAction(tr("&Find…"));
    m_findAction->setShortcut(QKeySequence::Find);
    connect(m_findAction, &QAction::triggered, this, &MainWindow::showSearchBar);

    m_findNextAction = editMenu->addAction(tr("Find &Next"));
    m_findNextAction->setShortcut(QKeySequence::FindNext);
    connect(m_findNextAction, &QAction::triggered, this, [this]() {
        if (auto* doc = m_documentView->currentDocument()) doc->findNext();
    });

    m_findPreviousAction = editMenu->addAction(tr("Find &Previous"));
    m_findPreviousAction->setShortcut(QKeySequence::FindPrevious);
    connect(m_findPreviousAction, &QAction::triggered, this, [this]() {
        if (auto* doc = m_documentView->currentDocument()) doc->findPrevious();
    });
}

void MainWindow::showSearchBar() {
    auto* doc = m_documentView->currentDocument();
    if (!doc || !doc->supportsSearch()) {
        return;
    }
    m_searchBar->show();
    m_searchBar->focusInput();
}

void MainWindow::hideSearchBar() {
    m_searchBar->hide();
    if (auto* doc = m_documentView->currentDocument()) {
        doc->clearSearch();
    }
    m_documentView->setFocus();
}

void MainWindow::buildViewMenu(QMenu* viewMenu) {
    auto* toggleSidebar = m_sidebar->toggleViewAction();
    toggleSidebar->setText(tr("Toggle &Sidebar"));
    toggleSidebar->setShortcut(QKeySequence(tr("Ctrl+Shift+D")));
    viewMenu->addAction(toggleSidebar);

    viewMenu->addSeparator();

    m_singlePageAction = viewMenu->addAction(tr("Single Page"));
    m_singlePageAction->setCheckable(true);
    connect(m_singlePageAction, &QAction::triggered, this, [this]() {
        if (auto* doc = m_documentView->currentDocument()) {
            doc->setViewMode(ViewMode::SinglePage);
        }
    });

    m_twoPagesAction = viewMenu->addAction(tr("Two Pages"));
    m_twoPagesAction->setCheckable(true);
    m_twoPagesAction->setEnabled(false);  // TODO: implement two-page layout
    m_twoPagesAction->setToolTip(tr("Two-page layout is not yet available."));

    m_continuousAction = viewMenu->addAction(tr("Continuous"));
    m_continuousAction->setCheckable(true);
    connect(m_continuousAction, &QAction::triggered, this, [this]() {
        if (auto* doc = m_documentView->currentDocument()) {
            doc->setViewMode(ViewMode::Continuous);
        }
    });

    m_viewModeGroup = new QActionGroup(this);
    m_viewModeGroup->setExclusive(true);
    m_viewModeGroup->addAction(m_singlePageAction);
    m_viewModeGroup->addAction(m_twoPagesAction);
    m_viewModeGroup->addAction(m_continuousAction);

    viewMenu->addSeparator();

    m_zoomInAction = viewMenu->addAction(tr("Zoom &In"));
    m_zoomInAction->setShortcuts({
        QKeySequence(tr("Ctrl+Alt++")),
        QKeySequence(tr("Ctrl+Alt+=")),
    });
    connect(m_zoomInAction, &QAction::triggered, this, [this]() {
        if (auto* doc = m_documentView->currentDocument()) doc->zoomIn();
    });

    m_zoomOutAction = viewMenu->addAction(tr("Zoom &Out"));
    m_zoomOutAction->setShortcut(QKeySequence(tr("Ctrl+Alt+-")));
    connect(m_zoomOutAction, &QAction::triggered, this, [this]() {
        if (auto* doc = m_documentView->currentDocument()) doc->zoomOut();
    });

    m_zoomActualAction = viewMenu->addAction(tr("&Actual Size"));
    m_zoomActualAction->setShortcut(QKeySequence(tr("Ctrl+Alt+0")));
    connect(m_zoomActualAction, &QAction::triggered, this, [this]() {
        if (auto* doc = m_documentView->currentDocument()) doc->zoomActual();
    });

    m_zoomFitAction = viewMenu->addAction(tr("&Fit to Width"));
    m_zoomFitAction->setShortcut(QKeySequence(tr("Ctrl+Alt+9")));
    connect(m_zoomFitAction, &QAction::triggered, this, [this]() {
        if (auto* doc = m_documentView->currentDocument()) doc->zoomFitWidth();
    });
}

void MainWindow::onCurrentDocumentChanged(IDocument* doc) {
    m_sidebar->setDocument(doc);

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

    const bool hasModes = doc && doc->supportsViewModes();
    m_singlePageAction->setEnabled(hasModes);
    m_continuousAction->setEnabled(hasModes);
    // m_twoPagesAction stays disabled pending implementation.

    syncViewModeActions(doc);
}

void MainWindow::syncViewModeActions(IDocument* doc) {
    if (!doc || !doc->supportsViewModes()) {
        m_singlePageAction->setChecked(false);
        m_twoPagesAction->setChecked(false);
        m_continuousAction->setChecked(false);
        return;
    }
    switch (doc->viewMode()) {
        case ViewMode::SinglePage: m_singlePageAction->setChecked(true); break;
        case ViewMode::TwoPages:   m_twoPagesAction->setChecked(true);   break;
        case ViewMode::Continuous: m_continuousAction->setChecked(true); break;
    }
}

void MainWindow::rebuildRecentMenu() {
    if (!m_recentMenu) {
        return;
    }
    m_recentMenu->clear();

    const auto entries = m_app->recentFiles().entries();
    if (entries.isEmpty()) {
        auto* empty = m_recentMenu->addAction(tr("(Empty)"));
        empty->setEnabled(false);
        return;
    }

    for (const RecentEntry& entry : entries) {
        auto* action = m_recentMenu->addAction(entry.displayName);
        action->setToolTip(entry.path);
        const QString path = entry.path;
        connect(action, &QAction::triggered, this, [this, path]() {
            m_app->openFiles({path});
        });
    }

    m_recentMenu->addSeparator();
    auto* clear = m_recentMenu->addAction(tr("Clear Menu"));
    connect(clear, &QAction::triggered, this, [this]() {
        m_app->clearRecent();
    });
}

void MainWindow::onOpen() {
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("Open"), QString(), tr("All files (*)"));
    if (!paths.isEmpty()) {
        m_app->openFiles(paths);
    }
}

void MainWindow::onAbout() {
    QMessageBox::about(this, tr("About Trailer"),
        tr("<h3>Trailer</h3>"
           "<p>Cross-platform PDF and image workbench.</p>"
           "<p>Version 0.1.0 (Phase 1)</p>"));
}

void MainWindow::addDocument(std::unique_ptr<IDocument> document) {
    if (!document) {
        return;
    }
    const QString title = document->displayName();
    m_documentView->addDocument(std::move(document));
    setWindowTitle(title.isEmpty() ? tr("Trailer") : tr("%1 — Trailer").arg(title));
}

int MainWindow::documentCount() const {
    return m_documentView->documentCount();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event) {
    QStringList paths;
    for (const QUrl& url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            paths.append(url.toLocalFile());
        }
    }
    if (!paths.isEmpty()) {
        m_app->openFiles(paths);
        event->acceptProposedAction();
    }
}

}  // namespace trailer
