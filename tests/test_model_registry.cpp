#include "ml/ModelRegistry.h"

#include <QDir>
#include <QFile>
#include <QObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest/QtTest>

using namespace trailer;

// Unit tests for the model registry. We cover: path resolution, hash
// verification, graceful failure when no URL is registered, and the
// end-to-end download path via file:// URLs pointing at fixtures (so
// these tests never touch the network).

class TestModelRegistry : public QObject {
    Q_OBJECT
  private slots:
    void builtinManifestHasAllIds();
    void localPathLivesUnderModelsDir();
    void verifyHashAcceptsMatchingFile();
    void verifyHashRejectsTamperedFile();
    void verifyHashReturnsFalseForMissingFile();
    void unknownIdFailsEnsureAvailable();
    void registeredModelWithoutUrlEmitsFailure();
    void downloadsFromFileUrlAndEmitsAvailable();
    void availableFiresEvenWhenFileAlreadyPresent();
    void corruptCacheTriggersRedownload();

  private:
    QString fixturePath() const;
};

QString TestModelRegistry::fixturePath() const {
    return QFINDTESTDATA(QStringLiteral("fixtures/identity.onnx"));
}

// SHA256 of tests/fixtures/identity.onnx. Pinned at fixture creation
// time (ModelRegistry test, Phase 6A).
static constexpr auto kIdentitySha256 =
    "990fbeccb7919f483797eeaf8513aac5496a672ab35c1335d07d18948780b800";

void TestModelRegistry::builtinManifestHasAllIds() {
    ModelRegistry reg;
    const auto list = reg.manifest();
    // Eight built-ins: u2netp, birefnet-lite, sam encoder + decoder,
    // ocr det/cls + two recognizers (latin, cjk).
    QCOMPARE(list.size(), 8);
    // Spot-check one so a future rename doesn't silently break the UI.
    QCOMPARE(reg.spec(ModelId::U2NetP).fileName, QStringLiteral("u2netp.onnx"));
}

void TestModelRegistry::localPathLivesUnderModelsDir() {
    ModelRegistry reg;
    const QString p = reg.localPath(ModelId::U2NetP);
    QVERIFY(p.endsWith(QStringLiteral("u2netp.onnx")));
}

void TestModelRegistry::verifyHashAcceptsMatchingFile() {
    QVERIFY(verifyModelHash(fixturePath(), kIdentitySha256));
}

void TestModelRegistry::verifyHashRejectsTamperedFile() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString p = dir.filePath("tampered.onnx");
    QFile f(p);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QByteArrayLiteral("nope"));
    f.close();
    QVERIFY(!verifyModelHash(p, kIdentitySha256));
}

void TestModelRegistry::verifyHashReturnsFalseForMissingFile() {
    QVERIFY(!verifyModelHash(QStringLiteral("/no/such/path.onnx"), kIdentitySha256));
}

void TestModelRegistry::unknownIdFailsEnsureAvailable() {
    ModelRegistry reg;
    reg.setManifestForTesting({}, QStringLiteral("/tmp/unused"));
    // All built-ins wiped; every enum value is now unknown.
    QVERIFY(!reg.ensureAvailable(ModelId::U2NetP));
}

void TestModelRegistry::registeredModelWithoutUrlEmitsFailure() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ModelRegistry reg;
    ModelSpec spec;
    spec.id = ModelId::U2NetP;
    spec.displayName = QStringLiteral("U2NetP");
    spec.fileName = QStringLiteral("u2netp.onnx");
    // no url, no sha256
    reg.setManifestForTesting({spec}, dir.path());

    QSignalSpy failed(&reg, &ModelRegistry::downloadFailed);
    QVERIFY(!reg.ensureAvailable(ModelId::U2NetP));
    QVERIFY(failed.wait(1000));
    QCOMPARE(failed.count(), 1);
}

void TestModelRegistry::downloadsFromFileUrlAndEmitsAvailable() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ModelRegistry reg;
    ModelSpec spec;
    spec.id = ModelId::U2NetP;
    spec.displayName = QStringLiteral("Identity (test)");
    spec.fileName = QStringLiteral("identity.onnx");
    spec.url = QUrl::fromLocalFile(fixturePath()).toString();
    spec.sha256 = QString::fromLatin1(kIdentitySha256);
    reg.setManifestForTesting({spec}, dir.path());

    QSignalSpy available(&reg, &ModelRegistry::available);
    QSignalSpy failed(&reg, &ModelRegistry::downloadFailed);

    QVERIFY(reg.ensureAvailable(ModelId::U2NetP));
    QVERIFY(available.wait(5000));
    QCOMPARE(available.count(), 1);
    QCOMPARE(failed.count(), 0);

    // File must be on disk after the signal fires, hashing correctly.
    const QString expectPath = dir.filePath(QStringLiteral("identity.onnx"));
    QVERIFY(QFile::exists(expectPath));
    QVERIFY(verifyModelHash(expectPath, spec.sha256));
}

void TestModelRegistry::availableFiresEvenWhenFileAlreadyPresent() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // Seed the models dir with a correct copy of the fixture first.
    const QString dest = dir.filePath(QStringLiteral("identity.onnx"));
    QVERIFY(QFile::copy(fixturePath(), dest));

    ModelRegistry reg;
    ModelSpec spec;
    spec.id = ModelId::U2NetP;
    spec.fileName = QStringLiteral("identity.onnx");
    spec.url = QUrl::fromLocalFile(fixturePath()).toString();
    spec.sha256 = QString::fromLatin1(kIdentitySha256);
    reg.setManifestForTesting({spec}, dir.path());

    QVERIFY(reg.isAvailable(ModelId::U2NetP));
    QSignalSpy available(&reg, &ModelRegistry::available);
    QVERIFY(reg.ensureAvailable(ModelId::U2NetP));
    QVERIFY(available.wait(1000));
    QCOMPARE(available.count(), 1);
}

void TestModelRegistry::corruptCacheTriggersRedownload() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // Seed the cache with a wrong-content file that won't hash to the
    // expected value; ensureAvailable should fetch a fresh copy.
    const QString dest = dir.filePath(QStringLiteral("identity.onnx"));
    QFile bogus(dest);
    QVERIFY(bogus.open(QIODevice::WriteOnly));
    bogus.write(QByteArrayLiteral("corrupt contents"));
    bogus.close();

    ModelRegistry reg;
    ModelSpec spec;
    spec.id = ModelId::U2NetP;
    spec.fileName = QStringLiteral("identity.onnx");
    spec.url = QUrl::fromLocalFile(fixturePath()).toString();
    spec.sha256 = QString::fromLatin1(kIdentitySha256);
    reg.setManifestForTesting({spec}, dir.path());

    QVERIFY(!reg.isAvailable(ModelId::U2NetP));

    QSignalSpy available(&reg, &ModelRegistry::available);
    QVERIFY(reg.ensureAvailable(ModelId::U2NetP));
    QVERIFY(available.wait(5000));
    QCOMPARE(available.count(), 1);
    QVERIFY(reg.isAvailable(ModelId::U2NetP));
}

QTEST_MAIN(TestModelRegistry)
#include "test_model_registry.moc"
