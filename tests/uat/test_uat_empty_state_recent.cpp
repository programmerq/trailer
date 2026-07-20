// UAT harness — Inline Open Recent list in the empty state
//
// Drives Application + MainWindow in-process under
// QT_QPA_PLATFORM=offscreen to exercise the empty-state welcome
// surface's inline Open Recent list (backlog
// 2026-07-12-empty-state-open-recent-list):
//   - After files have been opened, the empty state exposes an inline
//     recent list populated from the RecentFiles model, one entry per
//     recent file, each labelled with its display name.
//   - Clicking a recent entry opens that file through the same flow as
//     File → Open Recent (Application::openFiles).
//   - When there are no recent files, the recent section is hidden — no
//     empty placeholder (no-lying-controls / no-empty-affordance).
//
// A separate evidence slot writes curated before/after grab() PNGs of
// the SAME window (empty recents vs populated) to
// $TRAILER_WELCOME_EVIDENCE_DIR when set — the G2 UX-Done artifact.
// Unset, that slot still asserts the wired behaviour like the rest.
//
// This is a NEW harness file (not an edit to test_uat_empty_state.cpp)
// to avoid colliding with the open PR #95, which also touches the shared
// empty-state harness.
//
// Mirrors the custom-main + HOME-sandbox + init() scaffolding of
// test_uat_empty_state.cpp so Settings/RecentFiles write into a
// throwaway sandbox and every case starts from a no-window baseline.

#include "app/Application.h"
#include "ui/DocumentView.h"
#include "ui/EmptyStateWidget.h"
#include "ui/MainWindow.h"

#include <QByteArray>
#include <QDir>
#include <QList>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QPointer>
#include <QPushButton>
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

bool emptyStateIsCurrent(MainWindow *mw) {
    auto *empty = mw->findChild<EmptyStateWidget *>();
    auto *stack = mw->findChild<QStackedWidget *>();
    if (!empty || !stack)
        return false;
    return stack->currentWidget() == empty;
}

// The recent-entry buttons carry the file path as their tooltip; the
// "Open File…" button does not. Filter on a non-empty tooltip to collect
// exactly the recent-entry buttons.
QList<QPushButton *> recentButtons(EmptyStateWidget *empty) {
    QList<QPushButton *> out;
    for (QPushButton *b : empty->findChildren<QPushButton *>()) {
        if (!b->toolTip().isEmpty())
            out.append(b);
    }
    return out;
}

// Close the (single) document in `mw` via the same DocumentView slot the
// tab close button drives, leaving the window as an empty state.
void closeOnlyDocument(MainWindow *mw) {
    auto *dv = mw->findChild<DocumentView *>();
    QVERIFY(dv);
    QVERIFY(QMetaObject::invokeMethod(dv, "onTabCloseRequested", Q_ARG(int, 0)));
    QApplication::processEvents();
}

QString writeTinyPdf(const QString &path) {
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    QPainter p(&writer);
    p.drawText(100, 100, QStringLiteral("UAT empty-state recent fixture"));
    p.end();
    return path;
}

} // namespace

class TestUatEmptyStateRecent : public QObject {
    Q_OBJECT
  private slots:
    void init();

    void uat_empty_recent_010_closedDocumentSurfacesRecentEntry();
    void uat_empty_recent_020_clickingRecentEntryOpensIt();
    void uat_empty_recent_030_noRecentsHidesSection();
    void uat_empty_recent_090_beforeAfterEvidence();

  private:
    QTemporaryDir m_scratch;
};

void TestUatEmptyStateRecent::init() {
    for (auto *w : QApplication::topLevelWidgets()) {
        if (qobject_cast<MainWindow *>(w))
            w->close();
    }
    QApplication::processEvents();
    // Start every case from a clean recents model so counts are exact.
    if (auto *app = qobject_cast<Application *>(qApp))
        app->clearRecent();
    QApplication::processEvents();
}

// Opening then closing a document leaves the empty-state window showing
// an inline recent list with that file as an entry, labelled by its
// display name.
void TestUatEmptyStateRecent::uat_empty_recent_010_closedDocumentSurfacesRecentEntry() {
#ifdef Q_OS_MACOS
    QSKIP("macOS closes the last window instead of persisting an empty-state window.");
#else
    QVERIFY(m_scratch.isValid());
    const QString pdfPath = writeTinyPdf(m_scratch.filePath("uat_empty_recent_010.pdf"));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    QCOMPARE(mw->documentCount(), 1);

    closeOnlyDocument(mw);
    QCOMPARE(mw->documentCount(), 0);
    QVERIFY2(emptyStateIsCurrent(mw), "After closing the last document the empty state must show");

    auto *empty = mw->findChild<EmptyStateWidget *>();
    QVERIFY(empty);
    QVERIFY2(empty->isRecentSectionVisible(),
             "With a recent file, the inline recent section must be visible");
    QCOMPARE(empty->recentEntryCount(), 1);

    const QList<QPushButton *> buttons = recentButtons(empty);
    QCOMPARE(buttons.size(), 1);
    QCOMPARE(buttons.first()->text(), QStringLiteral("uat_empty_recent_010.pdf"));
    QCOMPARE(buttons.first()->toolTip(), pdfPath);
#endif
}

// Clicking a recent entry reopens that file — the same result as
// File → Open Recent. Over the empty-state window, the click drives
// Application::openFiles and a document ends up open again.
void TestUatEmptyStateRecent::uat_empty_recent_020_clickingRecentEntryOpensIt() {
#ifdef Q_OS_MACOS
    QSKIP("macOS closes the last window instead of persisting an empty-state window.");
#else
    QVERIFY(m_scratch.isValid());
    const QString pdfPath = writeTinyPdf(m_scratch.filePath("uat_empty_recent_020.pdf"));

    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->openFiles({pdfPath});
    QApplication::processEvents();

    MainWindow *mw = currentMainWindow();
    QVERIFY(mw);
    closeOnlyDocument(mw);
    QCOMPARE(mw->documentCount(), 0);

    auto *empty = mw->findChild<EmptyStateWidget *>();
    QVERIFY(empty);
    const QList<QPushButton *> buttons = recentButtons(empty);
    QCOMPARE(buttons.size(), 1);

    // Click the recent entry the way the user would.
    buttons.first()->click();
    QApplication::processEvents();

    // openFiles may route into the persisted empty window or a fresh one
    // depending on the OpenFilesIn preference; assert some window now
    // holds the document.
    bool opened = false;
    for (auto *w : QApplication::topLevelWidgets()) {
        if (auto *cand = qobject_cast<MainWindow *>(w); cand && cand->documentCount() == 1) {
            opened = true;
            break;
        }
    }
    QVERIFY2(opened, "Clicking a recent entry must open that document (same flow as Open Recent)");
#endif
}

// No recent files → the recent section is hidden (no empty placeholder).
// Freshly-launched with a cleared recents model, the empty-state window
// must not show a recent list.
void TestUatEmptyStateRecent::uat_empty_recent_030_noRecentsHidesSection() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);

    MainWindow *mw = app->ensureWindow();
    QVERIFY(mw);
    QCOMPARE(mw->documentCount(), 0);

    auto *empty = mw->findChild<EmptyStateWidget *>();
    QVERIFY(empty);
    QVERIFY2(!empty->isRecentSectionVisible(),
             "With no recent files the inline recent section must be hidden");
    QCOMPARE(empty->recentEntryCount(), 0);
}

// G2 before/after evidence: the SAME window with an empty recents model
// (no list — the pre-change look) and after recents are populated (the
// inline list appears). Writes PNGs only when $TRAILER_WELCOME_EVIDENCE_DIR
// is set; otherwise it still asserts the state transition.
void TestUatEmptyStateRecent::uat_empty_recent_090_beforeAfterEvidence() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);

    // BEFORE: a window with no document and an empty recents model shows
    // the welcome surface with no recent list.
    MainWindow *mw = app->ensureWindow();
    QVERIFY(mw);
    mw->resize(760, 580);
    mw->show();
    QApplication::processEvents();

    auto *empty = mw->findChild<EmptyStateWidget *>();
    QVERIFY(empty);
    QVERIFY2(!empty->isRecentSectionVisible(), "Before: recent list absent with empty recents");

    const QByteArray dir = qgetenv("TRAILER_WELCOME_EVIDENCE_DIR");
    if (!dir.isEmpty()) {
        QDir().mkpath(QString::fromLocal8Bit(dir));
        QVERIFY(mw->grab().save(QString::fromLocal8Bit(dir) +
                                QStringLiteral("/empty_state_recent_before.png")));
    }

    // Populate recents by opening a few fixtures. Each open notifies every
    // window (Application::notifyWindowsRecentChanged), so this still-empty
    // window's inline list refreshes even though the documents may land in
    // other windows.
    QVERIFY(m_scratch.isValid());
    for (int i = 0; i < 3; ++i) {
        const QString p =
            writeTinyPdf(m_scratch.filePath(QStringLiteral("welcome_evidence_%1.pdf").arg(i)));
        app->openFiles({p});
        QApplication::processEvents();
    }

    QVERIFY2(empty->isRecentSectionVisible(), "After: recent list present once recents exist");
    QVERIFY(empty->recentEntryCount() >= 1);

    mw->show();
    QApplication::processEvents();
    if (!dir.isEmpty()) {
        QVERIFY(mw->grab().save(QString::fromLocal8Bit(dir) +
                                QStringLiteral("/empty_state_recent_after.png")));
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

    Application app(argc, argv);
    TestUatEmptyStateRecent tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_uat_empty_state_recent.moc"
