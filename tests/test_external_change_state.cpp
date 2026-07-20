#include "document/ExternalChangeState.h"

#include <QObject>
#include <QTemporaryDir>
#include <QTest>

using namespace trailer;

// Unit tests for the pure external-change classifier (the "is this a
// conflict?" decision) plus the FileBaseline::fromPath stat helper. The
// classifier is a free function with no Qt-UI deps, so every row of the
// behaviour matrix is asserted headlessly here.
class TestExternalChangeState : public QObject {
    Q_OBJECT

  private slots:
    // No baseline (untitled / never-saved doc) is never a conflict, whatever
    // the disk looks like or how dirty we are.
    void invalidBaselineIsNoChange() {
        FileBaseline none; // valid == false
        QCOMPARE(classifyExternalChange(none, true, 12345, 100, true),
                 ExternalChangeState::NoChange);
        QCOMPARE(classifyExternalChange(none, false, 0, -1, true), ExternalChangeState::NoChange);
    }

    // Matching mtime + size against a valid baseline: nothing changed.
    void matchingBaselineIsNoChange() {
        FileBaseline b{true, 1000, 500};
        QCOMPARE(classifyExternalChange(b, true, 1000, 500, false),
                 ExternalChangeState::NoChange);
        // Even a dirty buffer is NoChange when the file on disk still matches
        // what we read — our edits are ours; the disk is untouched.
        QCOMPARE(classifyExternalChange(b, true, 1000, 500, true), ExternalChangeState::NoChange);
    }

    // Clean buffer + changed mtime → silently reloadable.
    void cleanExternalMtimeChange() {
        FileBaseline b{true, 1000, 500};
        QCOMPARE(classifyExternalChange(b, true, 2000, 500, false),
                 ExternalChangeState::CleanExternalChange);
    }

    // Same mtime but a different size still counts as a change (the secondary
    // signal catches a same-second overwrite).
    void sizeOnlyChangeIsDetected() {
        FileBaseline b{true, 1000, 500};
        QCOMPARE(classifyExternalChange(b, true, 1000, 512, false),
                 ExternalChangeState::CleanExternalChange);
    }

    // Dirty buffer + changed disk → genuine conflict, never auto-resolved.
    void dirtyExternalChangeIsConflict() {
        FileBaseline b{true, 1000, 500};
        QCOMPARE(classifyExternalChange(b, true, 2000, 600, true),
                 ExternalChangeState::DirtyConflict);
    }

    // File gone → Deleted regardless of dirty state.
    void missingFileIsDeleted() {
        FileBaseline b{true, 1000, 500};
        QCOMPARE(classifyExternalChange(b, false, 0, -1, false), ExternalChangeState::Deleted);
        QCOMPARE(classifyExternalChange(b, false, 0, -1, true), ExternalChangeState::Deleted);
    }

    // fromPath yields an invalid baseline for an empty path or a missing file,
    // and a valid one for a real file whose mtime/size it captures.
    void fromPathStatsRealFile() {
        QCOMPARE(FileBaseline::fromPath(QString()).valid, false);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString missing = dir.filePath(QStringLiteral("nope.txt"));
        QCOMPARE(FileBaseline::fromPath(missing).valid, false);

        const QString real = dir.filePath(QStringLiteral("real.txt"));
        QFile f(real);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("hello");
        f.close();
        const FileBaseline b = FileBaseline::fromPath(real);
        QVERIFY(b.valid);
        QCOMPARE(b.size, static_cast<qint64>(5));
        QVERIFY(b.mtimeMs > 0);

        // classifyExternalChangeFor against the just-captured baseline is
        // NoChange, and Deleted once the file is removed.
        QCOMPARE(classifyExternalChangeFor(b, real, false), ExternalChangeState::NoChange);
        QVERIFY(QFile::remove(real));
        QCOMPARE(classifyExternalChangeFor(b, real, false), ExternalChangeState::Deleted);
    }
};

QTEST_MAIN(TestExternalChangeState)
#include "test_external_change_state.moc"
