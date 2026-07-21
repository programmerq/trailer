// UAT harness — Window-menu Maximize/Restore dynamic label
//
// Drives Application + MainWindow in-process under
// QT_QPA_PLATFORM=offscreen to exercise the Window-menu maximize/restore
// action's dynamic label (backlog
// 2026-07-12-maximize-restore-dynamic-label):
//   - On Win/Linux the action reads "&Maximize" while the window is
//     normal and flips to "&Restore" once the window is maximized,
//     tracking the live window state (via changeEvent) — and back again
//     on restore.
//   - On macOS the action keeps the static native "&Zoom" label (the
//     platform-native shape); the dynamic relabel is non-mac only.
//
// A gated evidence slot writes a grab() of the open Window menu (static
// "Maximize" vs "Restore") to $TRAILER_WINDOWMENU_EVIDENCE_DIR when set —
// the G2 UX-Done artifact. Menus can be awkward to render offscreen, so
// the slot degrades to the text assertion if the grab is empty.
//
// Mirrors the custom-main + HOME-sandbox + init() scaffolding of
// test_uat_empty_state.cpp.

#include "app/Application.h"
#include "ui/MainWindow.h"

#include <QAction>
#include <QByteArray>
#include <QDir>
#include <QMenu>
#include <QMenuBar>
#include <QPixmap>
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

QMenu *windowMenu(QMenuBar *bar) {
    for (QAction *top : bar->actions()) {
        if (top->text() == QStringLiteral("&Window"))
            return top->menu();
    }
    return nullptr;
}

// The maximize/restore action is the one whose text is any of the three
// labels it can carry across platforms. Match on that set so the lookup
// survives a relabel.
QAction *maximizeAction(QMenu *menu) {
    for (QAction *a : menu->actions()) {
        const QString t = a->text();
        if (t == QStringLiteral("&Maximize") || t == QStringLiteral("&Restore") ||
            t == QStringLiteral("&Zoom"))
            return a;
    }
    return nullptr;
}

} // namespace

class TestUatWindowMenuMaximize : public QObject {
    Q_OBJECT
  private slots:
    void init();

    void uat_winmenu_010_labelTracksWindowState();
    void uat_winmenu_090_menuEvidence();

  private:
    QTemporaryDir m_scratch;
};

void TestUatWindowMenuMaximize::init() {
    for (auto *w : QApplication::topLevelWidgets()) {
        if (qobject_cast<MainWindow *>(w))
            w->close();
    }
    QApplication::processEvents();
}

// The maximize/restore label reflects the window state: "Maximize" when
// normal, "Restore" when maximized (Win/Linux); static "Zoom" on macOS.
void TestUatWindowMenuMaximize::uat_winmenu_010_labelTracksWindowState() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    MainWindow *mw = app->ensureWindow();
    QVERIFY(mw);
    mw->showNormal();
    QApplication::processEvents();

    QMenu *menu = windowMenu(mw->menuBar());
    QVERIFY2(menu, "Window menu not found");
    QAction *action = maximizeAction(menu);
    QVERIFY2(action, "Maximize/Restore/Zoom action not found");

#ifdef Q_OS_MACOS
    QCOMPARE(action->text(), QStringLiteral("&Zoom"));
    // On macOS the label is static — maximizing must not change it.
    mw->showMaximized();
    QApplication::processEvents();
    QCOMPARE(action->text(), QStringLiteral("&Zoom"));
#else
    // Normal window: "Maximize".
    QCOMPARE(action->text(), QStringLiteral("&Maximize"));

    // Maximize → changeEvent retitles to "Restore".
    mw->showMaximized();
    QApplication::processEvents();
    QVERIFY2(mw->isMaximized(), "showMaximized() should put the window in the maximized state");
    QCOMPARE(action->text(), QStringLiteral("&Restore"));

    // Restore → back to "Maximize".
    mw->showNormal();
    QApplication::processEvents();
    QVERIFY2(!mw->isMaximized(), "showNormal() should leave the maximized state");
    QCOMPARE(action->text(), QStringLiteral("&Maximize"));
#endif
}

// G2 evidence: render the open Window menu in both states. Menus are
// awkward to grab offscreen; if the grab comes back empty the slot falls
// back to the label assertion (which the case above already guarantees),
// and the PR notes the menu image is a best-effort supplement.
void TestUatWindowMenuMaximize::uat_winmenu_090_menuEvidence() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    MainWindow *mw = app->ensureWindow();
    QVERIFY(mw);
    mw->resize(760, 580);
    mw->show();
    mw->showNormal();
    QApplication::processEvents();

    QMenu *menu = windowMenu(mw->menuBar());
    QVERIFY(menu);
    QAction *action = maximizeAction(menu);
    QVERIFY(action);

    const QByteArray dir = qgetenv("TRAILER_WINDOWMENU_EVIDENCE_DIR");
    const QString outDir = QString::fromLocal8Bit(dir);
    if (!dir.isEmpty())
        QDir().mkpath(outDir);

    // BEFORE: normal window → "Maximize". Popup so the menu lays out,
    // then grab it.
    menu->popup(QPoint(0, 0));
    QApplication::processEvents();
    menu->ensurePolished();
#ifndef Q_OS_MACOS
    QCOMPARE(action->text(), QStringLiteral("&Maximize"));
#endif
    if (!dir.isEmpty()) {
        const QPixmap before = menu->grab();
        if (!before.isNull() && before.size().width() > 1)
            before.save(outDir + QStringLiteral("/window_menu_maximize_before.png"));
    }
    menu->close();
    QApplication::processEvents();

    // AFTER: maximized window → "Restore".
    mw->showMaximized();
    QApplication::processEvents();
    menu->popup(QPoint(0, 0));
    QApplication::processEvents();
    menu->ensurePolished();
#ifndef Q_OS_MACOS
    QCOMPARE(action->text(), QStringLiteral("&Restore"));
#endif
    if (!dir.isEmpty()) {
        const QPixmap after = menu->grab();
        if (!after.isNull() && after.size().width() > 1)
            after.save(outDir + QStringLiteral("/window_menu_maximize_after.png"));
    }
    menu->close();
    QApplication::processEvents();
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
    TestUatWindowMenuMaximize tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_uat_window_menu_maximize.moc"
