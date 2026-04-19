#include "MainWindow.h"

#include "AnimationBar.h"
#include "DocumentView.h"
#include "Magnifier.h"
#include "SearchBar.h"
#include "Sidebar.h"
#include "app/Application.h"
#include "recent/RecentFiles.h"

#include <QAction>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGuiApplication>
#include <QImageWriter>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPixmap>
#include <QScreen>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QVBoxLayout>
#include <QWidget>
#include "document/ImageAdapter.h"

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

    m_animationBar = new AnimationBar(center);
    m_animationBar->hide();

    centerLayout->addWidget(m_searchBar);
    centerLayout->addWidget(m_documentView, 1);
    centerLayout->addWidget(m_animationBar);
    setCentralWidget(center);

    m_sidebar = new Sidebar(this);
    addDockWidget(Qt::LeftDockWidgetArea, m_sidebar);
    connect(m_sidebar, &Sidebar::deletePagesRequested,
            this, [this](const std::vector<int>& rows) {
                auto* doc = m_documentView->currentDocument();
                if (!doc || !doc->supportsEditing()) return;
                doc->deletePages(rows);
                m_sidebar->refreshThumbnails();
                onCurrentDocumentChanged(doc);
            });
    connect(m_sidebar, &Sidebar::movePageRequested,
            this, [this](int from, int to) {
                auto* doc = m_documentView->currentDocument();
                if (!doc || !doc->supportsEditing()) return;
                doc->movePage(from, to);
                m_sidebar->refreshThumbnails();
                onCurrentDocumentChanged(doc);
            });

    m_magnifier = new Magnifier(this);

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

    m_saveAction = fileMenu->addAction(tr("&Save"));
    m_saveAction->setShortcut(QKeySequence::Save);
    connect(m_saveAction, &QAction::triggered, this, &MainWindow::onSave);

    m_saveAsAction = fileMenu->addAction(tr("Save &As…"));
    m_saveAsAction->setShortcut(QKeySequence::SaveAs);
    connect(m_saveAsAction, &QAction::triggered, this, &MainWindow::onSaveAs);
    fileMenu->addSeparator();

    m_printAction = fileMenu->addAction(tr("&Print…"));
    m_printAction->setShortcut(QKeySequence::Print);
    connect(m_printAction, &QAction::triggered, this, [this]() {
        if (auto* doc = m_documentView->currentDocument()) {
            doc->print(this);
        }
    });
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

    auto* toolsMenu = menuBar()->addMenu(tr("&Tools"));
    buildToolsMenu(toolsMenu);

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

    m_previousPageAction = viewMenu->addAction(tr("&Previous Page"));
    m_previousPageAction->setShortcut(QKeySequence(Qt::Key_PageUp));
    connect(m_previousPageAction, &QAction::triggered, this, [this]() {
        if (auto* doc = m_documentView->currentDocument()) {
            doc->goToPage(doc->currentPage() - 1);
        }
    });

    m_nextPageAction = viewMenu->addAction(tr("&Next Page"));
    m_nextPageAction->setShortcut(QKeySequence(Qt::Key_PageDown));
    connect(m_nextPageAction, &QAction::triggered, this, [this]() {
        if (auto* doc = m_documentView->currentDocument()) {
            doc->goToPage(doc->currentPage() + 1);
        }
    });

    viewMenu->addSeparator();

    m_zoomInAction = viewMenu->addAction(tr("Zoom &In"));
    m_zoomInAction->setShortcuts({
        QKeySequence::ZoomIn,
        QKeySequence(Qt::CTRL | Qt::Key_Equal),
    });
    connect(m_zoomInAction, &QAction::triggered, this, [this]() {
        if (auto* doc = m_documentView->currentDocument()) doc->zoomIn();
    });

    m_zoomOutAction = viewMenu->addAction(tr("Zoom &Out"));
    m_zoomOutAction->setShortcut(QKeySequence::ZoomOut);
    connect(m_zoomOutAction, &QAction::triggered, this, [this]() {
        if (auto* doc = m_documentView->currentDocument()) doc->zoomOut();
    });

    m_zoomActualAction = viewMenu->addAction(tr("&Actual Size"));
    m_zoomActualAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    connect(m_zoomActualAction, &QAction::triggered, this, [this]() {
        if (auto* doc = m_documentView->currentDocument()) doc->zoomActual();
    });

    m_zoomFitAction = viewMenu->addAction(tr("&Fit to Width"));
    m_zoomFitAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_1));
    connect(m_zoomFitAction, &QAction::triggered, this, [this]() {
        if (auto* doc = m_documentView->currentDocument()) doc->zoomFitWidth();
    });

    viewMenu->addSeparator();

    m_magnifierAction = viewMenu->addAction(tr("&Magnifier"));
    m_magnifierAction->setCheckable(true);
    m_magnifierAction->setShortcut(QKeySequence(Qt::Key_QuoteLeft));
    connect(m_magnifierAction, &QAction::toggled, this, [this](bool on) {
        if (on) {
            m_magnifier->setTarget(m_documentView->currentWidget());
            m_magnifier->activate();
        } else {
            m_magnifier->deactivate();
        }
    });
}

void MainWindow::buildToolsMenu(QMenu* toolsMenu) {
    m_rotateLeftAction = toolsMenu->addAction(tr("Rotate &Left"));
    m_rotateLeftAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_L));
    connect(m_rotateLeftAction, &QAction::triggered, this, &MainWindow::onRotateLeft);

    m_rotateRightAction = toolsMenu->addAction(tr("Rotate &Right"));
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

    toolsMenu->addSeparator();

    m_exportAsAction = toolsMenu->addAction(tr("&Export As…"));
    connect(m_exportAsAction, &QAction::triggered, this, &MainWindow::onExportAs);

    m_insertPagesAction = toolsMenu->addAction(tr("&Insert Pages from File…"));
    connect(m_insertPagesAction, &QAction::triggered, this, &MainWindow::onInsertPages);

    m_cropPagesAction = toolsMenu->addAction(tr("&Crop Pages…"));
    connect(m_cropPagesAction, &QAction::triggered, this, &MainWindow::onCropPages);

    toolsMenu->addSeparator();

    m_screenshotAction = toolsMenu->addAction(tr("&Take Screenshot"));
    m_screenshotAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_3));
    connect(m_screenshotAction, &QAction::triggered, this, &MainWindow::onTakeScreenshot);
}

void MainWindow::onCropPages() {
    auto* doc = m_documentView->currentDocument();
    if (!doc || !doc->supportsEditing()) return;

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Crop Pages"));
    auto* form = new QFormLayout(&dialog);

    auto makeSpin = [&]() {
        auto* s = new QDoubleSpinBox(&dialog);
        s->setRange(0.0, 500.0);
        s->setDecimals(1);
        s->setSuffix(QStringLiteral(" mm"));
        return s;
    };
    auto* leftSpin = makeSpin();
    auto* topSpin = makeSpin();
    auto* rightSpin = makeSpin();
    auto* bottomSpin = makeSpin();
    form->addRow(tr("Left margin"), leftSpin);
    form->addRow(tr("Top margin"), topSpin);
    form->addRow(tr("Right margin"), rightSpin);
    form->addRow(tr("Bottom margin"), bottomSpin);

    auto* allPagesCheck = new QCheckBox(tr("Apply to all pages"), &dialog);
    allPagesCheck->setChecked(true);
    form->addRow(allPagesCheck);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);

    if (dialog.exec() != QDialog::Accepted) return;

    constexpr double kMmToPt = 72.0 / 25.4;
    const double l = leftSpin->value() * kMmToPt;
    const double t = topSpin->value() * kMmToPt;
    const double r = rightSpin->value() * kMmToPt;
    const double b = bottomSpin->value() * kMmToPt;
    if (l == 0.0 && t == 0.0 && r == 0.0 && b == 0.0) return;

    bool anyApplied = false;
    if (allPagesCheck->isChecked()) {
        const int pages = doc->pageCount();
        std::vector<int> all;
        all.reserve(pages);
        for (int i = 0; i < pages; ++i) all.push_back(i);
        anyApplied = doc->cropPages(all, l, t, r, b);
    } else {
        anyApplied = doc->cropPage(doc->currentPage(), l, t, r, b);
    }

    if (!anyApplied) {
        QMessageBox::warning(this, tr("Crop failed"),
            tr("Could not apply crop. Margins may be too large."));
        return;
    }
    m_sidebar->refreshThumbnails();
    onCurrentDocumentChanged(doc);
}

void MainWindow::onInsertPages() {
    auto* doc = m_documentView->currentDocument();
    if (!doc || !doc->supportsEditing()) return;
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Insert Pages from File"), QString(),
        tr("PDF documents (*.pdf)"));
    if (path.isEmpty()) return;
    const int insertAt = doc->currentPage() + 1;
    if (!doc->insertPagesFrom(path, insertAt)) {
        QMessageBox::warning(this, tr("Insert failed"),
            tr("Could not insert pages from %1").arg(path));
        return;
    }
    m_sidebar->refreshThumbnails();
    onCurrentDocumentChanged(doc);
}

int MainWindow::selectedPageForEdit(IDocument* doc) const {
    if (!doc) return -1;
    const int page = doc->currentPage();
    if (page >= 0 && page < doc->pageCount()) return page;
    return -1;
}

void MainWindow::onRotateLeft() {
    auto* doc = m_documentView->currentDocument();
    if (!doc || !doc->supportsEditing()) return;
    const int page = selectedPageForEdit(doc);
    if (page < 0) return;
    doc->rotatePage(page, -90);
    m_sidebar->refreshThumbnails();
    updateTitleForDocument(doc);
}

void MainWindow::onRotateRight() {
    auto* doc = m_documentView->currentDocument();
    if (!doc || !doc->supportsEditing()) return;
    const int page = selectedPageForEdit(doc);
    if (page < 0) return;
    doc->rotatePage(page, 90);
    m_sidebar->refreshThumbnails();
    updateTitleForDocument(doc);
}

void MainWindow::onFlipHorizontal() {
    auto* doc = m_documentView->currentDocument();
    if (!doc || !doc->supportsEditing()) return;
    doc->flipHorizontal();
    m_sidebar->refreshThumbnails();
    updateTitleForDocument(doc);
}

void MainWindow::onFlipVertical() {
    auto* doc = m_documentView->currentDocument();
    if (!doc || !doc->supportsEditing()) return;
    doc->flipVertical();
    m_sidebar->refreshThumbnails();
    updateTitleForDocument(doc);
}

void MainWindow::onAdjustSize() {
    auto* doc = m_documentView->currentDocument();
    if (!doc || !doc->supportsEditing()) return;
    const QSize current = doc->imagePixelSize();
    if (current.isEmpty()) return;

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Adjust Size"));
    auto* form = new QFormLayout(&dialog);

    auto* widthSpin = new QSpinBox(&dialog);
    widthSpin->setRange(1, 32768);
    widthSpin->setValue(current.width());
    widthSpin->setSuffix(tr(" px"));

    auto* heightSpin = new QSpinBox(&dialog);
    heightSpin->setRange(1, 32768);
    heightSpin->setValue(current.height());
    heightSpin->setSuffix(tr(" px"));

    auto* aspectCheck = new QCheckBox(tr("Keep aspect ratio"), &dialog);
    aspectCheck->setChecked(true);
    auto* smoothCheck = new QCheckBox(tr("Smooth scaling"), &dialog);
    smoothCheck->setChecked(true);

    const double aspect = static_cast<double>(current.width()) /
                          static_cast<double>(current.height());
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

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);

    if (dialog.exec() != QDialog::Accepted) return;
    if (!doc->resizeImage(widthSpin->value(), heightSpin->value(),
                          smoothCheck->isChecked())) {
        QMessageBox::warning(this, tr("Resize failed"),
            tr("Could not resize this document."));
        return;
    }
    m_sidebar->refreshThumbnails();
    updateTitleForDocument(doc);
}

void MainWindow::onAdjustColour() {
    auto* doc = m_documentView->currentDocument();
    if (!doc || !doc->supportsEditing()) return;

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Adjust Colour"));
    auto* form = new QFormLayout(&dialog);

    auto makeSlider = [&]() {
        auto* s = new QSlider(Qt::Horizontal, &dialog);
        s->setRange(-100, 100);
        s->setValue(0);
        s->setTickPosition(QSlider::TicksBelow);
        s->setTickInterval(50);
        s->setMinimumWidth(240);
        return s;
    };
    auto* brightness = makeSlider();
    auto* contrast = makeSlider();
    auto* saturation = makeSlider();

    form->addRow(tr("Brightness"), brightness);
    form->addRow(tr("Contrast"), contrast);
    form->addRow(tr("Saturation"), saturation);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);

    if (dialog.exec() != QDialog::Accepted) return;
    const double b = brightness->value() / 100.0;
    const double c = contrast->value() / 100.0;
    const double s = saturation->value() / 100.0;
    if (b == 0.0 && c == 0.0 && s == 0.0) return;
    if (!doc->adjustColour(b, c, s)) {
        QMessageBox::warning(this, tr("Adjust Colour failed"),
            tr("Could not adjust colour for this document."));
        return;
    }
    m_sidebar->refreshThumbnails();
    updateTitleForDocument(doc);
}

void MainWindow::onExportAs() {
    auto* doc = m_documentView->currentDocument();
    if (!doc) return;

    QStringList filters;
    filters << tr("PNG image (*.png)")
            << tr("JPEG image (*.jpg *.jpeg)")
            << tr("TIFF image (*.tif *.tiff)")
            << tr("BMP image (*.bmp)")
            << tr("WebP image (*.webp)");
    QString selected;
    const QString suggested = doc->filePath().isEmpty()
        ? doc->displayName()
        : QFileInfo(doc->filePath()).completeBaseName() + ".png";
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export As"), suggested, filters.join(";;"), &selected);
    if (path.isEmpty()) return;
    QString format = QFileInfo(path).suffix().toLower();
    if (format.isEmpty()) format = "png";
    if (!doc->exportAs(path, format)) {
        QMessageBox::warning(this, tr("Export failed"),
            tr("Could not export to %1").arg(path));
    }
}

void MainWindow::onTakeScreenshot() {
    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) return;
    const QPixmap shot = screen->grabWindow(0);
    if (shot.isNull()) {
        QMessageBox::warning(this, tr("Screenshot failed"),
            tr("Could not capture the screen."));
        return;
    }
    const QString dir = QStandardPaths::writableLocation(
        QStandardPaths::PicturesLocation);
    const QString stamp = QDateTime::currentDateTime().toString(
        QStringLiteral("yyyyMMdd-HHmmss"));
    QTemporaryFile tmp(QDir(dir).filePath(
        QStringLiteral("trailer-screenshot-%1.XXXXXX.png").arg(stamp)));
    tmp.setAutoRemove(false);
    if (!tmp.open()) {
        QMessageBox::warning(this, tr("Screenshot failed"),
            tr("Could not write screenshot."));
        return;
    }
    const QString path = tmp.fileName();
    tmp.close();
    if (!shot.save(path, "PNG")) {
        QMessageBox::warning(this, tr("Screenshot failed"),
            tr("Could not write screenshot to %1").arg(path));
        return;
    }
    m_app->openFiles({path});
}

void MainWindow::onSave() {
    auto* doc = m_documentView->currentDocument();
    if (!doc || !doc->supportsEditing()) return;
    if (doc->filePath().isEmpty()) {
        onSaveAs();
        return;
    }
    if (!doc->save()) {
        QMessageBox::warning(this, tr("Save failed"),
            tr("Could not save to %1").arg(doc->filePath()));
        return;
    }
    updateTitleForDocument(doc);
}

void MainWindow::onSaveAs() {
    auto* doc = m_documentView->currentDocument();
    if (!doc || !doc->supportsEditing()) return;
    const bool isImage = dynamic_cast<ImageDocument*>(doc) != nullptr;
    const QString suggested = doc->filePath().isEmpty()
        ? doc->displayName()
        : doc->filePath();
    const QString filter = isImage
        ? tr("Images (*.png *.jpg *.jpeg *.bmp *.tiff *.tif *.webp)")
        : tr("PDF documents (*.pdf)");
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save As"), suggested, filter);
    if (path.isEmpty()) return;
    if (!doc->save(path)) {
        QMessageBox::warning(this, tr("Save failed"),
            tr("Could not save to %1").arg(path));
        return;
    }
    updateTitleForDocument(doc);
}

void MainWindow::updateTitleForDocument(IDocument* doc) {
    if (!doc) {
        setWindowTitle(tr("Trailer"));
        return;
    }
    const QString name = doc->displayName();
    const QString marker = doc->isDirty() ? QStringLiteral("• ") : QString();
    setWindowTitle(tr("%1%2 — Trailer").arg(marker, name));

    const int idx = m_documentView->currentIndex();
    if (idx >= 0) {
        m_documentView->setTabText(idx, marker + name);
    }
}

void MainWindow::onCurrentDocumentChanged(IDocument* doc) {
    m_sidebar->setDocument(doc);
    m_animationBar->setDocument(doc);

    const bool hasPrint = doc && doc->supportsPrint();
    m_printAction->setEnabled(hasPrint);

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

    const bool canEdit = doc && doc->supportsEditing();
    const bool isImage = dynamic_cast<ImageDocument*>(doc) != nullptr;
    const bool isPdfLike = canEdit && !isImage;
    m_saveAction->setEnabled(canEdit);
    m_saveAsAction->setEnabled(canEdit);
    m_rotateLeftAction->setEnabled(canEdit);
    m_rotateRightAction->setEnabled(canEdit);
    m_flipHorizontalAction->setEnabled(canEdit);
    m_flipVerticalAction->setEnabled(canEdit);
    m_adjustSizeAction->setEnabled(canEdit && isImage);
    m_adjustColourAction->setEnabled(canEdit && isImage);
    m_exportAsAction->setEnabled(doc != nullptr && isImage);
    m_insertPagesAction->setEnabled(isPdfLike);
    m_cropPagesAction->setEnabled(isPdfLike);

    syncViewModeActions(doc);
    updateTitleForDocument(doc);
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
    m_documentView->addDocument(std::move(document));
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
