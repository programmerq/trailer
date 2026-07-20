#include "document/ExternalChangeMonitor.h"

#include <QFile>
#include <QObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace trailer;

// Tests for the debounce / mute / typed-emit / re-arm logic of the file
// monitor. The deterministic seam (pokeForTest) drives the same slot a real
// QFileSystemWatcher signal would, so these run headlessly without depending
// on flaky filesystem-event timing.
class TestExternalChangeMonitor : public QObject {
    Q_OBJECT

  private slots:
    // A poke on an existing file emits externalChange (once, after debounce).
    void pokeEmitsExternalChange() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("doc.txt"));
        writeFile(path, "one");

        ExternalChangeMonitor mon;
        mon.setDebounceMsForTest(10);
        mon.setPath(path);
        QSignalSpy changed(&mon, &ExternalChangeMonitor::externalChange);
        QSignalSpy deleted(&mon, &ExternalChangeMonitor::fileDeleted);

        mon.pokeForTest();
        QVERIFY(changed.wait(1000));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(deleted.count(), 0);
    }

    // A burst of pokes within the debounce window collapses to ONE emit.
    void burstDebouncesToSingleEmit() {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("doc.txt"));
        writeFile(path, "one");

        ExternalChangeMonitor mon;
        mon.setDebounceMsForTest(40);
        mon.setPath(path);
        QSignalSpy changed(&mon, &ExternalChangeMonitor::externalChange);

        for (int i = 0; i < 5; ++i)
            mon.pokeForTest();
        QVERIFY(changed.wait(1000));
        // Give any (erroneously) queued second emit a chance to arrive.
        QTest::qWait(60);
        QCOMPARE(changed.count(), 1);
    }

    // When the file is gone, a poke emits fileDeleted, not externalChange.
    void pokeOnMissingFileEmitsDeleted() {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("doc.txt"));
        writeFile(path, "one");

        ExternalChangeMonitor mon;
        mon.setDebounceMsForTest(10);
        mon.setPath(path);
        QSignalSpy changed(&mon, &ExternalChangeMonitor::externalChange);
        QSignalSpy deleted(&mon, &ExternalChangeMonitor::fileDeleted);

        QVERIFY(QFile::remove(path));
        mon.pokeForTest();
        QVERIFY(deleted.wait(1000));
        QCOMPARE(deleted.count(), 1);
        QCOMPARE(changed.count(), 0);
    }

    // While muted, pokes are dropped entirely (our-own-save window).
    void mutedDropsEmissions() {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("doc.txt"));
        writeFile(path, "one");

        ExternalChangeMonitor mon;
        mon.setDebounceMsForTest(10);
        mon.setPath(path);
        QSignalSpy changed(&mon, &ExternalChangeMonitor::externalChange);

        mon.mute(true);
        mon.pokeForTest();
        QTest::qWait(40);
        QCOMPARE(changed.count(), 0);

        // Unmuting restores normal behaviour.
        mon.mute(false);
        mon.pokeForTest();
        QVERIFY(changed.wait(1000));
        QCOMPARE(changed.count(), 1);
    }

    // setPath("") clears watches so a stale document no longer reports.
    void clearingPathStopsReporting() {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("doc.txt"));
        writeFile(path, "one");

        ExternalChangeMonitor mon;
        mon.setDebounceMsForTest(10);
        mon.setPath(path);
        mon.setPath(QString());
        QCOMPARE(mon.path(), QString());
        QSignalSpy changed(&mon, &ExternalChangeMonitor::externalChange);
        mon.pokeForTest();
        QTest::qWait(40);
        QCOMPARE(changed.count(), 0);
    }

    // Real-filesystem smoke: editing the watched file fires SOMETHING
    // (externalChange) once the OS delivers the watch event. Tolerant of
    // watcher latency; skipped if the platform delivers no event in time.
    void realFileEditFiresSignal() {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("doc.txt"));
        writeFile(path, "one");

        ExternalChangeMonitor mon;
        mon.setPath(path);
        QSignalSpy changed(&mon, &ExternalChangeMonitor::externalChange);

        // Modify the file out-of-band.
        QTest::qWait(50);
        writeFile(path, "two-longer-content");

        if (!changed.wait(3000))
            QSKIP("platform delivered no filesystem watch event in time");
        QVERIFY(changed.count() >= 1);
    }

  private:
    static void writeFile(const QString &path, const QByteArray &data) {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(data);
        f.close();
    }
};

QTEST_MAIN(TestExternalChangeMonitor)
#include "test_external_change_monitor.moc"
