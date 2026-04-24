// Unit tests for SignatureStore — the PNG + JSON on-disk layout that
// backs the "Manage Signatures" dialog (design doc §6.4.3).

#include "signatures/SignatureStore.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace trailer;

class TestSignatures : public QObject {
    Q_OBJECT
private slots:
    void loadAllOnMissingDirectoryReturnsEmpty();
    void addWritesPngAndJsonSidecar();
    void loadAllReturnsNewestFirst();
    void removeDeletesBothFiles();
    void loadAllSynthesizesMetadataForOrphanPng();
};

namespace {

QImage solidImage() {
    QImage img(40, 20, QImage::Format_ARGB32);
    img.fill(QColor(0, 0, 0, 200));
    return img;
}

}  // namespace

void TestSignatures::loadAllOnMissingDirectoryReturnsEmpty() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString missing = dir.filePath(QStringLiteral("does-not-exist"));
    SignatureStore s(missing);
    QVERIFY(s.loadAll().empty());
}

void TestSignatures::addWritesPngAndJsonSidecar() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    SignatureStore s(dir.path());

    const Signature added = s.add(solidImage(), QStringLiteral("Work"),
                                  QStringLiteral("handwritten"));
    QVERIFY(!added.id.isEmpty());
    QVERIFY(!added.pngPath.isEmpty());
    QVERIFY(QFile::exists(added.pngPath));

    const QString jsonPath =
        added.pngPath.chopped(4) + QStringLiteral(".json");
    QVERIFY(QFile::exists(jsonPath));

    const auto all = s.loadAll();
    QCOMPARE(static_cast<int>(all.size()), 1);
    QCOMPARE(all[0].id, added.id);
    QCOMPARE(all[0].label, QStringLiteral("Work"));
    QCOMPARE(all[0].altText, QStringLiteral("handwritten"));
    QVERIFY(all[0].createdAt.isValid());
}

void TestSignatures::loadAllReturnsNewestFirst() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    SignatureStore s(dir.path());

    const Signature first = s.add(solidImage(), QStringLiteral("A"));
    // QDateTime resolution is ms; wait long enough to force a new
    // timestamp on the second id.
    QTest::qSleep(5);
    const Signature second = s.add(solidImage(), QStringLiteral("B"));
    QVERIFY(first.id != second.id);

    const auto all = s.loadAll();
    QCOMPARE(static_cast<int>(all.size()), 2);
    // Newest first — so "B" comes before "A".
    QCOMPARE(all[0].label, QStringLiteral("B"));
    QCOMPARE(all[1].label, QStringLiteral("A"));
}

void TestSignatures::removeDeletesBothFiles() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    SignatureStore s(dir.path());
    const Signature added = s.add(solidImage(), QStringLiteral("Temp"));
    QVERIFY(!added.id.isEmpty());

    QVERIFY(s.remove(added.id));
    QVERIFY(!QFile::exists(added.pngPath));
    const QString jsonPath =
        added.pngPath.chopped(4) + QStringLiteral(".json");
    QVERIFY(!QFile::exists(jsonPath));

    QVERIFY(s.loadAll().empty());
    // Removing a non-existent id is a no-op, not a crash.
    QVERIFY(!s.remove(added.id));
}

void TestSignatures::loadAllSynthesizesMetadataForOrphanPng() {
    // A user drops a PNG into the signatures folder by hand (no JSON).
    // loadAll should still pick it up and use the filename as label.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString pngPath =
        dir.filePath(QStringLiteral("sig_manual.png"));
    QVERIFY(solidImage().save(pngPath, "PNG"));

    SignatureStore s(dir.path());
    const auto all = s.loadAll();
    QCOMPARE(static_cast<int>(all.size()), 1);
    QCOMPARE(all[0].id, QStringLiteral("sig_manual"));
    QCOMPARE(all[0].label, QStringLiteral("sig_manual"));
    QVERIFY(all[0].createdAt.isValid());
}

QTEST_MAIN(TestSignatures)
#include "test_signatures.moc"
