// UAT harness — macOS Dock icon recents (UAT-FND-063 / UAT-FND-064)
//
// Drives Application + MainWindow in-process under QT_QPA_PLATFORM=offscreen
// to exercise the SHARED, cross-platform construction behind the macOS
// Dock icon's right-click recents menu (Application::refreshDockRecents,
// src/app/Application.cpp):
//   - The Dock menu (Application::dockMenuForTesting()) mirrors
//     RecentFiles, most-recent-first, capped to
//     DockRecents::kMaxSystemRecents (10).
//   - Missing files (moved/deleted since being recorded) are excluded —
//     RecentFiles::existingEntries, unit-tested directly in
//     tests/test_recent.cpp; this harness confirms the SAME filtering
//     shows up in the actual constructed menu end-to-end.
//   - Clearing recents (Application::clearRecent) empties the Dock menu.
//
// What this harness CANNOT cover (see UAT-FND-064 and the PR's owner
// verification checklist): whether macOS actually renders this QMenu when
// the Dock icon is right-clicked (QMenu::setAsDockMenu() is native Dock
// chrome — no offscreen surface renders it), and the entirely-separate
// "Trailer isn't running" path (src/platform/DockRecents.mm,
// NSDocumentController), which by definition has no live process for an
// offscreen harness to drive. Both need a real Mac.
//
// Mirrors the custom-main + HOME-sandbox + init() scaffolding of
// test_uat_empty_state.cpp so Settings/RecentFiles write into a
// throwaway sandbox and every case starts from a no-window baseline.

#include "app/Application.h"

#include <QAction>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMenu>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QSettings>
#include <QStringList>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace trailer;

namespace {

QString writeTinyPdf(const QString &path) {
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    QPainter p(&writer);
    p.drawText(100, 100, QStringLiteral("UAT dock recents fixture"));
    p.end();
    return path;
}

QStringList dockMenuTexts(Application *app) {
    QStringList out;
    QMenu *menu = app->dockMenuForTesting();
    if (!menu)
        return out;
    for (QAction *action : menu->actions())
        out.append(action->text());
    return out;
}

} // namespace

class TestUatDockRecents : public QObject {
    Q_OBJECT
  private slots:
    void init();

    void uat_fnd_063_menuTracksRecentFilesMostRecentFirst();
    void uat_fnd_063_capsAtTenEvenWithMoreRecents();
    void uat_fnd_063_excludesMissingFiles();
    void uat_fnd_063_clearRecentEmptiesDockMenu();
    void uat_fnd_063_chosenEntryOpensTheFile();
    void uat_fnd_063_090_menuEvidence();

  private:
    QTemporaryDir m_scratch;
};

void TestUatDockRecents::init() {
    QVERIFY(m_scratch.isValid());
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    app->clearRecent();
}

void TestUatDockRecents::uat_fnd_063_menuTracksRecentFilesMostRecentFirst() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);

    const QString a = writeTinyPdf(m_scratch.filePath("a.pdf"));
    const QString b = writeTinyPdf(m_scratch.filePath("b.pdf"));
    app->openFiles({a});
    app->openFiles({b});

    const QStringList texts = dockMenuTexts(app);
    QCOMPARE(texts.size(), 2);
    QCOMPARE(texts[0], QStringLiteral("b.pdf"));
    QCOMPARE(texts[1], QStringLiteral("a.pdf"));
}

void TestUatDockRecents::uat_fnd_063_capsAtTenEvenWithMoreRecents() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);

    for (int i = 0; i < 15; ++i) {
        app->openFiles({writeTinyPdf(m_scratch.filePath(QString("f%1.pdf").arg(i)))});
    }

    const QStringList texts = dockMenuTexts(app);
    QCOMPARE(texts.size(), 10); // DockRecents::kMaxSystemRecents
    QCOMPARE(texts.first(), QStringLiteral("f14.pdf"));
    QCOMPARE(texts.last(), QStringLiteral("f5.pdf"));
}

void TestUatDockRecents::uat_fnd_063_excludesMissingFiles() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);

    const QString keep = writeTinyPdf(m_scratch.filePath("keep.pdf"));
    const QString gone = writeTinyPdf(m_scratch.filePath("gone.pdf"));
    app->openFiles({keep});
    app->openFiles({gone});
    QCOMPARE(dockMenuTexts(app).size(), 2); // both present while gone.pdf still exists.
    QVERIFY(QFile::remove(gone));

    // refreshDockRecents() is private (Application.h) — it runs
    // automatically on every RecentFiles mutation, so open one more file
    // to trigger the same resync a real relaunch's constructor call would
    // also perform, and confirm the now-deleted entry drops out.
    app->openFiles({writeTinyPdf(m_scratch.filePath("another.pdf"))});

    const QStringList texts = dockMenuTexts(app);
    QCOMPARE(texts.size(), 2);
    QCOMPARE(texts[0], QStringLiteral("another.pdf"));
    QCOMPARE(texts[1], QStringLiteral("keep.pdf"));
    QVERIFY(!texts.contains(QStringLiteral("gone.pdf")));
}

void TestUatDockRecents::uat_fnd_063_clearRecentEmptiesDockMenu() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);

    app->openFiles({writeTinyPdf(m_scratch.filePath("a.pdf"))});
    QVERIFY(!dockMenuTexts(app).isEmpty());

    app->clearRecent();
    QVERIFY(dockMenuTexts(app).isEmpty());
}

void TestUatDockRecents::uat_fnd_063_chosenEntryOpensTheFile() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);

    const QString a = writeTinyPdf(m_scratch.filePath("a.pdf"));
    const QString b = writeTinyPdf(m_scratch.filePath("b.pdf"));
    app->openFiles({a});
    app->openFiles({b}); // b is now front-of-list, a is second.

    QMenu *menu = app->dockMenuForTesting();
    QVERIFY(menu);
    QCOMPARE(menu->actions().size(), 2);
    QAction *secondEntry = menu->actions().at(1);
    QCOMPARE(secondEntry->text(), QStringLiteral("a.pdf"));
    QCOMPARE(secondEntry->toolTip(), a); // tooltip carries the full path

    // Triggering the SECOND Dock-menu entry (a.pdf, not the current front)
    // must route through Application::openFiles just like File > Open
    // Recent — proven by a.pdf moving back to the front of RecentFiles
    // (opening always re-prepends), not merely staying wherever it was.
    secondEntry->trigger();
    QApplication::processEvents();
    QCOMPARE(app->recentFiles().entries().first().path,
             QFileInfo(a).canonicalFilePath());
    // The Dock menu itself refreshes to match (refreshDockRecents runs
    // again inside openFiles()).
    QCOMPARE(dockMenuTexts(app).first(), QStringLiteral("a.pdf"));
}

// G2 evidence: grab() the constructed Dock menu (empty vs populated), the
// same "popup then grab" technique as test_uat_window_menu_maximize.cpp.
// This captures the menu's CONTENT, not its attachment to a real Dock —
// see the file header and UAT-FND-063/064 for what remains real-Mac-only.
void TestUatDockRecents::uat_fnd_063_090_menuEvidence() {
    auto *app = qobject_cast<Application *>(qApp);
    QVERIFY(app);
    QMenu *menu = app->dockMenuForTesting();
    QVERIFY(menu);

    const QByteArray dir = qgetenv("TRAILER_DOCKRECENTS_EVIDENCE_DIR");
    const QString outDir = QString::fromLocal8Bit(dir);
    if (!dir.isEmpty())
        QDir().mkpath(outDir);

    // BEFORE: no recents yet (init() already cleared).
    menu->popup(QPoint(0, 0));
    QApplication::processEvents();
    menu->ensurePolished();
    if (!dir.isEmpty()) {
        const QPixmap before = menu->grab();
        if (!before.isNull() && before.size().width() > 1)
            before.save(outDir + QStringLiteral("/dock_menu_before.png"));
    }
    menu->close();
    QApplication::processEvents();

    // AFTER: a few recents populated.
    app->openFiles({writeTinyPdf(m_scratch.filePath("evidence-1.pdf"))});
    app->openFiles({writeTinyPdf(m_scratch.filePath("evidence-2.pdf"))});
    app->openFiles({writeTinyPdf(m_scratch.filePath("evidence-3.pdf"))});
    menu->popup(QPoint(0, 0));
    QApplication::processEvents();
    menu->ensurePolished();
    QCOMPARE(menu->actions().size(), 3);
    if (!dir.isEmpty()) {
        const QPixmap after = menu->grab();
        if (!after.isNull() && after.size().width() > 1)
            after.save(outDir + QStringLiteral("/dock_menu_after.png"));
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

    // See tests/test_image_scale.cpp's main() for why this is needed on
    // macOS: QSettings(org, app) defaults to NativeFormat there, which
    // ignores the HOME sandboxing above.
    QSettings::setDefaultFormat(QSettings::IniFormat);
    Application app(argc, argv);
    TestUatDockRecents tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_uat_dock_recents.moc"
