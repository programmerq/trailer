// UAT harness — Feedback / Diagnostic Report (Help menu)
//
// Covers UAT-XCT-071..073 in docs/uat/06-cross-cutting.md: the Help menu
// item is always enabled (G3 — no lying controls: this feature never has
// an "unavailable" state, it degrades instead), the dialog shows the full
// report text (not just a clipboard copy — the user must be able to read
// it), the "Include full file paths" checkbox defaults off and toggles
// path visibility live, and Copy to Clipboard puts the exact on-screen
// text on the clipboard.
//
// When TRAILER_FEEDBACK_EVIDENCE_DIR is set, also writes the curated G2
// evidence PNGs: the dialog with a document open (default, paths
// omitted) and the empty-state dialog reachable with zero windows open.
//
// Mirrors the custom-main + HOME-sandbox scaffolding of
// test_uat_preferences.cpp so Settings/RecentFiles write into a
// throwaway sandbox rather than the real user config dir.

#include "app/Application.h"
#include "ui/FeedbackDialog.h"
#include "ui/MainWindow.h"

#include <QPlainTextEdit>
#include <QTextCursor>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QSettings>
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
        if (topAction->text() != topText)
            continue;
        QMenu *menu = topAction->menu();
        if (!menu)
            return nullptr;
        for (QAction *action : menu->actions()) {
            if (action->text() == itemText)
                return action;
        }
    }
    return nullptr;
}

QString writeStaticImage(const QString &path) {
    QImage img(320, 240, QImage::Format_ARGB32);
    img.fill(qRgb(200, 208, 220));
    img.save(path, "PNG");
    return path;
}

// Scrolls the dialog's report view so the "Windows and open documents"
// section (the part that differs between the with-document and
// empty-state evidence shots) is on screen, not below the fold.
void scrollToWindowsSection(FeedbackDialog &dlg) {
    auto *text = dlg.findChild<QPlainTextEdit *>();
    QVERIFY2(text != nullptr, "FeedbackDialog must host a QPlainTextEdit");
    QTextCursor cursor = text->document()->find(QStringLiteral("Windows and open documents"));
    if (!cursor.isNull()) {
        text->setTextCursor(cursor);
        text->ensureCursorVisible();
    }
}

QString evidenceDir() {
    const QByteArray dir = qgetenv("TRAILER_FEEDBACK_EVIDENCE_DIR");
    if (dir.isEmpty())
        return QString();
    const QString path = QString::fromLocal8Bit(dir);
    QDir().mkpath(path);
    return path;
}

} // namespace

class TestUatFeedback : public QObject {
    Q_OBJECT
  private slots:
    void init();

    void uat_xct_071_menuItemAlwaysEnabled();
    void uat_xct_072_reportIsFullyVisibleAndOmitsPathsByDefault();
    void uat_xct_073_checkboxTogglesPathsAndCopyMatchesVisibleText();
    void uat_xct_081_feedbackItemDoesNotAccumulateAcrossWindows();
    void uat_fbk_090_evidenceShots();

  private:
    QTemporaryDir m_scratch;
};

void TestUatFeedback::init() {
    for (auto *w : QApplication::topLevelWidgets()) {
        if (qobject_cast<MainWindow *>(w))
            w->close();
    }
    QApplication::processEvents();
}

// UAT-XCT-071 — Feedback Report menu item is always enabled (G3).
// The report degrades (header + platform info only) rather than the
// control ever going grey — there is no "unavailable" state to gate on.
void TestUatFeedback::uat_xct_071_menuItemAlwaysEnabled() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    MainWindow *mw = app->ensureFreshWindow();
    QVERIFY(mw);
    QApplication::processEvents();

    QAction *action = findMenuAction(mw->menuBar(), QStringLiteral("&Help"),
                                      QStringLiteral("&Feedback Report…"));
    QVERIFY2(action, "Help menu must carry a Feedback Report… item");
    QVERIFY2(action->isEnabled(), "Feedback Report… must always be enabled (G3)");

    // Same with zero documents open (Windows/Linux empty-state window) —
    // still enabled, still produces a report (degraded, not blocked).
    QVERIFY2(mw->documentCount() == 0,
             "expected a fresh window with no documents for this assertion");
    QVERIFY(action->isEnabled());
}

// UAT-XCT-072 — the report text is shown in full in a readable widget
// (never clipboard-only), carries the stable header fields, and the
// default is to omit full file paths.
void TestUatFeedback::uat_xct_072_reportIsFullyVisibleAndOmitsPathsByDefault() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);

    const QString imgPath = writeStaticImage(m_scratch.filePath(QStringLiteral("doc072.png")));
    app->openFiles({imgPath});
    QApplication::processEvents();
    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);

    FeedbackDialog dlg(app, mw);
    const QString text = dlg.reportText();

    QVERIFY(text.contains(QStringLiteral("# Trailer Diagnostic Report")));
    QVERIFY(text.contains(QStringLiteral("**Version:")));
    QVERIFY(text.contains(QStringLiteral("**Platform:")));
    QVERIFY(text.contains(QStringLiteral("Windows and open documents")));
    QVERIFY2(!text.contains(m_scratch.path()),
             "full scratch-dir path must not appear with the paths checkbox unchecked");
    QVERIFY(dlg.includeFullPathsCheckBox());
    QVERIFY2(!dlg.includeFullPathsCheckBox()->isChecked(),
             "Include full file paths must default OFF (privacy-by-construction)");
}

// UAT-XCT-073 — toggling "Include full file paths" changes the visible
// text live, and Copy to Clipboard copies exactly what's on screen (no
// silent mismatch between what the user reviewed and what gets pasted).
void TestUatFeedback::uat_xct_073_checkboxTogglesPathsAndCopyMatchesVisibleText() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);

    const QString imgPath = writeStaticImage(m_scratch.filePath(QStringLiteral("doc073.png")));
    app->openFiles({imgPath});
    QApplication::processEvents();
    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);

    FeedbackDialog dlg(app, mw);
    QVERIFY(!dlg.reportText().contains(m_scratch.path()));

    dlg.includeFullPathsCheckBox()->setChecked(true);
    QApplication::processEvents();
    QVERIFY2(dlg.reportText().contains(m_scratch.path()),
             "checking the box must reveal the full path live");

    QVERIFY(dlg.copyButton());
    dlg.copyButton()->click();
    QApplication::processEvents();
    QCOMPARE(QGuiApplication::clipboard()->text(), dlg.reportText());
}

// UAT-XCT-081 (docs/uat/06-cross-cutting.md) — "Feedback Report…" does not
// accumulate in the command surface as windows are opened and closed.
//
// Owner dogfooding, 2026-08-02 (macOS Retina): FOUR identical "Feedback
// Report…" items stacked in the application menu after four MainWindows had
// been constructed in one session; two of those windows were already
// closed. Cause: the item carried QAction::ApplicationSpecificRole, which
// on macOS moves it into the single shared application menu — and unlike
// About / Settings / Quit, that role is not merged, so each window
// contributes its own copy.
//
// The native macOS merge is not observable offscreen (menuRole() does
// nothing off macOS), so this guards the structural precondition the Cocoa
// bridge reacts to: nothing is promoted into the shared application menu,
// and each window's Help menu holds exactly one item.
void TestUatFeedback::uat_xct_081_feedbackItemDoesNotAccumulateAcrossWindows() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);

    QList<MainWindow *> opened;
    for (int i = 0; i < 4; ++i) {
        MainWindow *mw = app->ensureFreshWindow();
        QVERIFY(mw);
        opened << mw;
    }
    // Close two — the owner's session shape (4 constructed, 2 still open).
    opened.takeLast()->close();
    opened.takeLast()->close();
    QApplication::processEvents();

    const QList<MainWindow *> live = app->windows();
    QCOMPARE(live.size(), 2);

    for (MainWindow *mw : live) {
        int inHelp = 0;
        int promotedToAppMenu = 0;
        for (QAction *a : mw->findChildren<QAction *>()) {
            if (a->objectName() == QStringLiteral("action.help.feedbackReport"))
                ++inHelp;
            if (a->menuRole() == QAction::ApplicationSpecificRole)
                ++promotedToAppMenu;
        }
        QCOMPARE(inHelp, 1);
        QVERIFY2(promotedToAppMenu == 0,
                 "no per-window action may claim ApplicationSpecificRole — each one becomes a "
                 "separate item in the shared macOS application menu");
        // And it is reachable where docs/platform-conventions.md §2 says it
        // is: the Help menu, on every platform.
        QAction *fromMenu =
            findMenuAction(mw->menuBar(), QStringLiteral("&Help"), QStringLiteral("&Feedback Report…"));
        QVERIFY2(fromMenu, "Feedback Report… is not in the Help menu");
        QVERIFY(fromMenu->isEnabled());
    }
}

// Curated G2 evidence (only when TRAILER_FEEDBACK_EVIDENCE_DIR is set):
// the dialog with a document open (default state, paths omitted) and the
// dialog reachable from a zero-document window (the empty-state case).
void TestUatFeedback::uat_fbk_090_evidenceShots() {
    const QString dir = evidenceDir();
    if (dir.isEmpty())
        QSKIP("TRAILER_FEEDBACK_EVIDENCE_DIR not set; skipping evidence capture");

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);

    // --- With a document open ---
    {
        const QString imgPath =
            writeStaticImage(m_scratch.filePath(QStringLiteral("evidence-doc.png")));
        app->openFiles({imgPath});
        QApplication::processEvents();
        MainWindow *mw = currentMainWindow();
        QVERIFY(mw);
        mw->resize(1000, 720);
        mw->show();
        QApplication::processEvents();

        FeedbackDialog dlg(app, mw);
        dlg.resize(720, 980);
        dlg.show();
        QApplication::processEvents();
        scrollToWindowsSection(dlg);
        QApplication::processEvents();
        QVERIFY(dlg.grab().save(dir + "/feedback-dialog-with-document.png"));

        // Toggle-on state: "Include full file paths" checked, revealing the
        // real scratch path in the document line.
        dlg.includeFullPathsCheckBox()->setChecked(true);
        QApplication::processEvents();
        scrollToWindowsSection(dlg);
        QApplication::processEvents();
        QVERIFY(dlg.grab().save(dir + "/feedback-dialog-full-paths-checked.png"));
        dlg.close();
    }

    // --- Empty-state window (zero documents) ---
    {
        for (auto *w : QApplication::topLevelWidgets()) {
            if (qobject_cast<MainWindow *>(w))
                w->close();
        }
        QApplication::processEvents();
        MainWindow *mw = app->ensureFreshWindow();
        QVERIFY(mw);
        mw->resize(900, 600);
        mw->show();
        QApplication::processEvents();

        FeedbackDialog dlg(app, mw);
        dlg.resize(720, 980);
        dlg.show();
        QApplication::processEvents();
        scrollToWindowsSection(dlg);
        QApplication::processEvents();
        QVERIFY(dlg.grab().save(dir + "/feedback-dialog-empty-state.png"));
        dlg.close();
    }

    // --- The Help menu itself (UAT-XCT-081 before/after pair) ---
    //
    // On Windows and Linux this capture is expected to be IDENTICAL before
    // and after the ApplicationSpecificRole removal: QAction::menuRole() is
    // a macOS-only property, so the item was already in the Help menu on
    // these platforms. That identity IS the evidence for those platforms —
    // the fix does not reshape their command surface. The macOS half (the
    // item moving out of the shared application menu) cannot be captured
    // offscreen; see UAT-XCT-081's "known coverage limit".
    {
        for (auto *w : QApplication::topLevelWidgets()) {
            if (qobject_cast<MainWindow *>(w))
                w->close();
        }
        QApplication::processEvents();
        MainWindow *mw = app->ensureFreshWindow();
        QVERIFY(mw);
        mw->resize(900, 600);
        mw->show();
        QApplication::processEvents();

        QMenu *help = nullptr;
        for (QAction *top : mw->menuBar()->actions()) {
            if (top->text() == QStringLiteral("&Help"))
                help = top->menu();
        }
        QVERIFY(help);
        help->popup(QPoint(0, 0));
        QApplication::processEvents();
        QVERIFY(help->grab().save(dir + "/help-menu.png"));
        help->close();
        QApplication::processEvents();
    }
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

    QSettings::setDefaultFormat(QSettings::IniFormat);
    Application app(argc, argv);
    TestUatFeedback tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_uat_feedback.moc"
