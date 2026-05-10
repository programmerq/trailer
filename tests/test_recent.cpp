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
        r.updateViewState(docPath, /*currentPage=*/42,
                          /*zoomFactor=*/1.5, /*scrollY=*/120,
                          /*sidebarVisible=*/false);
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
    r.updateViewState(dir.filePath("ghost.pdf"), 7, 1.0, 0, true);
    QVERIFY(r.entries().isEmpty());
}

QTEST_MAIN(TestRecent)
#include "test_recent.moc"
