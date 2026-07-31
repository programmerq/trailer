#include "recent/RecentFiles.h"

#include <QFile>
#include <QObject>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace trailer;

class TestRecent : public QObject {
    Q_OBJECT
  private slots:
    void addDedupesAndOrders();
    void trimsToMaxEntries();
    void roundTripsToDisk();
    void clearEmptiesList();
    void viewStateRoundTripsAndRestoresOnLookup();
    void viewStateUpdateNoOpsForUnknownPath();
    void extendedViewStateRoundTrips();
    void legacyViewStateLoadsGracefully();
    void existingEntriesCapsToLimit();
    void existingEntriesFiltersMissingFiles();
    void existingEntriesPreservesMostRecentFirstOrder();
    void existingEntriesZeroLimitIsEmpty();
    void existingEntriesLimitAboveSizeReturnsAll();
    void existingEntriesEmptyListIsEmpty();
};

namespace {
QString touch(const QTemporaryDir &dir, const QString &name) {
    const QString path = dir.filePath(name);
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.close();
    }
    return path;
}
} // namespace

void TestRecent::addDedupesAndOrders() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString a = touch(dir, "a.txt");
    const QString b = touch(dir, "b.txt");

    RecentFiles r(dir.filePath("recent.json"));
    r.add(a);
    r.add(b);
    r.add(a); // moves a to the front

    const auto entries = r.entries();
    QCOMPARE(entries.size(), 2);
    QCOMPARE(QFileInfo(entries[0].path).fileName(), QStringLiteral("a.txt"));
    QCOMPARE(QFileInfo(entries[1].path).fileName(), QStringLiteral("b.txt"));
}

void TestRecent::trimsToMaxEntries() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    RecentFiles r(dir.filePath("recent.json"));
    r.setMaxEntries(3);

    for (int i = 0; i < 10; ++i) {
        const QString path = touch(dir, QString("f%1.txt").arg(i));
        r.add(path);
    }

    QCOMPARE(r.entries().size(), 3);
    // Most recent should be f9; oldest in the retained set is f7.
    QCOMPARE(QFileInfo(r.entries().first().path).fileName(), QStringLiteral("f9.txt"));
    QCOMPARE(QFileInfo(r.entries().last().path).fileName(), QStringLiteral("f7.txt"));
}

void TestRecent::roundTripsToDisk() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString a = touch(dir, "a.txt");
    const QString b = touch(dir, "b.txt");
    const QString recentPath = dir.filePath("recent.json");

    {
        RecentFiles r(recentPath);
        r.add(a);
        r.add(b);
        r.save();
    }

    QVERIFY(QFile::exists(recentPath));

    RecentFiles reloaded(recentPath);
    reloaded.load();
    QCOMPARE(reloaded.entries().size(), 2);
    QCOMPARE(QFileInfo(reloaded.entries().first().path).fileName(), QStringLiteral("b.txt"));
}

void TestRecent::clearEmptiesList() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    RecentFiles r(dir.filePath("recent.json"));
    r.add(touch(dir, "a.txt"));
    r.clear();
    QVERIFY(r.entries().isEmpty());
}

void TestRecent::viewStateRoundTripsAndRestoresOnLookup() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString recentPath = dir.filePath("recent.json");
    const QString docPath = touch(dir, "doc.pdf");

    {
        RecentFiles r(recentPath);
        r.add(docPath);
        // Simulate the user closing on page 42 with the sidebar
        // hidden. zoom/scroll fields are intentionally non-zero so
        // we can confirm the JSON round-trip carries them.
        RecentEntry state;
        state.currentPage = 42;
        state.zoomFactor = 1.5;
        state.scrollY = 120;
        state.zoomMode = ZoomMode::Custom;
        state.sidebarMode = SidebarMode::Hidden;
        r.updateViewState(docPath, state);
        r.save();
    }

    RecentFiles reloaded(recentPath);
    reloaded.load();
    const RecentEntry e = reloaded.findByPath(docPath);
    QVERIFY2(!e.path.isEmpty(), "findByPath should locate the entry by canonical path");
    QCOMPARE(e.currentPage, 42);
    QCOMPARE(e.zoomFactor, 1.5);
    QCOMPARE(e.scrollY, 120);
    QCOMPARE(e.sidebarVisible, false);
}

void TestRecent::viewStateUpdateNoOpsForUnknownPath() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    RecentFiles r(dir.filePath("recent.json"));
    // Updating a path that's not in the list must silently do
    // nothing — closing a never-recented document shouldn't add it.
    RecentEntry state;
    state.currentPage = 7;
    state.zoomFactor = 1.0;
    state.sidebarMode = SidebarMode::Pages;
    r.updateViewState(dir.filePath("ghost.pdf"), state);
    QVERIFY(r.entries().isEmpty());
}

void TestRecent::extendedViewStateRoundTrips() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString recentPath = dir.filePath("recent.json");
    const QString docPath = touch(dir, "doc.pdf");

    const QByteArray geom = QByteArray::fromHex("01020304deadbeef");
    const QByteArray winState = QByteArray::fromHex("aabbccdd11223344");

    {
        RecentFiles r(recentPath);
        r.add(docPath);
        RecentEntry state;
        state.currentPage = 7;
        state.zoomFactor = 2.0;
        state.scrollY = 350;
        state.zoomMode = ZoomMode::FitToWidth;
        state.sidebarMode = SidebarMode::TableOfContents;
        state.markupToolbarVisible = true;
        state.windowGeometry = geom;
        state.windowState = winState;
        r.updateViewState(docPath, state);
        r.save();
    }

    RecentFiles reloaded(recentPath);
    reloaded.load();
    const RecentEntry e = reloaded.findByPath(docPath);
    QVERIFY(!e.path.isEmpty());
    QCOMPARE(e.zoomMode, ZoomMode::FitToWidth);
    QCOMPARE(e.sidebarMode, SidebarMode::TableOfContents);
    QCOMPARE(e.markupToolbarVisible, true);
    QCOMPARE(e.windowGeometry, geom);
    QCOMPARE(e.windowState, winState);
    QVERIFY(e.hasViewState());
}

void TestRecent::legacyViewStateLoadsGracefully() {
    // Simulate a pre-Workstream-I recent.json that only carries the
    // original fields. The new entries should default sensibly:
    //   - sidebarMode derived from sidebar_visible (true → Pages).
    //   - zoomMode defaults to Custom.
    //   - markup_toolbar_visible defaults to false.
    //   - window geometry / state remain empty.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString recentPath = dir.filePath("recent.json");
    const char *legacy =
        R"([{"path":"/tmp/legacy.pdf","display_name":"legacy.pdf",)"
        R"("opened_at":"2026-04-01T12:00:00Z","current_page":3,)"
        R"("zoom_factor":1.25,"scroll_y":50,"sidebar_visible":true}])";
    QFile f(recentPath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(legacy);
    f.close();

    RecentFiles r(recentPath);
    r.load();
    QCOMPARE(r.entries().size(), 1);
    const RecentEntry e = r.entries().first();
    QCOMPARE(e.currentPage, 3);
    QCOMPARE(e.zoomFactor, 1.25);
    QCOMPARE(e.scrollY, 50);
    QCOMPARE(e.sidebarVisible, true);
    QCOMPARE(e.sidebarMode, SidebarMode::Pages);
    QCOMPARE(e.zoomMode, ZoomMode::Custom);
    QCOMPARE(e.markupToolbarVisible, false);
    QVERIFY(e.windowGeometry.isEmpty());
    QVERIFY(e.windowState.isEmpty());
}

void TestRecent::existingEntriesCapsToLimit() {
    // Mirrors the macOS Dock-menu / system Recent-Documents cap
    // (DockRecents::kMaxSystemRecents == 10) without depending on
    // src/platform/ — the cap value itself is passed in by the caller.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    RecentFiles r(dir.filePath("recent.json"));
    for (int i = 0; i < 15; ++i) {
        r.add(touch(dir, QString("f%1.txt").arg(i)));
    }
    QCOMPARE(r.entries().size(), 15); // Trailer's own list is uncapped here.

    const auto capped = r.existingEntries(10);
    QCOMPARE(capped.size(), 10);
    // Most-recent-first: f14 was added last.
    QCOMPARE(QFileInfo(capped.first().path).fileName(), QStringLiteral("f14.txt"));
    QCOMPARE(QFileInfo(capped.last().path).fileName(), QStringLiteral("f5.txt"));
}

void TestRecent::existingEntriesFiltersMissingFiles() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    RecentFiles r(dir.filePath("recent.json"));
    const QString a = touch(dir, "a.txt");
    const QString b = dir.filePath("missing-b.txt"); // never created
    const QString c = touch(dir, "c.txt");
    r.add(a);
    r.add(b);
    r.add(c);
    QCOMPARE(r.entries().size(), 3); // Trailer's own list keeps the dead entry.

    const auto existing = r.existingEntries(10);
    QCOMPARE(existing.size(), 2);
    QCOMPARE(QFileInfo(existing[0].path).fileName(), QStringLiteral("c.txt"));
    QCOMPARE(QFileInfo(existing[1].path).fileName(), QStringLiteral("a.txt"));
}

void TestRecent::existingEntriesPreservesMostRecentFirstOrder() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    RecentFiles r(dir.filePath("recent.json"));
    const QString a = touch(dir, "a.txt");
    const QString b = touch(dir, "b.txt");
    const QString c = touch(dir, "c.txt");
    r.add(a);
    r.add(b);
    r.add(c);
    r.add(a); // re-opening a moves it back to the front.

    const auto existing = r.existingEntries(10);
    QCOMPARE(existing.size(), 3);
    QCOMPARE(QFileInfo(existing[0].path).fileName(), QStringLiteral("a.txt"));
    QCOMPARE(QFileInfo(existing[1].path).fileName(), QStringLiteral("c.txt"));
    QCOMPARE(QFileInfo(existing[2].path).fileName(), QStringLiteral("b.txt"));
}

void TestRecent::existingEntriesZeroLimitIsEmpty() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    RecentFiles r(dir.filePath("recent.json"));
    r.add(touch(dir, "a.txt"));
    QVERIFY(r.existingEntries(0).isEmpty());
    QVERIFY(r.existingEntries(-1).isEmpty());
}

void TestRecent::existingEntriesLimitAboveSizeReturnsAll() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    RecentFiles r(dir.filePath("recent.json"));
    r.add(touch(dir, "a.txt"));
    r.add(touch(dir, "b.txt"));
    QCOMPARE(r.existingEntries(1000).size(), 2);
}

void TestRecent::existingEntriesEmptyListIsEmpty() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    RecentFiles r(dir.filePath("recent.json"));
    QVERIFY(r.existingEntries(10).isEmpty());
}

QTEST_MAIN(TestRecent)
#include "test_recent.moc"
