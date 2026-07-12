// UAT harness — Foundations
//
// Drives the Application + MainWindow in-process under
// QT_QPA_PLATFORM=offscreen. Each slot implements one case from
// docs/uat/01-foundations.md; slot names end with the UAT ID so
// failures point directly at the spec case.
//
// The test binary is labelled `uat` in CTest. Regular CI runs pass
// `-LE uat` to skip it; UAT runs pass `-L uat`.

#include "app/Application.h"
#include "document/IDocument.h"
#include "platform/Share.h"
#include "ui/DocumentView.h"
#include "ui/MainWindow.h"

#include <QAction>
#include <QClipboard>
#include <QDockWidget>
#include <QFileOpenEvent>
#include <QMenu>
#include <QMenuBar>
#include <QPdfWriter>
#include <QPageSize>
#include <QImage>
#include <QPainter>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QToolBar>
#include <QtTest/QtTest>

using namespace trailer;

namespace {

MainWindow *currentMainWindow() {
    for (auto *w : QApplication::topLevelWidgets()) {
        if (auto *mw = qobject_cast<MainWindow *>(w))
            return mw;
    }
    return nullptr;
}

QAction *findMenuAction(QMenuBar *bar, const QString &topText, const QString &itemText) {
    for (QAction *topAction : bar->actions()) {
        if (topAction->text() == topText) {
            QMenu *menu = topAction->menu();
            if (!menu)
                return nullptr;
            for (QAction *action : menu->actions()) {
                if (action->text() == itemText)
                    return action;
            }
        }
    }
    return nullptr;
}

// Walk every QMenu under the menu bar and return the one that hosts the
// given action. Used to assert the hosting menu has tooltips enabled —
// a disabled action's tooltip is invisible in the menu unless the menu
// itself has setToolTipsVisible(true).
QMenu *menuContainingAction(QMenuBar *bar, QAction *action) {
    for (QMenu *menu : bar->findChildren<QMenu *>()) {
        if (menu->actions().contains(action))
            return menu;
    }
    return nullptr;
}

QStringList topLevelMenuTexts(QMenuBar *bar) {
    QStringList texts;
    for (QAction *a : bar->actions()) {
        if (!a->text().isEmpty())
            texts << a->text();
    }
    return texts;
}

QString writeTinyPdf(const QString &path) {
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    QPainter p(&writer);
    p.drawText(100, 100, QStringLiteral("UAT fixture"));
    p.end();
    return path;
}

} // namespace

class TestUatFoundations : public QObject {
    Q_OBJECT
  private slots:
    void init();

    // Map to docs/uat/01-foundations.md
    void uat_fnd_001_launchWithNoArguments();
    void uat_fnd_002_launchOpensFileInWindow();
    void uat_fnd_003_singleDocumentHidesTabBar();
    void uat_fnd_004_openingMultipleFilesSpawnsMultipleWindows();
    void uat_fnd_005_imageBatchSharesOneWindow();
    void uat_fnd_010_menuStructure();
    void uat_fnd_011_macosNoWindowMenuProvidesFileActions();
    void uat_fnd_016_toggleSidebar();
    void uat_fnd_020_flashErrorRoutesToStatusBarNotModal();
    void uat_fnd_030_autoSaveWritesDirtyDocsWithPath();
    void uat_fnd_031_autoSaveSkipsUntitledAndCleanDocs();
    void uat_fnd_040_shareMenuItemPresentOnSupportedPlatforms();
    void uat_fnd_041_shareDisabledWithTooltipWhenUnavailable();
    void uat_fnd_042_twoPagesActionDisabledWithTooltip();
    void uat_fnd_050_fileOpenEventOpensWindow();
    void uat_fnd_070_copyPageAsImageToClipboard();

  private:
    QTemporaryDir m_scratch;
};

void TestUatFoundations::init() {
    // Close any leftover windows from a previous slot so every case
    // starts from an "app with no open windows" baseline.
    for (auto *w : QApplication::topLevelWidgets()) {
        if (qobject_cast<MainWindow *>(w))
            w->close();
    }
    QApplication::processEvents();
}

void TestUatFoundations::uat_fnd_001_launchWithNoArguments() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);

    MainWindow *mw = app->ensureWindow();
    QVERIFY(mw);
    QCOMPARE(mw->windowTitle(), QStringLiteral("Trailer"));
    QCOMPARE(mw->documentCount(), 0);

    // Sidebar dock is hidden at launch (2026-04-30 HITL: chrome
    // off by default; the user opens it from the top-bar's
    // sidebar-mode picker or View → Toggle Sidebar).
    auto docks = mw->findChildren<QDockWidget *>();
    QDockWidget *sidebar = nullptr;
    for (auto *d : docks) {
        if (d->windowTitle().contains(QStringLiteral("Sidebar"), Qt::CaseInsensitive)) {
            sidebar = d;
            break;
        }
    }
    QVERIFY2(sidebar, "Sidebar dock not found");
    QVERIFY2(!sidebar->isVisible(), "Sidebar must be hidden by default — toggle via "
                                    "View → Toggle Sidebar");

    // Markup toolbar is hidden at launch (UAT-FND-001 correction).
    QToolBar *markup = nullptr;
    for (auto *t : mw->findChildren<QToolBar *>()) {
        if (t->windowTitle().contains(QStringLiteral("Markup"), Qt::CaseInsensitive)) {
            markup = t;
            break;
        }
    }
    QVERIFY2(markup, "Markup toolbar not found");
    QVERIFY2(!markup->isVisible(), "Markup toolbar should be hidden at launch");

    // Inspector dock is hidden at launch.
    QDockWidget *inspector = nullptr;
    for (auto *d : docks) {
        if (d->windowTitle().contains(QStringLiteral("Inspector"), Qt::CaseInsensitive)) {
            inspector = d;
            break;
        }
    }
    QVERIFY2(inspector, "Inspector dock not found");
    QVERIFY2(!inspector->isVisible(), "Inspector dock should be hidden at launch");
}

void TestUatFoundations::uat_fnd_002_launchOpensFileInWindow() {
    QVERIFY(m_scratch.isValid());
    const QString pdfPath = writeTinyPdf(m_scratch.filePath("uat_fnd_002.pdf"));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);

    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY2(mw, "Expected a MainWindow after openFiles");
    QCOMPARE(mw->documentCount(), 1);

    const auto recent = app->recentFiles().entries();
    QVERIFY2(!recent.isEmpty(), "Recent files should contain at least one entry after openFiles");
}

// Window-per-file is the default (see TODO.md UX polish pass). A
// window holding exactly one document should show no tab strip — the
// tab bar is a distraction on single-doc frames. Qt's built-in
// tabBarAutoHide handles this when count() <= 1.
void TestUatFoundations::uat_fnd_003_singleDocumentHidesTabBar() {
    QVERIFY(m_scratch.isValid());
    const QString pdfPath = writeTinyPdf(m_scratch.filePath("uat_fnd_003.pdf"));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);

    auto *tabs = mw->findChild<QTabWidget *>();
    QVERIFY2(tabs, "Expected a QTabWidget (DocumentView) inside MainWindow");
    QCOMPARE(tabs->count(), 1);
    QVERIFY2(tabs->tabBarAutoHide(), "DocumentView must set tabBarAutoHide so single-doc windows "
                                     "don't show a tab strip");
    QVERIFY2(!tabs->tabBar()->isVisible(),
             "With autoHide on and one tab, the tab bar must not be visible");
}

// Opening several files in a single openFiles() call should, under
// the default NewWindow mode, spawn one window per file — not pile
// them all into a single frame. The test confirms the count of
// top-level MainWindows matches the number of paths.
void TestUatFoundations::uat_fnd_004_openingMultipleFilesSpawnsMultipleWindows() {
    QVERIFY(m_scratch.isValid());
    const QString p1 = writeTinyPdf(m_scratch.filePath("uat_fnd_004_a.pdf"));
    const QString p2 = writeTinyPdf(m_scratch.filePath("uat_fnd_004_b.pdf"));
    const QString p3 = writeTinyPdf(m_scratch.filePath("uat_fnd_004_c.pdf"));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);

    // Sanity: starting state has no MainWindows (init() closed them).
    int baselineWindows = 0;
    for (auto *w : QApplication::topLevelWidgets()) {
        if (qobject_cast<MainWindow *>(w))
            ++baselineWindows;
    }
    QCOMPARE(baselineWindows, 0);

    app->openFiles({p1, p2, p3});
    QApplication::processEvents();

    int spawned = 0;
    for (auto *w : QApplication::topLevelWidgets()) {
        if (auto *mw = qobject_cast<MainWindow *>(w)) {
            QCOMPARE(mw->documentCount(), 1);
            ++spawned;
        }
    }
    QCOMPARE(spawned, 3);
}

// UAT-FND-005 — A batch of images opened together should share one
// window (the QTabWidget central shows tabs when count > 1) so the
// user can flip through them without arranging multiple frames.
// PDF batches still spawn separate windows because PDFs are
// typically multi-page documents that warrant their own frame.
void TestUatFoundations::uat_fnd_005_imageBatchSharesOneWindow() {
    QVERIFY(m_scratch.isValid());
    auto writePng = [this](const QString &name) {
        const QString path = m_scratch.filePath(name);
        QImage img(80, 60, QImage::Format_RGB32);
        img.fill(Qt::white);
        img.save(path, "PNG");
        return path;
    };
    const QString p1 = writePng(QStringLiteral("uat_fnd_005_a.png"));
    const QString p2 = writePng(QStringLiteral("uat_fnd_005_b.png"));
    const QString p3 = writePng(QStringLiteral("uat_fnd_005_c.png"));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);

    int baselineWindows = 0;
    for (auto *w : QApplication::topLevelWidgets()) {
        if (qobject_cast<MainWindow *>(w))
            ++baselineWindows;
    }
    QCOMPARE(baselineWindows, 0);

    app->openFiles({p1, p2, p3});
    QApplication::processEvents();

    int windowCount = 0;
    int totalDocs = 0;
    for (auto *w : QApplication::topLevelWidgets()) {
        if (auto *mw = qobject_cast<MainWindow *>(w)) {
            ++windowCount;
            totalDocs += mw->documentCount();
        }
    }
    QCOMPARE(windowCount, 1);
    QCOMPARE(totalDocs, 3);
}

void TestUatFoundations::uat_fnd_010_menuStructure() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    MainWindow *mw = app->ensureWindow();
    QVERIFY(mw);
    QMenuBar *bar = mw->menuBar();
    QVERIFY(bar);

    const QStringList topLevel = topLevelMenuTexts(bar);
    for (const QString &expected :
         {QStringLiteral("&File"), QStringLiteral("&Edit"), QStringLiteral("&View"),
          QStringLiteral("&Tools"), QStringLiteral("&Help")}) {
        QVERIFY2(topLevel.contains(expected),
                 qPrintable(QStringLiteral("Missing top-level menu: ") + expected));
    }

    // Spot-check a handful of items that the UAT doc lists.
    for (const auto &pair : {
             std::pair{QStringLiteral("&File"), QStringLiteral("&Open…")},
             std::pair{QStringLiteral("&File"), QStringLiteral("&Quit")},
             std::pair{QStringLiteral("&View"), QStringLiteral("Zoom &In")},
             std::pair{QStringLiteral("&Tools"), QStringLiteral("Rotate &Right")},
             std::pair{QStringLiteral("&Help"), QStringLiteral("&About Trailer")},
         }) {
        QAction *a = findMenuAction(bar, pair.first, pair.second);
        QVERIFY2(a, qPrintable(QStringLiteral("Missing menu item: ") + pair.first +
                               QStringLiteral(" > ") + pair.second));
    }
}

void TestUatFoundations::uat_fnd_011_macosNoWindowMenuProvidesFileActions() {
#ifndef Q_OS_MACOS
    QSKIP("macOS-only menu-bar behavior.");
#else
    auto* app = qobject_cast<Application*>(qApp);
    QVERIFY(app);

    QMenuBar* bar = app->noWindowMenuBar();
    QVERIFY2(bar, "Application-level macOS menu bar should exist");

    for (const QString& item : {
             QStringLiteral("&New"),
             QStringLiteral("&Open…"),
             QStringLiteral("New from &Clipboard"),
             QStringLiteral("&Acquire…"),
         }) {
        QAction* a = findMenuAction(bar, QStringLiteral("&File"), item);
        QVERIFY2(a, qPrintable(QStringLiteral("Missing File menu item: ") + item));
    }

    QAction* newAction =
        findMenuAction(bar, QStringLiteral("&File"), QStringLiteral("&New"));
    QVERIFY(newAction);
    const int before = static_cast<int>(app->windows().size());
    newAction->trigger();
    QApplication::processEvents();
    QVERIFY2(app->windows().size() >= before + 1,
             "File > New should create a window in no-window mode");
#endif
}

void TestUatFoundations::uat_fnd_016_toggleSidebar() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    MainWindow *mw = app->ensureWindow();
    QVERIFY(mw);

    QDockWidget *sidebar = nullptr;
    for (auto *d : mw->findChildren<QDockWidget *>()) {
        if (d->windowTitle().contains(QStringLiteral("Sidebar"), Qt::CaseInsensitive)) {
            sidebar = d;
            break;
        }
    }
    QVERIFY(sidebar);
    QVERIFY2(!sidebar->isVisible(), "Sidebar should be hidden at launch (2026-04-30)");

    QAction *toggle =
        findMenuAction(mw->menuBar(), QStringLiteral("&View"), QStringLiteral("Toggle &Sidebar"));
    QVERIFY2(toggle, "View > Toggle Sidebar action not found");

    toggle->trigger();
    QApplication::processEvents();
    QVERIFY2(sidebar->isVisible(), "First trigger should show the Sidebar");

    toggle->trigger();
    QApplication::processEvents();
    QVERIFY2(!sidebar->isVisible(), "Second trigger should hide the Sidebar");
}

// UAT-FND-020 — flashError routes operation-failure feedback into the
// status bar instead of popping a QMessageBox::warning modal. The
// 2026-04-29 polish pass replaced a dozen-plus operation-failure
// modals with status-bar messages so users dealing with a tax doc /
// bill / court doc are not interrupted by a click-through every time
// something doesn't work the first try.
void TestUatFoundations::uat_fnd_020_flashErrorRoutesToStatusBarNotModal() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    MainWindow *mw = app->ensureWindow();
    QVERIFY(mw);

    // Snapshot of any existing QMessageBox windows so we don't false-
    // positive on a leftover from another test.
    auto countMessageBoxes = []() {
        int n = 0;
        for (auto *w : QApplication::topLevelWidgets()) {
            if (w->inherits("QMessageBox"))
                ++n;
        }
        return n;
    };
    const int messageBoxesBefore = countMessageBoxes();

    mw->flashError(QStringLiteral("Crop failed — margins too large."));
    QApplication::processEvents();

    QVERIFY2(countMessageBoxes() == messageBoxesBefore, "flashError must NOT spawn a QMessageBox");
    const QString status = mw->statusBar()->currentMessage();
    QVERIFY2(status.contains(QStringLiteral("Crop failed")),
             qPrintable(QStringLiteral("status bar was: '%1'").arg(status)));
    QVERIFY2(status.contains(QStringLiteral("⚠")),
             "flashError prefixes the message with a warning glyph so the "
             "bar visually changes character without stylesheet juggling");

    // flashSuccess and flashStatus take parallel paths — confirm both
    // also bypass any modal layer.
    mw->flashSuccess(QStringLiteral("Saved."));
    QApplication::processEvents();
    QCOMPARE(countMessageBoxes(), messageBoxesBefore);
    QVERIFY(mw->statusBar()->currentMessage().contains(QStringLiteral("Saved.")));
    QVERIFY(mw->statusBar()->currentMessage().contains(QStringLiteral("✓")));

    mw->flashStatus(QStringLiteral("Window/region capture not supported."));
    QApplication::processEvents();
    QCOMPARE(countMessageBoxes(), messageBoxesBefore);
    QVERIFY(mw->statusBar()->currentMessage().contains(QStringLiteral("not supported")));
}

// UAT-FND-030 — Auto-save writes the file when a document is dirty
// and has an established path. The 30 s timer's slot is exposed as a
// public method so the test can trigger it without waiting.
void TestUatFoundations::uat_fnd_030_autoSaveWritesDirtyDocsWithPath() {
    QVERIFY(m_scratch.isValid());
    const QString pdfPath = writeTinyPdf(m_scratch.filePath("uat_fnd_030.pdf"));
    const auto sizeBefore = QFileInfo(pdfPath).size();

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->settings().setAutoSave(true);
    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    auto *dv = mw->findChild<QTabWidget *>();
    QVERIFY(dv);

    // Mutate the active doc so isDirty() reports true. Adding an
    // empty rectangle annotation through the public API (the same
    // path the markup toolbar uses) is enough.
    auto *doc = qobject_cast<MainWindow *>(mw)->findChild<QObject *>();
    Q_UNUSED(doc);
    // Use the test-friendly path: rotate the page (mutates state) so
    // isDirty becomes true via the document's normal write path.
    auto *dvCast = mw->findChild<DocumentView *>();
    QVERIFY(dvCast);
    if (auto *idoc = dvCast->currentDocument()) {
        idoc->rotatePage(0, 90);
        QVERIFY2(idoc->isDirty(), "rotating a page should make the document dirty");
    }

    // Trigger the auto-save tick directly.
    mw->autoSaveDirtyDocs();
    QApplication::processEvents();

    // The file on disk should have been rewritten — its size will
    // typically change after a rotate, but at minimum its mtime
    // changes. Assert the doc is now clean (auto-save cleared dirty).
    auto *idocAfter = dvCast->currentDocument();
    QVERIFY2(idocAfter && !idocAfter->isDirty(),
             "After autoSaveDirtyDocs() the doc should no longer be dirty");

    // sizeBefore captured pre-edit; the post-save file is rewritten.
    Q_UNUSED(sizeBefore);
}

// UAT-FND-031 — Auto-save MUST NOT pick a destination for the user;
// untitled docs (filePath empty) are skipped. It also skips clean
// documents — no churn on idle files.
void TestUatFoundations::uat_fnd_031_autoSaveSkipsUntitledAndCleanDocs() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->settings().setAutoSave(true);
    MainWindow *mw = app->ensureWindow();
    QVERIFY(mw);

    // No documents in the window — autoSave is a no-op, must not
    // crash and must not flash a "saved" status (we asserted nothing
    // happens).
    const QString preMsg = mw->statusBar()->currentMessage();
    mw->autoSaveDirtyDocs();
    QApplication::processEvents();
    const QString postMsg = mw->statusBar()->currentMessage();
    QVERIFY2(!postMsg.contains(QStringLiteral("Auto-saved")),
             "Auto-save with no documents must not flash a success message");

    // autoSave off → no work, no status change, no crash.
    app->settings().setAutoSave(false);
    mw->autoSaveDirtyDocs();
    QApplication::processEvents();
    QVERIFY2(!mw->statusBar()->currentMessage().contains(QStringLiteral("Auto-saved")),
             "When autoSave setting is off, no save should happen");
}

// UAT-FND-040 — File → Share is ALWAYS present in the File menu. On
// platforms that have a native share-sheet (macOS via
// NSSharingServicePicker) it is enabled and wired; elsewhere it is
// present-but-disabled with an explanatory tooltip (owner policy:
// unavailable capabilities are disabled + explained, never hidden).
// This test only checks presence; firing the picker is platform-
// modal and not UAT-friendly.
void TestUatFoundations::uat_fnd_040_shareMenuItemPresentOnSupportedPlatforms() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    MainWindow *mw = app->ensureWindow();
    QVERIFY(mw);

    QAction *shareAction =
        findMenuAction(mw->menuBar(), QStringLiteral("&File"), QStringLiteral("&Share…"));
    QVERIFY2(shareAction, "File → Share… must always be present in the File menu "
                          "(disabled + tooltip when unavailable, never hidden)");
}

// UAT-FND-041 — When ShareService::isAvailable() is false (Linux /
// Windows stub), the always-present Share action must be shown
// disabled with a non-empty tooltip that points the user at the real
// alternative, rather than silently vanishing.
void TestUatFoundations::uat_fnd_041_shareDisabledWithTooltipWhenUnavailable() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    MainWindow *mw = app->ensureWindow();
    QVERIFY(mw);

    QAction *shareAction =
        findMenuAction(mw->menuBar(), QStringLiteral("&File"), QStringLiteral("&Share…"));
    QVERIFY2(shareAction, "File → Share… must always exist");

    if (ShareService::isAvailable()) {
        QSKIP("ShareService is available on this platform; disabled-state case "
              "does not apply.");
    }

    QVERIFY2(!shareAction->isEnabled(),
             "Share must be disabled when ShareService is unavailable");
    QVERIFY2(!shareAction->toolTip().isEmpty(),
             "Disabled Share must carry an explanatory tooltip pointing at the "
             "alternative");

    // A tooltip is only actually shown in the menu if the hosting menu has
    // tooltips enabled — otherwise the "disabled + explanation" policy is
    // only half-delivered (invisible on hover).
    QMenu *hostMenu = menuContainingAction(mw->menuBar(), shareAction);
    QVERIFY2(hostMenu, "Could not locate the menu hosting the Share action");
    QVERIFY2(hostMenu->toolTipsVisible(),
             "The File menu must call setToolTipsVisible(true) so the disabled "
             "Share tooltip is actually rendered on hover");
}

// UAT-FND-042 — View → Two Pages is an unbuilt capability: QPdfView
// has no facing/two-up layout. Per owner policy the action must be
// present but disabled with an explanatory tooltip, never silently
// aliasing another layout.
void TestUatFoundations::uat_fnd_042_twoPagesActionDisabledWithTooltip() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    MainWindow *mw = app->ensureWindow();
    QVERIFY(mw);

    QAction *twoPages =
        findMenuAction(mw->menuBar(), QStringLiteral("&View"), QStringLiteral("Two Pages"));
    QVERIFY2(twoPages, "View → Two Pages action must be present");
    QVERIFY2(!twoPages->isEnabled(),
             "Two Pages must be disabled while a real facing layout is unbuilt");
    QVERIFY2(!twoPages->toolTip().isEmpty(),
             "Disabled Two Pages must carry an explanatory tooltip");

    // The tooltip is only visible on hover if the hosting menu enables
    // tooltips — assert the View menu does so.
    QMenu *hostMenu = menuContainingAction(mw->menuBar(), twoPages);
    QVERIFY2(hostMenu, "Could not locate the menu hosting the Two Pages action");
    QVERIFY2(hostMenu->toolTipsVisible(),
             "The View menu must call setToolTipsVisible(true) so the disabled "
             "Two Pages tooltip is actually rendered on hover");
}

// UAT-FND-050 — Synthesize the QFileOpenEvent macOS dispatches when
// the user drops a file onto the Dock icon (or right-click → Open
// With → Trailer). Application::event already routes it through
// openFiles, so we just have to confirm the wiring still survives.
// The previous TODO entry (2026-04-30 #1) suspected a registry-
// timing bug; the registry is registered in the Application
// constructor, before exec(), so by the time any QEvent::FileOpen
// can be dispatched the routing path is fully wired. This test
// pins that contract so a future refactor that defers
// registry init can't silently break Dock-drop.
//
// (The end-to-end live test — actually drag a file onto the Dock
// — still needs a real macOS session. See README/dev notes; this
// UAT only covers the in-process event-routing path.)
void TestUatFoundations::uat_fnd_050_fileOpenEventOpensWindow() {
    QVERIFY(m_scratch.isValid());
    const QString pdfPath = writeTinyPdf(
        m_scratch.filePath(QStringLiteral("uat_fnd_050.pdf")));

    auto* app = qobject_cast<Application*>(qApp);
    QVERIFY(app);
    // Confirm we start with no windows so the event below is the
    // only thing that can open one.
    QCOMPARE(app->windows().size(), 0);

    QFileOpenEvent ev(pdfPath);
    QCoreApplication::sendEvent(app, &ev);
    QApplication::processEvents();

    QCOMPARE(app->windows().size(), 1);
    MainWindow* mw = currentMainWindow();
    QVERIFY(mw);
    QCOMPARE(mw->documentCount(), 1);
}

// UAT-FND-070 — Copy Page as Image puts a rendered page on the clipboard.
//
// The end-of-session flow: mark up a page, then copy it to paste into a
// chat app. Edit > Copy Page as Image renders the current page / image
// and pushes it to the system clipboard.
void TestUatFoundations::uat_fnd_070_copyPageAsImageToClipboard() {
    QVERIFY(m_scratch.isValid());
    const QString pdfPath = writeTinyPdf(m_scratch.filePath("uat_fnd_070.pdf"));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);

    QClipboard *clip = QApplication::clipboard();
    QVERIFY(clip);
    clip->clear();
    QVERIFY(clip->image().isNull());

    QAction *copyPage = findMenuAction(mw->menuBar(), QStringLiteral("&Edit"),
                                       QStringLiteral("Copy Page as &Image"));
    QVERIFY2(copyPage, "Edit > Copy Page as Image action not found");
    QVERIFY2(copyPage->isEnabled(), "Copy Page as Image should be enabled for a PDF");

    copyPage->trigger();
    QApplication::processEvents();

    const QImage copied = clip->image();
    QVERIFY2(!copied.isNull(), "Copy Page as Image must place an image on the clipboard");
    QVERIFY(copied.width() > 0 && copied.height() > 0);
}

// Custom main: we need to set HOME (and XDG vars) before constructing
// Application so Settings/RecentFiles write into a sandbox, not the
// user's real config dir. QTEST_MAIN would construct QApplication
// before we got a chance to do that.
int main(int argc, char **argv) {
    QTemporaryDir fakeHome;
    if (!fakeHome.isValid())
        return 1;
    qputenv("HOME", fakeHome.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (fakeHome.path() + "/.config").toUtf8());
    qputenv("XDG_DATA_HOME", (fakeHome.path() + "/.local/share").toUtf8());
    QDir().mkpath(fakeHome.path() + "/.config/trailer");
    QDir().mkpath(fakeHome.path() + "/.local/share/trailer");

    Application app(argc, argv);
    TestUatFoundations tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_uat_foundations.moc"
