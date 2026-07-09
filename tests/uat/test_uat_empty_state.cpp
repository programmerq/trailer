// UAT harness — Empty-State Window Model
//
// Drives Application + MainWindow in-process under
// QT_QPA_PLATFORM=offscreen to exercise the empty-state window model:
//   - A window with no document shows the EmptyStateWidget and keeps
//     File → Open enabled.
//   - Opening a document swaps to the document view.
//   - (Win/Linux) Closing the last document of the last window leaves
//     the window alive, showing the empty state again.
//
// Mirrors the custom-main + HOME-sandbox + init() scaffolding of
// test_uat_foundations.cpp so Settings/RecentFiles write into a
// throwaway sandbox and every case starts from a no-window baseline.

#include "app/Application.h"
#include "document/IDocument.h"
#include "ui/DocumentView.h"
#include "ui/EmptyStateWidget.h"
#include "ui/MainWindow.h"
#include "ui/MarkupToolbar.h"

#include <QAction>
#include <QMenu>
#include <QMenuBar>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QPointer>
#include <QStackedWidget>
#include <QTemporaryDir>
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

// The stacked page currently showing an EmptyStateWidget must be the
// stack's current widget for the empty state to be visible to the user.
bool emptyStateIsCurrent(MainWindow *mw) {
    auto *empty = mw->findChild<EmptyStateWidget *>();
    auto *stack = mw->findChild<QStackedWidget *>();
    if (!empty || !stack)
        return false;
    return stack->currentWidget() == empty;
}

QString writeTinyPdf(const QString &path) {
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    QPainter p(&writer);
    p.drawText(100, 100, QStringLiteral("UAT empty-state fixture"));
    p.end();
    return path;
}

} // namespace

class TestUatEmptyState : public QObject {
    Q_OBJECT
  private slots:
    void init();

    void uat_empty_001_launchNoDocumentShowsEmptyState();
    void uat_empty_002_openingDocumentHidesEmptyState();
    void uat_empty_003_closingLastDocumentPersistsEmptyState();
    void uat_empty_004_multiWindowClosesNonLastWindow();
    void uat_empty_005_markupToolbarHiddenOverEmptyState();

  private:
    QTemporaryDir m_scratch;
};

void TestUatEmptyState::init() {
    for (auto *w : QApplication::topLevelWidgets()) {
        if (qobject_cast<MainWindow *>(w))
            w->close();
    }
    QApplication::processEvents();
}

// T1 — a window spawned with no document shows the EmptyStateWidget
// (headline + button), documentCount()==0, and File → Open is enabled.
void TestUatEmptyState::uat_empty_001_launchNoDocumentShowsEmptyState() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);

    MainWindow *mw = app->ensureWindow();
    QVERIFY(mw);
    QCOMPARE(mw->documentCount(), 0);

    auto *empty = mw->findChild<EmptyStateWidget *>();
    QVERIFY2(empty, "MainWindow should contain an EmptyStateWidget");
    QVERIFY2(emptyStateIsCurrent(mw),
             "With no document open the empty state must be the current stack page");

    QAction *open =
        findMenuAction(mw->menuBar(), QStringLiteral("&File"), QStringLiteral("&Open…"));
    QVERIFY2(open, "File → Open… action not found");
    QVERIFY2(open->isEnabled(), "File → Open… must be enabled in the empty state");
}

// T5 — opening a real document swaps the central stack away from the
// empty state to the document view.
void TestUatEmptyState::uat_empty_002_openingDocumentHidesEmptyState() {
    QVERIFY(m_scratch.isValid());
    const QString pdfPath = writeTinyPdf(m_scratch.filePath("uat_empty_002.pdf"));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    QCOMPARE(mw->documentCount(), 1);
    QVERIFY2(!emptyStateIsCurrent(mw),
             "With a document open the empty state must NOT be the current stack page");

    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    QVERIFY2(dv->isVisible(), "The document view should be visible once a document is open");
}

// T2 — (Win/Linux) opening then closing the last document of the last
// window leaves the window alive with the empty state shown again,
// rather than deleting the window / quitting the app.
void TestUatEmptyState::uat_empty_003_closingLastDocumentPersistsEmptyState() {
#ifdef Q_OS_MACOS
    QSKIP("macOS closes the last window instead of persisting an empty-state window.");
#else
    QVERIFY(m_scratch.isValid());
    const QString pdfPath = writeTinyPdf(m_scratch.filePath("uat_empty_003.pdf"));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    QCOMPARE(mw->documentCount(), 1);
    QCOMPARE(app->windowCount(), 1);

    QPointer<MainWindow> guard(mw);

    // Drive the tab-close path through the DocumentView so allTabsClosed
    // fires exactly as it would from the UI close button. The tab-close
    // handler is a private slot wired to QTabWidget::tabCloseRequested;
    // invoke it by name (the same slot the close button triggers).
    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    QVERIFY(QMetaObject::invokeMethod(dv, "onTabCloseRequested", Q_ARG(int, 0)));
    QApplication::processEvents();

    QVERIFY2(!guard.isNull(),
             "The last window must persist (not be deleted) when its last document closes");
    QCOMPARE(mw->documentCount(), 0);
    QVERIFY2(emptyStateIsCurrent(mw),
             "After closing the last document the empty state must be shown again");
    QCOMPARE(app->windowCount(), 1);

    QAction *open =
        findMenuAction(mw->menuBar(), QStringLiteral("&File"), QStringLiteral("&Open…"));
    QVERIFY2(open && open->isEnabled(), "File → Open… must remain enabled in the empty state");

    // Gate 4 (no lying controls): document-dependent actions must be
    // DISABLED over the empty state — there is no live document for
    // Save / Save As / Print / Export to act on. Closing the last tab
    // drives QTabWidget::currentChanged(-1) → onCurrentDocumentChanged(
    // nullptr), which is what disables them; assert it actually did.
    QAction *save =
        findMenuAction(mw->menuBar(), QStringLiteral("&File"), QStringLiteral("&Save"));
    QVERIFY2(save, "File → Save action not found");
    QVERIFY2(!save->isEnabled(), "File → Save must be DISABLED over the empty state");
    QAction *saveAs =
        findMenuAction(mw->menuBar(), QStringLiteral("&File"), QStringLiteral("Save &As…"));
    QVERIFY2(saveAs, "File → Save As action not found");
    QVERIFY2(!saveAs->isEnabled(), "File → Save As must be DISABLED over the empty state");
    QAction *print =
        findMenuAction(mw->menuBar(), QStringLiteral("&File"), QStringLiteral("&Print…"));
    QVERIFY2(print, "File → Print action not found");
    QVERIFY2(!print->isEnabled(), "File → Print must be DISABLED over the empty state");
#endif
}

// T3 — (Win/Linux) multi-window: closing the last document of a window
// while OTHER windows exist closes that window (empty-window pile-up
// avoidance) rather than persisting it as an empty state. The other
// window is untouched. This is the windowCount() > 1 branch of
// onAllTabsClosed that the single-window cases above never exercise.
void TestUatEmptyState::uat_empty_004_multiWindowClosesNonLastWindow() {
#ifdef Q_OS_MACOS
    QSKIP("macOS closes the last window regardless of window count.");
#else
    QVERIFY(m_scratch.isValid());
    const QString pdf1 = writeTinyPdf(m_scratch.filePath("uat_empty_004a.pdf"));
    const QString pdf2 = writeTinyPdf(m_scratch.filePath("uat_empty_004b.pdf"));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);

    // Default open mode is NewWindow, so each openFiles() call spawns a
    // fresh window holding exactly one document.
    app->openFiles({pdf1});
    QApplication::processEvents();
    app->openFiles({pdf2});
    QApplication::processEvents();
    QCOMPARE(app->windowCount(), 2);

    const QList<MainWindow *> wins = app->windows();
    QCOMPARE(wins.size(), 2);
    MainWindow *victim = wins.first();
    MainWindow *survivor = wins.last();
    QVERIFY(victim && survivor && victim != survivor);
    QCOMPARE(victim->documentCount(), 1);
    QCOMPARE(survivor->documentCount(), 1);

    QPointer<MainWindow> victimGuard(victim);
    QPointer<MainWindow> survivorGuard(survivor);

    auto *dv = victim->findChild<DocumentView *>();
    QVERIFY(dv);
    QVERIFY(QMetaObject::invokeMethod(dv, "onTabCloseRequested", Q_ARG(int, 0)));
    QApplication::processEvents();

    QVERIFY2(victimGuard.isNull(),
             "A non-last window must be CLOSED (deleted) when its last document closes");
    QVERIFY2(!survivorGuard.isNull(), "The other window must remain alive");
    QCOMPARE(app->windowCount(), 1);
    QCOMPARE(survivor->documentCount(), 1);
#endif
}

// T4 — (Win/Linux) no lying controls: a markup toolbar the user
// surfaced while a document was open must not linger, visible with its
// annotation-tool buttons, over the empty state after the document is
// closed. Previously closing the last document closed the window, so
// this surface never existed; the empty-state window model must hide
// the document-only toolbars when it swaps to the welcome surface.
void TestUatEmptyState::uat_empty_005_markupToolbarHiddenOverEmptyState() {
#ifdef Q_OS_MACOS
    QSKIP("macOS closes the last window instead of persisting an empty-state window.");
#else
    QVERIFY(m_scratch.isValid());
    const QString pdfPath = writeTinyPdf(m_scratch.filePath("uat_empty_005.pdf"));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    QCOMPARE(mw->documentCount(), 1);

    auto *markup = mw->findChild<MarkupToolbar *>();
    QVERIFY(markup);
    // Surface the markup toolbar the way the user would (View → Toggle
    // Markup Toolbar / Ctrl+Shift+A drives the same toggleViewAction).
    markup->toggleViewAction()->trigger();
    QApplication::processEvents();
    QVERIFY2(markup->toggleViewAction()->isChecked(),
             "Markup toolbar should be shown after toggling it on with a document open");

    // Close the last document → empty state.
    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    QVERIFY(QMetaObject::invokeMethod(dv, "onTabCloseRequested", Q_ARG(int, 0)));
    QApplication::processEvents();

    QCOMPARE(mw->documentCount(), 0);
    QVERIFY2(emptyStateIsCurrent(mw), "Empty state must be shown after closing the last document");
    QVERIFY2(!markup->toggleViewAction()->isChecked(),
             "Markup toolbar must be hidden over the empty state (no lying controls)");
#endif
}

// Custom main: sandbox HOME before Application is constructed so
// Settings/RecentFiles never touch the real config dir.
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
    TestUatEmptyState tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_uat_empty_state.moc"
