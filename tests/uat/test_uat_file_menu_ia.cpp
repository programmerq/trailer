// UAT harness — File-menu Information-Architecture redesign
//
// Drives Application + MainWindow in-process under
// QT_QPA_PLATFORM=offscreen to exercise the File-menu IA redesign
// (DR 2026-07-18-file-menu-acquire-ia):
//   - ⌘N is "New from Clipboard" (the hottest acquire path), replacing
//     the former standalone "New" (blank window) binding.
//   - "Acquire" is dissolved into direct File-menu entries: a Screenshot
//     submenu (Whole Screen / Window / Selected Area) plus visible-but-
//     disabled Scanner / Camera placeholders with honest tooltips.
//   - These create/acquire commands stay reachable whether or not a
//     document window is key (the regression: they used to live only in
//     the macOS no-window bar and vanished the moment a window became
//     key). Here we assert they are present on the per-window File menu
//     both with zero and with one-or-more document windows open.
//   - New-from-Clipboard tracks the clipboard: image / openable file →
//     enabled; non-image plain text → disabled + tooltip, and invoking
//     it is a silent no-op (never an informational popup).
//   - The dead ⌘⇧3 binding is gone (macOS eats it globally).
//
// Mirrors the custom-main + HOME-sandbox + init() scaffolding of
// test_uat_empty_state.cpp so Settings/RecentFiles write into a
// throwaway sandbox and every case starts from a no-window baseline.

#include "app/Application.h"
#include "ui/MainWindow.h"

#include <QAction>
#include <QClipboard>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
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

// Locate the File menu on a window's menu bar.
QMenu *fileMenu(QMenuBar *bar) {
    for (QAction *topAction : bar->actions()) {
        if (topAction->text() == QStringLiteral("&File"))
            return topAction->menu();
    }
    return nullptr;
}

// A top-level File-menu item by exact text.
QAction *fileItem(QMenuBar *bar, const QString &itemText) {
    QMenu *menu = fileMenu(bar);
    if (!menu)
        return nullptr;
    for (QAction *action : menu->actions()) {
        if (action->text() == itemText)
            return action;
    }
    return nullptr;
}

// A sub-item under a File-menu submenu (e.g. Screenshot → Whole Screen).
QAction *fileSubItem(QMenuBar *bar, const QString &submenuText, const QString &itemText) {
    QAction *sub = fileItem(bar, submenuText);
    if (!sub || !sub->menu())
        return nullptr;
    for (QAction *action : sub->menu()->actions()) {
        if (action->text() == itemText)
            return action;
    }
    return nullptr;
}

// Recursively collect every action reachable from a menu bar (including
// those nested in submenus) so shortcut-uniqueness assertions cover the
// whole surface, not just the File menu's first level.
void collectActions(QMenu *menu, QList<QAction *> &out) {
    if (!menu)
        return;
    for (QAction *action : menu->actions()) {
        out.append(action);
        if (action->menu())
            collectActions(action->menu(), out);
    }
}

QList<QAction *> allActions(QMenuBar *bar) {
    QList<QAction *> out;
    for (QAction *topAction : bar->actions()) {
        out.append(topAction);
        if (topAction->menu())
            collectActions(topAction->menu(), out);
    }
    return out;
}

QString writeTinyPdf(const QString &path) {
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    QPainter p(&writer);
    p.drawText(100, 100, QStringLiteral("UAT file-menu-IA fixture"));
    p.end();
    return path;
}

void setClipboardImage() {
    QImage img(4, 4, QImage::Format_ARGB32);
    img.fill(Qt::red);
    QGuiApplication::clipboard()->setImage(img);
}

void setClipboardPlainText(const QString &text) {
    // A non-file, non-image payload: plain text that is NOT an existing
    // path, so clipboardHasOpenableContent() is false.
    QGuiApplication::clipboard()->setText(text);
}

} // namespace

class TestUatFileMenuIa : public QObject {
    Q_OBJECT
  private slots:
    void init();

    void uat_fmia_001_acquireActionsPresentWithNoWindowDoc();
    void uat_fmia_002_acquireActionsPresentAndEnabledWithDocumentOpen();
    void uat_fmia_003_cmdN_boundToNewFromClipboardNotStandaloneNew();
    void uat_fmia_004_scannerAndCameraDisabledWithHonestTooltip();
    void uat_fmia_005_nonImageClipboardDisablesAndNoopsNewFromClipboard();
    void uat_fmia_006_deadCmdShift3BindingRemoved();
    void uat_fmia_007_clipboardDataChangedAutoRefreshesNewFromClipboard();

  private:
    QTemporaryDir m_scratch;
};

void TestUatFileMenuIa::init() {
    for (auto *w : QApplication::topLevelWidgets()) {
        if (qobject_cast<MainWindow *>(w))
            w->close();
    }
    QApplication::processEvents();
}

// (a) With no document open, the acquire/screenshot/new-from-clipboard
// group is PRESENT in the per-window File menu: New from Clipboard, the
// Screenshot submenu with its Whole Screen / Window / Selected Area
// sub-items, and the Scanner / Camera placeholders all exist.
void TestUatFileMenuIa::uat_fmia_001_acquireActionsPresentWithNoWindowDoc() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);

    MainWindow *mw = app->ensureWindow();
    QVERIFY(mw);
    QCOMPARE(mw->documentCount(), 0);
    QMenuBar *bar = mw->menuBar();

    QVERIFY2(fileItem(bar, QStringLiteral("New from &Clipboard")),
             "File → New from Clipboard must be present with no document open");
    QVERIFY2(fileItem(bar, QStringLiteral("Screenshot")),
             "File → Screenshot submenu must be present with no document open");
    QVERIFY2(fileSubItem(bar, QStringLiteral("Screenshot"), QStringLiteral("Whole Screen")),
             "Screenshot → Whole Screen must be present");
    QVERIFY2(fileSubItem(bar, QStringLiteral("Screenshot"), QStringLiteral("Window")),
             "Screenshot → Window must be present");
    QVERIFY2(fileSubItem(bar, QStringLiteral("Screenshot"), QStringLiteral("Selected Area")),
             "Screenshot → Selected Area must be present");
    QVERIFY2(fileItem(bar, QStringLiteral("Scanner")),
             "File → Scanner placeholder must be present");
    QVERIFY2(fileItem(bar, QStringLiteral("Camera")),
             "File → Camera placeholder must be present");
}

// (b) Regression guard for the "vanish" bug: with a document window
// open, the same acquire/screenshot/new-from-clipboard actions are
// STILL present AND enabled. New from Clipboard's enabled state tracks
// the clipboard, so we prime it with an image first; Whole Screen and
// the Screenshot submenu itself are enabled on every platform (Window /
// Selected Area are platform-gated on non-macOS, so we only assert
// their presence, not their enabled state).
void TestUatFileMenuIa::uat_fmia_002_acquireActionsPresentAndEnabledWithDocumentOpen() {
    QVERIFY(m_scratch.isValid());
    const QString pdfPath = writeTinyPdf(m_scratch.filePath("uat_fmia_002.pdf"));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);

    // Prime the clipboard with an image so New from Clipboard is enabled,
    // then refresh so the registered actions pick up the state.
    setClipboardImage();
    app->refreshClipboardActions();

    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    QCOMPARE(mw->documentCount(), 1);
    QMenuBar *bar = mw->menuBar();

    QAction *newClip = fileItem(bar, QStringLiteral("New from &Clipboard"));
    QVERIFY2(newClip, "New from Clipboard must NOT vanish when a document window is open");
    // Refresh once more now the doc window's own action is registered.
    app->refreshClipboardActions();
    QVERIFY2(newClip->isEnabled(),
             "New from Clipboard must be enabled when the clipboard holds an image");

    QAction *screenshot = fileItem(bar, QStringLiteral("Screenshot"));
    QVERIFY2(screenshot, "Screenshot submenu must NOT vanish when a document window is open");
    QVERIFY2(screenshot->isEnabled(), "Screenshot submenu must be enabled with a document open");

    QAction *wholeScreen =
        fileSubItem(bar, QStringLiteral("Screenshot"), QStringLiteral("Whole Screen"));
    QVERIFY2(wholeScreen, "Screenshot → Whole Screen must still be present with a document open");
    QVERIFY2(wholeScreen->isEnabled(),
             "Screenshot → Whole Screen must be enabled with a document open");

    // Presence-only for the placeholders / platform-gated modes.
    QVERIFY2(fileSubItem(bar, QStringLiteral("Screenshot"), QStringLiteral("Window")),
             "Screenshot → Window must still be present with a document open");
    QVERIFY2(fileSubItem(bar, QStringLiteral("Screenshot"), QStringLiteral("Selected Area")),
             "Screenshot → Selected Area must still be present with a document open");
    QVERIFY2(fileItem(bar, QStringLiteral("Scanner")),
             "Scanner must still be present with a document open");
    QVERIFY2(fileItem(bar, QStringLiteral("Camera")),
             "Camera must still be present with a document open");
}

// (c) ⌘N / Ctrl+N (QKeySequence::New) is bound to New from Clipboard,
// and NOT to any old standalone "New" (blank window) item — which no
// longer exists on the File menu at all.
void TestUatFileMenuIa::uat_fmia_003_cmdN_boundToNewFromClipboardNotStandaloneNew() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    MainWindow *mw = app->ensureWindow();
    QVERIFY(mw);
    QMenuBar *bar = mw->menuBar();

    QAction *newClip = fileItem(bar, QStringLiteral("New from &Clipboard"));
    QVERIFY2(newClip, "New from Clipboard action not found");
    QVERIFY2(newClip->shortcut() == QKeySequence(QKeySequence::New),
             "New from Clipboard must carry the standard New shortcut (⌘N / Ctrl+N)");

    // No other action anywhere on the menu bar may claim the New
    // shortcut, and there is no standalone "New" / "&New" item.
    const QKeySequence newSeq(QKeySequence::New);
    for (QAction *action : allActions(bar)) {
        if (action == newClip)
            continue;
        QVERIFY2(!action->shortcuts().contains(newSeq),
                 qPrintable(QStringLiteral("Only New from Clipboard may own ⌘N; also claimed by: %1")
                                .arg(action->text())));
        const QString stripped = action->text();
        QVERIFY2(stripped != QStringLiteral("New") && stripped != QStringLiteral("&New"),
                 "The old standalone New (blank window) item must no longer exist");
    }
}

// (d) Scanner and Camera exist but are DISABLED (no backend yet) and
// carry a non-empty, honest tooltip rather than being hidden.
void TestUatFileMenuIa::uat_fmia_004_scannerAndCameraDisabledWithHonestTooltip() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    MainWindow *mw = app->ensureWindow();
    QVERIFY(mw);
    QMenuBar *bar = mw->menuBar();

    QAction *scanner = fileItem(bar, QStringLiteral("Scanner"));
    QVERIFY2(scanner, "Scanner action must exist");
    QVERIFY2(!scanner->isEnabled(), "Scanner must be DISABLED (no backend yet)");
    QVERIFY2(!scanner->toolTip().isEmpty(), "Scanner must carry an honest placeholder tooltip");

    QAction *camera = fileItem(bar, QStringLiteral("Camera"));
    QVERIFY2(camera, "Camera action must exist");
    QVERIFY2(!camera->isEnabled(), "Camera must be DISABLED (no backend yet)");
    QVERIFY2(!camera->toolTip().isEmpty(), "Camera must carry an honest placeholder tooltip");
}

// (e) Non-image clipboard: with the clipboard holding plain text that is
// not an openable path, New from Clipboard is DISABLED (refreshClipboard
// Actions' contract), and invoking newFromClipboard() is a silent no-op
// — no document opens, no modal dialog. (If a modal were shown here it
// would block exec() and hang the offscreen test.)
void TestUatFileMenuIa::uat_fmia_005_nonImageClipboardDisablesAndNoopsNewFromClipboard() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    MainWindow *mw = app->ensureWindow();
    QVERIFY(mw);
    QCOMPARE(mw->documentCount(), 0);
    QMenuBar *bar = mw->menuBar();

    setClipboardPlainText(QStringLiteral("this is plain text, not an image or a path"));
    app->refreshClipboardActions();

    QVERIFY2(!Application::clipboardHasOpenableContent(),
             "Plain non-path text must not count as openable clipboard content");

    QAction *newClip = fileItem(bar, QStringLiteral("New from &Clipboard"));
    QVERIFY2(newClip, "New from Clipboard action not found");
    QVERIFY2(!newClip->isEnabled(),
             "New from Clipboard must be DISABLED when the clipboard holds only non-image text");
    QVERIFY2(!newClip->toolTip().isEmpty(),
             "The disabled New from Clipboard item must show an honest tooltip, not a popup");

    // Guard path: invoking the slot directly must be a no-op — no new
    // document, no modal. (The real UI can't reach this because the item
    // is disabled; we drive the slot to prove the guard, not a popup.)
    const int before = mw->documentCount();
    app->newFromClipboard();
    QApplication::processEvents();
    QCOMPARE(mw->documentCount(), before);
    QCOMPARE(app->windowCount(), 1);
}

// (f) The dead ⌘⇧3 binding (macOS eats it globally) is removed: no
// action anywhere on the menu bar owns Ctrl/Cmd+Shift+3.
void TestUatFileMenuIa::uat_fmia_006_deadCmdShift3BindingRemoved() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    MainWindow *mw = app->ensureWindow();
    QVERIFY(mw);
    QMenuBar *bar = mw->menuBar();

    const QKeySequence deadSeq(Qt::CTRL | Qt::SHIFT | Qt::Key_3);
    for (QAction *action : allActions(bar)) {
        QVERIFY2(!action->shortcuts().contains(deadSeq),
                 qPrintable(QStringLiteral("⌘⇧3 must be unbound; still claimed by: %1")
                                .arg(action->text())));
    }
}

// (g) The QClipboard::dataChanged → refreshClipboardActions wiring keeps
// New from Clipboard live WITHOUT any manual refresh. We never call
// refreshClipboardActions() here: we mutate the clipboard, pump the event
// loop so the dataChanged signal is delivered, and assert the item's
// enabled state tracked the change. Deleting the connect() in Application
// would make this fail (the earlier cases mask it by refreshing by hand).
void TestUatFileMenuIa::uat_fmia_007_clipboardDataChangedAutoRefreshesNewFromClipboard() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    MainWindow *mw = app->ensureWindow();
    QVERIFY(mw);
    QMenuBar *bar = mw->menuBar();

    QAction *newClip = fileItem(bar, QStringLiteral("New from &Clipboard"));
    QVERIFY2(newClip, "New from Clipboard action not found");

    // Image on the clipboard → auto-refresh must ENABLE it (no manual
    // refreshClipboardActions() call anywhere in this test).
    setClipboardImage();
    QApplication::processEvents();
    QVERIFY2(newClip->isEnabled(),
             "dataChanged wiring must auto-enable New from Clipboard when an "
             "image is copied (no manual refresh)");

    // Plain non-path text → auto-refresh must DISABLE it again.
    setClipboardPlainText(QStringLiteral("still not an image or a path"));
    QApplication::processEvents();
    QVERIFY2(!newClip->isEnabled(),
             "dataChanged wiring must auto-disable New from Clipboard when the "
             "clipboard turns to non-openable text (no manual refresh)");
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
    TestUatFileMenuIa tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_uat_file_menu_ia.moc"
