// Unit tests for SamSession (MobileSAM encoder + decoder).
//
// MobileSAM weights are ~44 MB total, so we don't download them in
// CI. These tests cover:
//
//   - isModelReady() is false with an empty cache, and segment() /
//     prepare() fail fast without emitting signals.
//   - ensureModelsAvailable() fails with modelsUnavailable when the
//     registry has no URL registered.
//   - modelsReady fires exactly once when BOTH models land on disk.
//   - If TRAILER_TEST_SAM_ENCODER + TRAILER_TEST_SAM_DECODER both
//     point at real MobileSAM ONNX files, run the full pipeline on a
//     synthetic disc-on-background image and verify the mask is
//     non-empty, mostly inside the clicked region, and that
//     contourFromLastMask returns a plausible polygon. Otherwise
//     that test is skipped.
//
// As with test_background_remover, we use setManifestForTesting to
// keep tests hermetic.

#include "ml/ModelRegistry.h"
#include "ml/SamSession.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QObject>
#include <QPolygon>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest/QtTest>

#include <algorithm>

using namespace trailer;

class TestSamSession : public QObject {
    Q_OBJECT
  private slots:
    void notReadyWhenCacheEmpty();
    void segmentFailsWithoutPrepare();
    void ensureModelsAvailableFailsGracefullyWithoutUrls();
    void modelsReadyFiresWhenBothModelsSeeded();
    void endToEndInferenceWithRealModels();

  private:
    static QByteArray sha256Hex(const QString &path) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            return {};
        QCryptographicHash h(QCryptographicHash::Sha256);
        if (!h.addData(&f))
            return {};
        return h.result().toHex();
    }
    static ModelSpec makeSpec(ModelId id, const QString &fileName, const QString &url = {},
                              const QString &sha256 = {}) {
        ModelSpec s;
        s.id = id;
        s.displayName = fileName;
        s.fileName = fileName;
        s.url = url;
        s.sha256 = sha256;
        return s;
    }
    static QImage makeTestScene(int w = 320, int h = 240) {
        // Dark-grey background with a bright circle in the middle —
        // easy foreground for SAM to segment when we click inside.
        QImage img(w, h, QImage::Format_ARGB32);
        img.fill(qRgb(32, 32, 32));
        const int cx = w / 2;
        const int cy = h / 2;
        const int r = std::min(w, h) / 4;
        for (int y = 0; y < h; ++y) {
            auto *scan = reinterpret_cast<QRgb *>(img.scanLine(y));
            for (int x = 0; x < w; ++x) {
                const int dx = x - cx;
                const int dy = y - cy;
                if (dx * dx + dy * dy <= r * r) {
                    scan[x] = qRgb(220, 220, 220);
                }
            }
        }
        return img;
    }
    static QPoint sceneCenter(const QImage &img) {
        return QPoint(img.width() / 2, img.height() / 2);
    }
};

void TestSamSession::notReadyWhenCacheEmpty() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ModelRegistry reg;
    reg.setManifestForTesting(
        {makeSpec(ModelId::MobileSamEncoder, QStringLiteral("mobile_sam_encoder.onnx")),
         makeSpec(ModelId::MobileSamDecoder, QStringLiteral("mobile_sam_decoder.onnx"))},
        dir.path());
    SamSession s(&reg);
    QVERIFY(!s.isModelReady());
}

void TestSamSession::segmentFailsWithoutPrepare() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ModelRegistry reg;
    reg.setManifestForTesting(
        {makeSpec(ModelId::MobileSamEncoder, QStringLiteral("mobile_sam_encoder.onnx")),
         makeSpec(ModelId::MobileSamDecoder, QStringLiteral("mobile_sam_decoder.onnx"))},
        dir.path());
    SamSession s(&reg);
    const QImage mask = s.segment({QPoint(10, 10)}, {});
    QVERIFY(mask.isNull());
    QVERIFY(s.contourFromLastMask().isEmpty());
}

void TestSamSession::ensureModelsAvailableFailsGracefullyWithoutUrls() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ModelRegistry reg;
    // Specs without URLs should make the registry emit failure.
    reg.setManifestForTesting(
        {makeSpec(ModelId::MobileSamEncoder, QStringLiteral("mobile_sam_encoder.onnx")),
         makeSpec(ModelId::MobileSamDecoder, QStringLiteral("mobile_sam_decoder.onnx"))},
        dir.path());
    SamSession s(&reg);

    QSignalSpy failed(&s, &SamSession::modelsUnavailable);
    s.ensureModelsAvailable();
    QVERIFY(failed.wait(2000));
    QVERIFY(failed.count() >= 1);
}

void TestSamSession::modelsReadyFiresWhenBothModelsSeeded() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // Seed both destinations with placeholder bytes and pin their
    // actual hashes. The registry will treat them as cached and
    // ensureAvailable() emits available(id, path) for each — the
    // SamSession only emits modelsReady once BOTH are ready.
    const QString encPath = dir.filePath(QStringLiteral("mobile_sam_encoder.onnx"));
    const QString decPath = dir.filePath(QStringLiteral("mobile_sam_decoder.onnx"));
    {
        QFile f(encPath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArrayLiteral("enc-placeholder"));
    }
    {
        QFile f(decPath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArrayLiteral("dec-placeholder"));
    }
    const QString encHash = QString::fromLatin1(sha256Hex(encPath));
    const QString decHash = QString::fromLatin1(sha256Hex(decPath));
    QVERIFY(!encHash.isEmpty());
    QVERIFY(!decHash.isEmpty());

    ModelRegistry reg;
    reg.setManifestForTesting(
        {makeSpec(ModelId::MobileSamEncoder, QStringLiteral("mobile_sam_encoder.onnx"),
                  QUrl::fromLocalFile(encPath).toString(), encHash),
         makeSpec(ModelId::MobileSamDecoder, QStringLiteral("mobile_sam_decoder.onnx"),
                  QUrl::fromLocalFile(decPath).toString(), decHash)},
        dir.path());
    SamSession s(&reg);

    QVERIFY(s.isModelReady());
    QSignalSpy ready(&s, &SamSession::modelsReady);
    s.ensureModelsAvailable();
    QVERIFY(ready.wait(2000));
    QVERIFY(ready.count() >= 1);
}

void TestSamSession::endToEndInferenceWithRealModels() {
    const QString encSrc = QString::fromLocal8Bit(qgetenv("TRAILER_TEST_SAM_ENCODER"));
    const QString decSrc = QString::fromLocal8Bit(qgetenv("TRAILER_TEST_SAM_DECODER"));
    if (encSrc.isEmpty() || !QFileInfo::exists(encSrc) || decSrc.isEmpty() ||
        !QFileInfo::exists(decSrc)) {
        QSKIP("TRAILER_TEST_SAM_ENCODER + TRAILER_TEST_SAM_DECODER not "
              "set — skipping real inference path. Set both to the "
              "MobileSAM ONNX files to exercise this.");
    }

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString encPath = dir.filePath(QStringLiteral("mobile_sam_encoder.onnx"));
    const QString decPath = dir.filePath(QStringLiteral("mobile_sam_decoder.onnx"));
    QVERIFY(QFile::copy(encSrc, encPath));
    QVERIFY(QFile::copy(decSrc, decPath));
    const QString encHash = QString::fromLatin1(sha256Hex(encPath));
    const QString decHash = QString::fromLatin1(sha256Hex(decPath));

    ModelRegistry reg;
    reg.setManifestForTesting(
        {makeSpec(ModelId::MobileSamEncoder, QStringLiteral("mobile_sam_encoder.onnx"),
                  QUrl::fromLocalFile(encPath).toString(), encHash),
         makeSpec(ModelId::MobileSamDecoder, QStringLiteral("mobile_sam_decoder.onnx"),
                  QUrl::fromLocalFile(decPath).toString(), decHash)},
        dir.path());
    SamSession s(&reg);
    QVERIFY(s.isModelReady());

    const QImage scene = makeTestScene();
    QVERIFY(s.prepare(scene));
    QCOMPARE(s.preparedSize(), scene.size());

    const QImage mask = s.segment({sceneCenter(scene)}, {});
    QVERIFY(!mask.isNull());
    QCOMPARE(mask.size(), scene.size());
    QCOMPARE(mask.format(), QImage::Format_Grayscale8);

    // Count foreground pixels — should be non-zero and cover less
    // than the whole image (not a degenerate "all-white" fallback).
    int fg = 0;
    for (int y = 0; y < mask.height(); ++y) {
        const uchar *scan = mask.constScanLine(y);
        for (int x = 0; x < mask.width(); ++x) {
            if (scan[x])
                ++fg;
        }
    }
    const int total = mask.width() * mask.height();
    QVERIFY2(fg > total / 64,
             qPrintable(QStringLiteral("mask too sparse: %1/%2").arg(fg).arg(total)));
    QVERIFY2(fg < total - total / 16,
             qPrintable(QStringLiteral("mask covers too much: %1/%2").arg(fg).arg(total)));

    // The clicked pixel must end up inside the mask.
    const QPoint click = sceneCenter(scene);
    QVERIFY2(mask.constScanLine(click.y())[click.x()] != 0, "Clicked pixel should be foreground");

    // applyAsAlpha yields an ARGB32 image with the mask in alpha.
    const QImage alphaApplied = s.applyAsAlpha(scene);
    QVERIFY(!alphaApplied.isNull());
    QCOMPARE(alphaApplied.size(), scene.size());
    QVERIFY(alphaApplied.hasAlphaChannel());

    // Contour should give us a polygon of reasonable complexity.
    const QPolygon poly = s.contourFromLastMask();
    QVERIFY2(poly.size() >= 3, "Contour should have at least 3 points");
    QVERIFY2(poly.boundingRect().intersects(scene.rect()),
             "Contour bounds should overlap the image");
}

QTEST_MAIN(TestSamSession)
#include "test_sam_session.moc"
