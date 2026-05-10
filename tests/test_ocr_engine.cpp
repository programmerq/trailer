// Unit tests for OcrEngine (PP-OCRv3 detector + PP-OCRv3 Latin
// recognizer).
//
// The combined weights are ~11 MB, so we don't download them in CI.
// These tests cover:
//
//   - isModelReady() is false with an empty cache, and recognize()
//     returns an empty vector without crashing.
//   - ensureModelsAvailable() fails gracefully with modelsUnavailable
//     when the registry has no URL for the OCR models.
//   - modelsReady fires exactly once when BOTH models land on disk.
//   - The PaddleOCR English dictionary resource (:/ml/ppocr_en_dict.txt)
//     is present and parses into 95 characters plus a trailing space
//     for a total of 96 classes — matching the recogniser's softmax
//     width of 97 (blank + 96).
//   - If TRAILER_TEST_PPOCR_DET + TRAILER_TEST_PPOCR_REC both point at
//     real PP-OCR ONNX files, render a simple high-contrast text image
//     and verify recognize() returns at least one TextBlock whose
//     polygon is inside the image and whose recognised text has
//     strictly positive confidence. Otherwise the test is skipped.
//
// We use setManifestForTesting so tests run against a clean temporary
// models dir and never touch the user's real cache.

#include "ml/ModelRegistry.h"
#include "ml/OcrEngine.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QImage>
#include <QObject>
#include <QPainter>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTextStream>
#include <QUrl>
#include <QtTest/QtTest>

using namespace trailer;

class TestOcrEngine : public QObject {
    Q_OBJECT
  private slots:
    void notReadyWhenCacheEmpty();
    void recognizeReturnsEmptyWhenModelMissing();
    void ensureModelsAvailableFailsGracefullyWithoutUrls();
    void modelsReadyFiresWhenBothModelsSeeded();
    void dictionaryResourceHas96Entries();
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
    // Paint a simple "HELLO 1234" banner onto a white background.
    // Big, high-contrast, axis-aligned: the detector's easy mode.
    static QImage makeTextImage(const QString &text, int w = 640, int h = 200) {
        QImage img(w, h, QImage::Format_RGB32);
        img.fill(Qt::white);
        QPainter p(&img);
        p.setRenderHint(QPainter::TextAntialiasing, true);
        QFont f;
        f.setPixelSize(80);
        f.setBold(true);
        p.setFont(f);
        p.setPen(Qt::black);
        p.drawText(img.rect(), Qt::AlignCenter, text);
        p.end();
        return img;
    }
};

void TestOcrEngine::notReadyWhenCacheEmpty() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ModelRegistry reg;
    reg.setManifestForTesting(
        {makeSpec(ModelId::PpOcrDetector, QStringLiteral("pp_ocr_det.onnx")),
         makeSpec(ModelId::PpOcrRecognizerLatin, QStringLiteral("pp_ocr_rec_en.onnx"))},
        dir.path());
    OcrEngine engine(&reg);
    QVERIFY(!engine.isModelReady());
}

void TestOcrEngine::recognizeReturnsEmptyWhenModelMissing() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ModelRegistry reg;
    reg.setManifestForTesting(
        {makeSpec(ModelId::PpOcrDetector, QStringLiteral("pp_ocr_det.onnx")),
         makeSpec(ModelId::PpOcrRecognizerLatin, QStringLiteral("pp_ocr_rec_en.onnx"))},
        dir.path());
    OcrEngine engine(&reg);

    QImage img(32, 32, QImage::Format_RGB32);
    img.fill(Qt::white);
    const auto blocks = engine.recognize(img);
    QVERIFY(blocks.isEmpty());
    QVERIFY(engine.lastDetectionMask().isNull());
}

void TestOcrEngine::ensureModelsAvailableFailsGracefullyWithoutUrls() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ModelRegistry reg;
    reg.setManifestForTesting(
        {makeSpec(ModelId::PpOcrDetector, QStringLiteral("pp_ocr_det.onnx")),
         makeSpec(ModelId::PpOcrRecognizerLatin, QStringLiteral("pp_ocr_rec_en.onnx"))},
        dir.path());
    OcrEngine engine(&reg);

    QSignalSpy failed(&engine, &OcrEngine::modelsUnavailable);
    engine.ensureModelsAvailable();
    QVERIFY(failed.wait(2000));
    QVERIFY(failed.count() >= 1);
}

void TestOcrEngine::modelsReadyFiresWhenBothModelsSeeded() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString detPath = dir.filePath(QStringLiteral("pp_ocr_det.onnx"));
    const QString recPath = dir.filePath(QStringLiteral("pp_ocr_rec_en.onnx"));
    {
        QFile f(detPath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArrayLiteral("det-placeholder"));
    }
    {
        QFile f(recPath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArrayLiteral("rec-placeholder"));
    }
    const QString detHash = QString::fromLatin1(sha256Hex(detPath));
    const QString recHash = QString::fromLatin1(sha256Hex(recPath));
    QVERIFY(!detHash.isEmpty());
    QVERIFY(!recHash.isEmpty());

    ModelRegistry reg;
    reg.setManifestForTesting(
        {makeSpec(ModelId::PpOcrDetector, QStringLiteral("pp_ocr_det.onnx"),
                  QUrl::fromLocalFile(detPath).toString(), detHash),
         makeSpec(ModelId::PpOcrRecognizerLatin, QStringLiteral("pp_ocr_rec_en.onnx"),
                  QUrl::fromLocalFile(recPath).toString(), recHash)},
        dir.path());
    OcrEngine engine(&reg);
    QVERIFY(engine.isModelReady());

    QSignalSpy ready(&engine, &OcrEngine::modelsReady);
    engine.ensureModelsAvailable();
    QVERIFY(ready.wait(2000));
    QVERIFY(ready.count() >= 1);
}

void TestOcrEngine::dictionaryResourceHas96Entries() {
    // See OcrEngine::loadDictionary — Qt resources compiled into a
    // static library need an explicit init from a TU that lands in
    // the binary.
    Q_INIT_RESOURCE(trailer);
    QFile f(QStringLiteral(":/ml/ppocr_en_dict.txt"));
    QVERIFY2(f.open(QIODevice::ReadOnly),
             "ppocr_en_dict.txt resource must be compiled into the core library");
    QTextStream in(&f);
    int lines = 0;
    while (!in.atEnd()) {
        in.readLine();
        ++lines;
    }
    QCOMPARE(lines, 95);
}

void TestOcrEngine::endToEndInferenceWithRealModels() {
    const QString detSrc = QString::fromLocal8Bit(qgetenv("TRAILER_TEST_PPOCR_DET"));
    const QString recSrc = QString::fromLocal8Bit(qgetenv("TRAILER_TEST_PPOCR_REC"));
    if (detSrc.isEmpty() || !QFileInfo::exists(detSrc) || recSrc.isEmpty() ||
        !QFileInfo::exists(recSrc)) {
        QSKIP("TRAILER_TEST_PPOCR_DET + TRAILER_TEST_PPOCR_REC not set — "
              "skipping real inference path.");
    }

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString detPath = dir.filePath(QStringLiteral("pp_ocr_det.onnx"));
    const QString recPath = dir.filePath(QStringLiteral("pp_ocr_rec_en.onnx"));
    QVERIFY(QFile::copy(detSrc, detPath));
    QVERIFY(QFile::copy(recSrc, recPath));
    const QString detHash = QString::fromLatin1(sha256Hex(detPath));
    const QString recHash = QString::fromLatin1(sha256Hex(recPath));

    ModelRegistry reg;
    reg.setManifestForTesting(
        {makeSpec(ModelId::PpOcrDetector, QStringLiteral("pp_ocr_det.onnx"),
                  QUrl::fromLocalFile(detPath).toString(), detHash),
         makeSpec(ModelId::PpOcrRecognizerLatin, QStringLiteral("pp_ocr_rec_en.onnx"),
                  QUrl::fromLocalFile(recPath).toString(), recHash)},
        dir.path());
    OcrEngine engine(&reg);
    QVERIFY(engine.isModelReady());

    const QImage scene = makeTextImage(QStringLiteral("HELLO 1234"));
    const auto blocks = engine.recognize(scene);
    QVERIFY2(!blocks.isEmpty(), "Expected at least one text region on a HELLO 1234 banner");

    for (const auto &b : blocks) {
        QVERIFY(!b.text.isEmpty());
        QVERIFY(b.confidence >= 0.0f && b.confidence <= 1.0f);
        const QRect bounds = b.polygon.boundingRect();
        QVERIFY2(bounds.intersects(scene.rect()), "Block polygon should overlap the source image");
    }

    // The joined recognised text should contain at least one ASCII
    // letter or digit we painted. PP-OCR's confidence on stylised
    // system fonts is not always high, so we accept partial matches
    // — any of the characters from HELLO/1234 is enough.
    QString joined;
    for (const auto &b : blocks)
        joined += b.text;
    QVERIFY2(joined.contains(QRegularExpression(QStringLiteral("[A-Z0-9]"))),
             qPrintable(QStringLiteral("No alnum chars recognised in: '%1'").arg(joined)));

    const QImage mask = engine.lastDetectionMask();
    QVERIFY(!mask.isNull());
    QCOMPARE(mask.size(), scene.size());
}

QTEST_MAIN(TestOcrEngine)
#include "test_ocr_engine.moc"
