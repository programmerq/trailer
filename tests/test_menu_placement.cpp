// Unit test — command-surface placement invariants for per-window menus.
//
// Owner dogfooding report, 2026-08-02 (build 0.3.1-dev+768.gce56b4b8,
// macOS Retina): the Trailer application menu showed FOUR identical
// "Feedback Report…" items between "Settings…" and "Services". The
// diagnostic report attached to it listed 2 open windows; 4 MainWindows
// had been constructed that session. The items accumulate over time and
// closing a window does not reliably remove its copy.
//
// Root cause: MainWindow::buildMenus() built the Feedback item with
// `setMenuRole(QAction::ApplicationSpecificRole)`. Every MainWindow
// builds its own menu bar, and on macOS that role relocates the item out
// of the window's Help menu into the single, shared *application* menu.
// Qt's Cocoa bridge merges the well-known roles (AboutRole,
// PreferencesRole, QuitRole) into fixed application-menu slots — which is
// why About and Settings appear exactly once no matter how many windows
// exist — but ApplicationSpecificRole has no such merge: QCocoaMenuLoader
// keys those items on the per-QAction QCocoaMenuItem pointer, so each
// window contributes a separate item.
//
// The native macOS menu merge is not observable from the offscreen Linux
// harness (menuRole() is a no-op off macOS), so this test asserts the
// STRUCTURAL precondition instead — the property of the QAction graph
// that the Cocoa bridge reacts to. That is a real oracle: with zero
// ApplicationSpecificRole actions in the tree, the bridge has nothing to
// append, so the duplication cannot occur on any platform. See the PR
// body / docs/platform-conventions.md §2 for the honest statement of what
// this does and does not cover.

#include "app/Application.h"
#include "ui/MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QMenu>
#include <QMenuBar>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace trailer;

namespace {

// Every QAction reachable from a window's own object tree — the set the
// platform menu bridge walks when it builds that window's menu bar.
QList<QAction *> actionsOf(MainWindow *window) {
    return window->findChildren<QAction *>(QString(), Qt::FindChildrenRecursively);
}

// Actions carrying the one menu role Qt's macOS bridge does NOT merge into
// a single application-menu slot. Every one of these becomes its own item
// in the shared application menu, per window.
QStringList applicationSpecificActionTexts(const QList<MainWindow *> &windows) {
    QStringList out;
    for (MainWindow *w : windows) {
        for (QAction *a : actionsOf(w)) {
            if (a->menuRole() == QAction::ApplicationSpecificRole)
                out << a->text();
        }
    }
    return out;
}

int countByObjectName(MainWindow *window, const QString &name) {
    int n = 0;
    for (QAction *a : actionsOf(window)) {
        if (a->objectName() == name)
            ++n;
    }
    return n;
}

QMenu *menuByTitle(MainWindow *window, const QString &title) {
    for (QAction *top : window->menuBar()->actions()) {
        if (top->text() == title)
            return top->menu();
    }
    return nullptr;
}

} // namespace

class TestMenuPlacement : public QObject {
    Q_OBJECT

  private slots:
    void cleanup();

    // The reported defect, stated as its structural precondition.
    void noPerWindowActionUsesApplicationSpecificRole();
    // The invariant the fix must hold: exactly one Feedback Report item in
    // the command surface, however many windows have been opened/closed.
    void feedbackReportIsSinglePerWindowAndSurvivesWindowChurn();
    // G4: the item lives in the same command surface (Help) on every OS.
    void feedbackReportLivesInTheHelpMenu();
};

void TestMenuPlacement::cleanup() {
    // Destroy, don't close(): closeEvent() persists RecentFiles /
    // DocumentTypeDefaults, which is cross-test contamination this cleanup
    // exists to prevent. Mirrors tests/test_image_scale.cpp::cleanup().
    auto *app = qobject_cast<Application *>(qApp);
    if (!app)
        return;
    const QList<MainWindow *> windows = app->windows();
    for (MainWindow *w : windows)
        delete w;
}

void TestMenuPlacement::noPerWindowActionUsesApplicationSpecificRole() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app != nullptr);

    // Four windows — the exact count from the owner's report.
    QList<MainWindow *> windows;
    for (int i = 0; i < 4; ++i) {
        MainWindow *w = app->ensureFreshWindow();
        QVERIFY(w != nullptr);
        windows << w;
    }
    QCOMPARE(windows.size(), 4);

    // Pre-fix this returned four "&Feedback Report…" entries — one per
    // window, each of which the Cocoa bridge appends to the shared
    // application menu as its own item.
    const QStringList offenders = applicationSpecificActionTexts(windows);
    QVERIFY2(offenders.isEmpty(),
             qPrintable(QStringLiteral("actions with ApplicationSpecificRole (each one becomes a "
                                       "separate item in the shared macOS application menu, per "
                                       "window): ")
                        + offenders.join(QStringLiteral(", "))));
}

void TestMenuPlacement::feedbackReportIsSinglePerWindowAndSurvivesWindowChurn() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app != nullptr);

    const QString name = QStringLiteral("action.help.feedbackReport");

    // Open four, close two — the owner's session shape (2 live windows,
    // 4 constructed). The command surface must not have accumulated.
    QList<MainWindow *> windows;
    for (int i = 0; i < 4; ++i)
        windows << app->ensureFreshWindow();
    delete windows.takeAt(3);
    delete windows.takeAt(2);

    const QList<MainWindow *> live = app->windows();
    QCOMPARE(live.size(), 2);

    // Nothing was promoted into the shared application menu...
    QVERIFY(applicationSpecificActionTexts(live).isEmpty());
    // ...and each live window owns exactly one Feedback Report item, so
    // the menu bar on screen (macOS shows the active window's) shows one.
    for (MainWindow *w : live)
        QCOMPARE(countByObjectName(w, name), 1);
}

void TestMenuPlacement::feedbackReportLivesInTheHelpMenu() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app != nullptr);

    MainWindow *w = app->ensureFreshWindow();
    QVERIFY(w != nullptr);

    QMenu *help = menuByTitle(w, QStringLiteral("&Help"));
    QVERIFY2(help != nullptr, "no Help menu on the window's menu bar");

    int found = 0;
    QAction *feedback = nullptr;
    for (QAction *a : help->actions()) {
        if (a->objectName() == QStringLiteral("action.help.feedbackReport")) {
            ++found;
            feedback = a;
        }
    }
    QCOMPARE(found, 1);
    QVERIFY(feedback != nullptr);
    // G3: this action never has an unavailable state — the report degrades
    // to header + platform info rather than refusing.
    QVERIFY(feedback->isEnabled());
    // NoRole is set explicitly. Qt's *default* is TextHeuristicRole, which
    // lets the Cocoa bridge guess from the item's translated text whether it
    // is really About / Preferences / Quit and relocate it. NoRole says "this
    // belongs to the Help menu", in any language.
    QCOMPARE(feedback->menuRole(), QAction::NoRole);
}

int main(int argc, char **argv) {
    QTemporaryDir fakeHome;
    if (!fakeHome.isValid())
        return 1;
    qputenv("HOME", fakeHome.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", (fakeHome.path() + "/.config").toUtf8());
    qputenv("XDG_DATA_HOME", (fakeHome.path() + "/.local/share").toUtf8());
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QDir().mkpath(fakeHome.path() + "/.config/trailer");
    QDir().mkpath(fakeHome.path() + "/.local/share/trailer");
    // See tests/test_image_scale.cpp's main(): QSettings(org, app) defaults
    // to NativeFormat on macOS, which ignores the HOME sandbox above.
    QSettings::setDefaultFormat(QSettings::IniFormat);

    Application app(argc, argv);
    TestMenuPlacement tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_menu_placement.moc"
