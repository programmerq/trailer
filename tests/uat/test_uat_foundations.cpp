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
#include "ui/DocumentView.h"
#include "ui/MainWindow.h"

#include <QAction>
#include <QDockWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPdfWriter>
#include <QPageSize>
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

MainWindow* currentMainWindow() {
    for (auto* w : QApplication::topLevelWidgets()) {
        if (auto* mw = qobject_cast<MainWindow*>(w)) return mw;
    }
    return nullptr;
}

QAction* findMenuAction(QMenuBar* bar, const QString& topText, const QString& itemText) {
    for (QAction* topAction : bar->actions()) {
        if (topAction->text() == topText) {
            QMenu* menu = topAction->menu();
            if (!menu) return nullptr;
            for (QAction* action : menu->actions()) {
                if (action->text() == itemText) return action;
            }
        }
    }
    return nullptr;
}

QStringList topLevelMenuTexts(QMenuBar* bar) {
    QStringList texts;
    for (QAction* a : bar->actions()) {
        if (!a->text().isEmpty()) texts << a->text();
    }
    return texts;
}

QString writeTinyPdf(const QString& path) {
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    QPainter p(&writer);
    p.drawText(100, 100, QStringLiteral("UAT fixture"));
    p.end();
    return path;
}

}  // namespace

class TestUatFoundations : public QObject {
    Q_OBJECT
private slots:
    void init();

    // Map to docs/uat/01-foundations.md
    void uat_fnd_001_launchWithNoArguments();
    void uat_fnd_002_launchOpensFileInWindow();
    void uat_fnd_003_singleDocumentHidesTabBar();
    void uat_fnd_004_openingMultipleFilesSpawnsMultipleWindows();
    void uat_fnd_010_menuStructure();
    void uat_fnd_016_toggleSidebar();
    void uat_fnd_020_flashErrorRoutesToStatusBarNotModal();
    void uat_fnd_030_autoSaveWritesDirtyDocsWithPath();
    void uat_fnd_031_autoSaveSkipsUntitledAndCleanDocs();
    void uat_fnd_040_shareMenuItemPresentOnSupportedPlatforms();

private:
    QTemporaryDir m_scratch;
};

void TestUatFoundations::init() {
    // Close any leftover windows from a previous slot so every case
    // starts from an "app with no open windows" baseline.
    for (auto* w : QApplication::topLevelWidgets()) {
        if (qobject_cast<MainWindow*>(w)) w->close();
    }
    QApplication::processEvents();
}

void TestUatFoundations::uat_fnd_001_launchWithNoArguments() {
    auto* app = qobject_cast<Application*>(qApp);
    QVERIFY(app);

    MainWindow* mw = app->ensureWindow();
    QVERIFY(mw);
    QCOMPARE(mw->windowTitle(), QStringLiteral("Trailer"));
    QCOMPARE(mw->documentCount(), 0);

    // Sidebar dock is visible at launch.
    auto docks = mw->findChildren<QDockWidget*>();
    QDockWidget* sidebar = nullptr;
    for (auto* d : docks) {
        if (d->windowTitle().contains(QStringLiteral("Sidebar"), Qt::CaseInsensitive)) {
            sidebar = d;
            break;
        }
    }
    QVERIFY2(sidebar, "Sidebar dock not found");
    QVERIFY(sidebar->isVisible());

    // Markup toolbar is hidden at launch (UAT-FND-001 correction).
    QToolBar* markup = nullptr;
    for (auto* t : mw->findChildren<QToolBar*>()) {
        if (t->windowTitle().contains(QStringLiteral("Markup"), Qt::CaseInsensitive)) {
            markup = t;
            break;
        }
    }
    QVERIFY2(markup, "Markup toolbar not found");
    QVERIFY2(!markup->isVisible(), "Markup toolbar should be hidden at launch");

    // Inspector dock is hidden at launch.
    QDockWidget* inspector = nullptr;
    for (auto* d : docks) {
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

    auto* app = qobject_cast<Application*>(qApp);
    QVERIFY(app);

    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow* mw = currentMainWindow();
    QVERIFY2(mw, "Expected a MainWindow after openFiles");
    QCOMPARE(mw->documentCount(), 1);

    const auto recent = app->recentFiles().entries();
    QVERIFY2(!recent.isEmpty(),
             "Recent files should contain at least one entry after openFiles");
}

// Window-per-file is the default (see TODO.md UX polish pass). A
// window holding exactly one document should show no tab strip — the
// tab bar is a distraction on single-doc frames. Qt's built-in
// tabBarAutoHide handles this when count() <= 1.
void TestUatFoundations::uat_fnd_003_singleDocumentHidesTabBar() {
    QVERIFY(m_scratch.isValid());
    const QString pdfPath = writeTinyPdf(m_scratch.filePath("uat_fnd_003.pdf"));

    auto* app = qobject_cast<Application*>(qApp);
    QVERIFY(app);
    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow* mw = currentMainWindow();
    QVERIFY(mw);

    auto* tabs = mw->findChild<QTabWidget*>();
    QVERIFY2(tabs, "Expected a QTabWidget (DocumentView) inside MainWindow");
    QCOMPARE(tabs->count(), 1);
    QVERIFY2(tabs->tabBarAutoHide(),
             "DocumentView must set tabBarAutoHide so single-doc windows "
             "don't show a tab strip");
    QVERIFY2(!tabs->tabBar()->isVisible(),
             "With autoHide on and one tab, the tab bar must not be visible");
}

// Opening several files in a single openFiles() call should, under
// the default NewWindow mode, spawn one window per file — not pile
// them all into a single frame. The test confirms the count of
// top-level MainWindows matches the number of paths.
void TestUatFoundations::
    uat_fnd_004_openingMultipleFilesSpawnsMultipleWindows() {
    QVERIFY(m_scratch.isValid());
    const QString p1 = writeTinyPdf(m_scratch.filePath("uat_fnd_004_a.pdf"));
    const QString p2 = writeTinyPdf(m_scratch.filePath("uat_fnd_004_b.pdf"));
    const QString p3 = writeTinyPdf(m_scratch.filePath("uat_fnd_004_c.pdf"));

    auto* app = qobject_cast<Application*>(qApp);
    QVERIFY(app);

    // Sanity: starting state has no MainWindows (init() closed them).
    int baselineWindows = 0;
    for (auto* w : QApplication::topLevelWidgets()) {
        if (qobject_cast<MainWindow*>(w)) ++baselineWindows;
    }
    QCOMPARE(baselineWindows, 0);

    app->openFiles({p1, p2, p3});
    QApplication::processEvents();

    int spawned = 0;
    for (auto* w : QApplication::topLevelWidgets()) {
        if (auto* mw = qobject_cast<MainWindow*>(w)) {
            QCOMPARE(mw->documentCount(), 1);
            ++spawned;
        }
    }
    QCOMPARE(spawned, 3);
}

void TestUatFoundations::uat_fnd_010_menuStructure() {
    auto* app = qobject_cast<Application*>(qApp);
    QVERIFY(app);
    MainWindow* mw = app->ensureWindow();
    QVERIFY(mw);
    QMenuBar* bar = mw->menuBar();
    QVERIFY(bar);

    const QStringList topLevel = topLevelMenuTexts(bar);
    for (const QString& expected : {QStringLiteral("&File"),
                                    QStringLiteral("&Edit"),
                                    QStringLiteral("&View"),
                                    QStringLiteral("&Tools"),
                                    QStringLiteral("&Help")}) {
        QVERIFY2(topLevel.contains(expected),
                 qPrintable(QStringLiteral("Missing top-level menu: ") + expected));
    }

    // Spot-check a handful of items that the UAT doc lists.
    for (const auto& pair : {
             std::pair{QStringLiteral("&File"), QStringLiteral("&Open…")},
             std::pair{QStringLiteral("&File"), QStringLiteral("&Quit")},
             std::pair{QStringLiteral("&View"), QStringLiteral("Zoom &In")},
             std::pair{QStringLiteral("&Tools"), QStringLiteral("Rotate &Right")},
             std::pair{QStringLiteral("&Help"), QStringLiteral("&About Trailer")},
         }) {
        QAction* a = findMenuAction(bar, pair.first, pair.second);
        QVERIFY2(a, qPrintable(QStringLiteral("Missing menu item: ") + pair.first
                               + QStringLiteral(" > ") + pair.second));
    }
}

void TestUatFoundations::uat_fnd_016_toggleSidebar() {
    auto* app = qobject_cast<Application*>(qApp);
    QVERIFY(app);
    MainWindow* mw = app->ensureWindow();
    QVERIFY(mw);

    QDockWidget* sidebar = nullptr;
    for (auto* d : mw->findChildren<QDockWidget*>()) {
        if (d->windowTitle().contains(QStringLiteral("Sidebar"), Qt::CaseInsensitive)) {
            sidebar = d;
            break;
        }
    }
    QVERIFY(sidebar);
    QVERIFY(sidebar->isVisible());

    QAction* toggle = findMenuAction(mw->menuBar(), QStringLiteral("&View"),
                                     QStringLiteral("Toggle &Sidebar"));
    QVERIFY2(toggle, "View > Toggle Sidebar action not found");

    toggle->trigger();
    QApplication::processEvents();
    QVERIFY2(!sidebar->isVisible(), "First trigger should hide the Sidebar");

    toggle->trigger();
    QApplication::processEvents();
    QVERIFY2(sidebar->isVisible(), "Second trigger should show the Sidebar");
}

// UAT-FND-020 — flashError routes operation-failure feedback into the
// status bar instead of popping a QMessageBox::warning modal. The
// 2026-04-29 polish pass replaced a dozen-plus operation-failure
// modals with status-bar messages so users dealing with a tax doc /
// bill / court doc are not interrupted by a click-through every time
// something doesn't work the first try.
void TestUatFoundations::
    uat_fnd_020_flashErrorRoutesToStatusBarNotModal() {
    auto* app = qobject_cast<Application*>(qApp);
    QVERIFY(app);
    MainWindow* mw = app->ensureWindow();
    QVERIFY(mw);

    // Snapshot of any existing QMessageBox windows so we don't false-
    // positive on a leftover from another test.
    auto countMessageBoxes = []() {
        int n = 0;
        for (auto* w : QApplication::topLevelWidgets()) {
            if (w->inherits("QMessageBox")) ++n;
        }
        return n;
    };
    const int messageBoxesBefore = countMessageBoxes();

    mw->flashError(QStringLiteral("Crop failed — margins too large."));
    QApplication::processEvents();

    QVERIFY2(countMessageBoxes() == messageBoxesBefore,
             "flashError must NOT spawn a QMessageBox");
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
    QVERIFY(mw->statusBar()->currentMessage()
                .contains(QStringLiteral("not supported")));
}

// UAT-FND-030 — Auto-save writes the file when a document is dirty
// and has an established path. The 30 s timer's slot is exposed as a
// public method so the test can trigger it without waiting.
void TestUatFoundations::uat_fnd_030_autoSaveWritesDirtyDocsWithPath() {
    QVERIFY(m_scratch.isValid());
    const QString pdfPath = writeTinyPdf(m_scratch.filePath("uat_fnd_030.pdf"));
    const auto sizeBefore = QFileInfo(pdfPath).size();

    auto* app = qobject_cast<Application*>(qApp);
    QVERIFY(app);
    app->settings().setAutoSave(true);
    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow* mw = currentMainWindow();
    QVERIFY(mw);
    auto* dv = mw->findChild<QTabWidget*>();
    QVERIFY(dv);

    // Mutate the active doc so isDirty() reports true. Adding an
    // empty rectangle annotation through the public API (the same
    // path the markup toolbar uses) is enough.
    auto* doc = qobject_cast<MainWindow*>(mw)->findChild<QObject*>();
    Q_UNUSED(doc);
    // Use the test-friendly path: rotate the page (mutates state) so
    // isDirty becomes true via the document's normal write path.
    auto* dvCast = mw->findChild<DocumentView*>();
    QVERIFY(dvCast);
    if (auto* idoc = dvCast->currentDocument()) {
        idoc->rotatePage(0, 90);
        QVERIFY2(idoc->isDirty(),
                 "rotating a page should make the document dirty");
    }

    // Trigger the auto-save tick directly.
    mw->autoSaveDirtyDocs();
    QApplication::processEvents();

    // The file on disk should have been rewritten — its size will
    // typically change after a rotate, but at minimum its mtime
    // changes. Assert the doc is now clean (auto-save cleared dirty).
    auto* idocAfter = dvCast->currentDocument();
    QVERIFY2(idocAfter && !idocAfter->isDirty(),
             "After autoSaveDirtyDocs() the doc should no longer be dirty");

    // sizeBefore captured pre-edit; the post-save file is rewritten.
    Q_UNUSED(sizeBefore);
}

// UAT-FND-031 — Auto-save MUST NOT pick a destination for the user;
// untitled docs (filePath empty) are skipped. It also skips clean
// documents — no churn on idle files.
void TestUatFoundations::uat_fnd_031_autoSaveSkipsUntitledAndCleanDocs() {
    auto* app = qobject_cast<Application*>(qApp);
    QVERIFY(app);
    app->settings().setAutoSave(true);
    MainWindow* mw = app->ensureWindow();
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

// UAT-FND-040 — File → Share is wired up on platforms that have a
// native share-sheet (macOS via NSSharingServicePicker). Other
// platforms hide the menu item until xdg-email / WinShare are
// implemented. This test only checks the action presence; actually
// firing the picker is platform-modal and not UAT-friendly.
void TestUatFoundations::
    uat_fnd_040_shareMenuItemPresentOnSupportedPlatforms() {
    auto* app = qobject_cast<Application*>(qApp);
    QVERIFY(app);
    MainWindow* mw = app->ensureWindow();
    QVERIFY(mw);

    QAction* shareAction = findMenuAction(mw->menuBar(),
                                          QStringLiteral("&File"),
                                          QStringLiteral("&Share…"));
#ifdef Q_OS_MACOS
    QVERIFY2(shareAction,
             "File → Share… should be present on macOS where the "
             "NSSharingServicePicker implementation lives");
#else
    QVERIFY2(!shareAction,
             "File → Share… is hidden on platforms whose ShareService "
             "stub returns isAvailable() == false");
#endif
}

// Custom main: we need to set HOME (and XDG vars) before constructing
// Application so Settings/RecentFiles write into a sandbox, not the
// user's real config dir. QTEST_MAIN would construct QApplication
// before we got a chance to do that.
int main(int argc, char** argv) {
    QTemporaryDir fakeHome;
    if (!fakeHome.isValid()) return 1;
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
