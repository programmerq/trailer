// Unit tests for RecoveryStore — the app-data index of auto-save recovery
// sidecars (docs/decision-records/2026-07-19-autosave-recovery-sidecar.md).
//
// The store never touches the user's backing file; these tests pin its
// sidecar-path derivation, record/lookup/clear round-trip, index persistence,
// and — importantly for the external-file-change interplay (#89) — that it
// refuses to "recover" over a source that changed under it.

#include "document/RecoveryStore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace trailer;

namespace {

void writeFile(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(bytes);
    f.close();
}

// Force a file's modification time so mtime comparisons are deterministic
// (filesystem second-granularity would otherwise make same-run writes tie).
void setMtime(const QString &path, const QDateTime &when) {
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadWrite));
    QVERIFY(f.setFileTime(when, QFileDevice::FileModificationTime));
    f.close();
}

} // namespace

class TestRecoveryStore : public QObject {
    Q_OBJECT
  private slots:
    void sidecarPathIsDeterministicUnderBaseDir();
    void recordLookupClearRoundTrip();
    void pendingRecoveryNeedsNewerSidecarOverUnchangedSource();
    void pendingRecoveryRejectsExternallyChangedSource();
    void indexPersistsAcrossInstances();
};

void TestRecoveryStore::sidecarPathIsDeterministicUnderBaseDir() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    RecoveryStore store(dir.filePath("rec"));

    const QString backing = dir.filePath("mydoc.pdf");
    const QString a = store.sidecarPathFor(backing);
    const QString b = store.sidecarPathFor(backing);
    QCOMPARE(a, b); // deterministic
    QVERIFY2(a.startsWith(store.baseDir()), "sidecar must live under the store base dir");
    QVERIFY2(a.endsWith(QStringLiteral(".pdf")), "sidecar keeps the backing suffix");
    // Different backing paths get different sidecars.
    QVERIFY(store.sidecarPathFor(dir.filePath("other.pdf")) != a);
}

void TestRecoveryStore::recordLookupClearRoundTrip() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    RecoveryStore store(dir.filePath("rec"));

    const QString backing = dir.filePath("doc.pdf");
    writeFile(backing, "source");
    const QString sidecar = store.sidecarPathFor(backing);
    writeFile(sidecar, "snapshot");
    store.recordSnapshot(backing, sidecar);

    const auto e = store.lookup(backing);
    QVERIFY(e.has_value());
    QCOMPARE(e->sidecarPath, sidecar);

    store.clear(backing);
    QVERIFY2(!store.lookup(backing).has_value(), "clear drops the index entry");
    QVERIFY2(!QFile::exists(sidecar), "clear deletes the sidecar file");
    // The backing file is never touched by clear().
    QVERIFY(QFile::exists(backing));
}

void TestRecoveryStore::pendingRecoveryNeedsNewerSidecarOverUnchangedSource() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    RecoveryStore store(dir.filePath("rec"));

    const QString backing = dir.filePath("doc.pdf");
    writeFile(backing, "source");
    const QDateTime base = QDateTime::currentDateTime();
    setMtime(backing, base);

    const QString sidecar = store.sidecarPathFor(backing);
    writeFile(sidecar, "snapshot");
    setMtime(sidecar, base.addSecs(5)); // sidecar newer than source
    store.recordSnapshot(backing, sidecar);

    const auto pending = store.pendingRecovery(backing);
    QVERIFY2(pending.has_value(), "a newer sidecar over an unchanged source is recoverable");
    QCOMPARE(*pending, sidecar);
}

void TestRecoveryStore::pendingRecoveryRejectsExternallyChangedSource() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    RecoveryStore store(dir.filePath("rec"));

    const QString backing = dir.filePath("doc.pdf");
    writeFile(backing, "source");
    const QDateTime base = QDateTime::currentDateTime();
    setMtime(backing, base);

    const QString sidecar = store.sidecarPathFor(backing);
    writeFile(sidecar, "snapshot");
    setMtime(sidecar, base.addSecs(5));
    store.recordSnapshot(backing, sidecar); // captures the base mtime

    // The source changes under us (external edit, or a save from elsewhere):
    // its mtime no longer matches what we recorded, so the sidecar is stale
    // and must NOT be auto-recovered.
    setMtime(backing, base.addSecs(20));
    QVERIFY2(!store.pendingRecovery(backing).has_value(),
             "a source changed since the snapshot must not be silently overwritten by recovery");
}

void TestRecoveryStore::indexPersistsAcrossInstances() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString baseDir = dir.filePath("rec");
    const QString backing = dir.filePath("doc.pdf");
    writeFile(backing, "source");

    QString sidecar;
    {
        RecoveryStore store(baseDir);
        sidecar = store.sidecarPathFor(backing);
        writeFile(sidecar, "snapshot");
        store.recordSnapshot(backing, sidecar);
    }
    // A fresh store over the same base dir reloads the persisted index.
    RecoveryStore reopened(baseDir);
    const auto e = reopened.lookup(backing);
    QVERIFY2(e.has_value(), "the index must persist across store instances");
    QCOMPARE(e->sidecarPath, sidecar);
}

QTEST_GUILESS_MAIN(TestRecoveryStore)
#include "test_recovery_store.moc"
