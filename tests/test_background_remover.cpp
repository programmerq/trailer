// Unit tests for BackgroundRemover.
//
// The real u2netp model is ~4.4 MB and tied to a network fetch, so we
// don't download it in CI. These tests cover:
//
//   - isModelReady() returns false when the cache is empty.
//   - remove() returns a null QImage when the model isn't ready
//     (i.e. no segfault, no UB, predictable error path).
//   - ensureModelAvailable() triggers the registry's download machinery
//     — we verify via a signal spy on the registry itself.
//   - Signals fan out cleanly: when the registry emits available /
//     downloadFailed for U2NetP, BackgroundRemover echoes modelReady /
//     modelUnavailable.
//   - If the environment variable TRAILER_TEST_U2NETP points at a
//     real u2netp ONNX file (32-bit float in/out, saliency output),
//     seed the cache with it and exercise the full inference path
//     against a synthetic checker pattern. Otherwise that test is
//     skipped so a CI run without the model still passes cleanly.
//
// We use setManifestForTesting so tests run against a clean temporary
// models dir and never touch the user's real cache.

#include "ml/BackgroundRemover.h"
#include "ml/ModelRegistry.h"

#include <QColor>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest/QtTest>

#include <algorithm>

using namespace trailer;

class TestBackgroundRemover : public QObject {
    Q_OBJECT
  private slots:
    void notReadyMeansFalse();
    void removeReturnsNullWhenModelMissing();
    void ensureModelAvailableFailsGracefullyWithoutUrl();
    void signalsFanOutFromRegistryFailure();
    void signalsFanOutFromRegistrySuccessWithSeededCache();
    void removeProducesAlphaVarianceWithRealModel();

  private:
    QString realModelEnvPath() const {
        return QString::fromLocal8Bit(qgetenv("TRAILER_TEST_U2NETP"));
    }
    void pinU2NetP(ModelRegistry &reg, const QString &modelsDir, const QString &url = {},
                   const QString &sha256 = {}) const {
        ModelSpec spec;
        spec.id = ModelId::U2NetP;
        spec.displayName = QStringLiteral("U2NetP (test)");
        spec.fileName = QStringLiteral("u2netp.onnx");
        spec.url = url;
        spec.sha256 = sha256;
        reg.setManifestForTesting({spec}, modelsDir);
    }
    static QByteArray sha256Hex(const QString &path) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            return {};
        QCryptographicHash h(QCryptographicHash::Sha256);
        if (!h.addData(&f))
            return {};
        return h.result().toHex();
    }
    // Build a simple checker-pattern image so inference has both bright
    // and dark regions to assign differing saliency to. Size chosen to
    // sit comfortably away from the 320x320 model resolution.
    static QImage makeCheckerPattern(int w = 512, int h = 384) {
        QImage img(w, h, QImage::Format_ARGB32);
        for (int y = 0; y < h; ++y) {
            auto *scan = reinterpret_cast<QRgb *>(img.scanLine(y));
            for (int x = 0; x < w; ++x) {
                const bool onTile = ((x / 64) + (y / 64)) % 2;
                scan[x] = onTile ? qRgb(250, 250, 250) : qRgb(20, 20, 20);
            }
        }
        return img;
    }
};

void TestBackgroundRemover::notReadyMeansFalse() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ModelRegistry reg;
    pinU2NetP(reg, dir.path());
    BackgroundRemover r(&reg);
    QVERIFY(!r.isModelReady());
}

void TestBackgroundRemover::removeReturnsNullWhenModelMissing() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ModelRegistry reg;
    pinU2NetP(reg, dir.path());
    BackgroundRemover r(&reg);
    QImage in(64, 64, QImage::Format_ARGB32);
    in.fill(Qt::red);
    const QImage out = r.remove(in);
    QVERIFY(out.isNull());
}

void TestBackgroundRemover::ensureModelAvailableFailsGracefullyWithoutUrl() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ModelRegistry reg;
    pinU2NetP(reg, dir.path()); // no URL — registry should bail
    BackgroundRemover r(&reg);

    QSignalSpy unavailable(&r, &BackgroundRemover::modelUnavailable);
    QSignalSpy ready(&r, &BackgroundRemover::modelReady);
    r.ensureModelAvailable();
    QVERIFY(unavailable.wait(1000));
    QCOMPARE(unavailable.count(), 1);
    QCOMPARE(ready.count(), 0);
}

void TestBackgroundRemover::signalsFanOutFromRegistryFailure() {
    // Point the registry at a non-existent URL so ModelDownloader
    // fails on open. BackgroundRemover should translate that into a
    // modelUnavailable emission.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ModelRegistry reg;
    pinU2NetP(reg, dir.path(), QStringLiteral("file:///absolutely/does/not/exist.onnx"),
              QStringLiteral("0000000000000000000000000000000000000000000000000000000000000000"));
    BackgroundRemover r(&reg);

    QSignalSpy unavailable(&r, &BackgroundRemover::modelUnavailable);
    r.ensureModelAvailable();
    QVERIFY(unavailable.wait(5000));
    QCOMPARE(unavailable.count(), 1);
}

void TestBackgroundRemover::signalsFanOutFromRegistrySuccessWithSeededCache() {
    // Seed the temp models dir with a file whose SHA256 we pre-compute,
    // then pin a matching ModelSpec. ensureAvailable() sees it already
    // on disk and emits available — BackgroundRemover should echo
    // modelReady. (We're not running inference here; we're verifying
    // the signal plumbing.)
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dest = dir.filePath(QStringLiteral("u2netp.onnx"));
    QFile seed(dest);
    QVERIFY(seed.open(QIODevice::WriteOnly));
    seed.write(QByteArrayLiteral("not-a-real-model-but-hashable"));
    seed.close();
    const QByteArray hash = sha256Hex(dest);
    QVERIFY(!hash.isEmpty());

    ModelRegistry reg;
    pinU2NetP(reg, dir.path(), QUrl::fromLocalFile(dest).toString(), QString::fromLatin1(hash));
    BackgroundRemover r(&reg);

    QVERIFY(r.isModelReady());
    QSignalSpy ready(&r, &BackgroundRemover::modelReady);
    r.ensureModelAvailable();
    QVERIFY(ready.wait(1000));
    QCOMPARE(ready.count(), 1);
}

void TestBackgroundRemover::removeProducesAlphaVarianceWithRealModel() {
    const QString realPath = realModelEnvPath();
    if (realPath.isEmpty() || !QFileInfo::exists(realPath)) {
        QSKIP("TRAILER_TEST_U2NETP not set or missing — skipping real "
              "inference path. Set it to a u2netp.onnx to exercise this.");
    }

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dest = dir.filePath(QStringLiteral("u2netp.onnx"));
    QVERIFY(QFile::copy(realPath, dest));

    const QByteArray hash = sha256Hex(dest);
    QVERIFY(!hash.isEmpty());

    ModelRegistry reg;
    pinU2NetP(reg, dir.path(), QUrl::fromLocalFile(realPath).toString(), QString::fromLatin1(hash));
    BackgroundRemover r(&reg);
    QVERIFY(r.isModelReady());

    const QImage input = makeCheckerPattern();
    const QImage output = r.remove(input);
    QVERIFY(!output.isNull());
    QCOMPARE(output.size(), input.size());
    QVERIFY(output.hasAlphaChannel());

    // Scan the alpha channel — a well-behaved saliency model should
    // produce at least some variance across the image. A constant
    // output would mean the pipeline wasn't exercised.
    int minAlpha = 255;
    int maxAlpha = 0;
    for (int y = 0; y < output.height(); ++y) {
        const auto *scan = reinterpret_cast<const QRgb *>(output.scanLine(y));
        for (int x = 0; x < output.width(); ++x) {
            const int a = qAlpha(scan[x]);
            minAlpha = std::min(minAlpha, a);
            maxAlpha = std::max(maxAlpha, a);
        }
    }
    QVERIFY2(
        maxAlpha - minAlpha > 32,
        qPrintable(QStringLiteral("alpha range too narrow: [%1, %2]").arg(minAlpha).arg(maxAlpha)));
}

QTEST_MAIN(TestBackgroundRemover)
#include "test_background_remover.moc"
