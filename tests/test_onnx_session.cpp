#include "ml/OnnxSession.h"

#include <QByteArray>
#include <QFile>
#include <QObject>
#include <QtTest/QtTest>

using namespace trailer;

// Smoke tests for OnnxSession. We use the tiny identity model in
// tests/fixtures/ so these tests run with zero network access and in
// under a millisecond. Feature-specific runs (u2netp, MobileSAM, OCR)
// live in their own suites and skip when their models aren't cached.

class TestOnnxSession : public QObject {
    Q_OBJECT
  private slots:
    void loadFromFile();
    void loadFromBytes();
    void inputsAndOutputsReported();
    void identityRoundTripsFloats();
    void malformedBytesReturnNullptr();
    void emptyBytesReturnNullptr();
    void mismatchedElementCountRejected();

  private:
    QString fixturePath() const;
};

QString TestOnnxSession::fixturePath() const {
    // test_onnx_session runs with cwd = build/tests/. The fixtures
    // directory is a stable location alongside the source tree.
    const QString rel = QStringLiteral("fixtures/identity.onnx");
    const QString candidate = QFINDTESTDATA(rel);
    return candidate;
}

void TestOnnxSession::loadFromFile() {
    const QString path = fixturePath();
    QVERIFY2(!path.isEmpty(), "fixture identity.onnx not locatable via QFINDTESTDATA");
    auto s = OnnxSession::fromFile(path);
    QVERIFY(s != nullptr);
}

void TestOnnxSession::loadFromBytes() {
    QFile f(fixturePath());
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray bytes = f.readAll();
    QVERIFY(!bytes.isEmpty());
    auto s = OnnxSession::fromBytes(bytes);
    QVERIFY(s != nullptr);
}

void TestOnnxSession::inputsAndOutputsReported() {
    auto s = OnnxSession::fromFile(fixturePath());
    QVERIFY(s);
    const auto inputs = s->inputNames();
    const auto outputs = s->outputNames();
    QCOMPARE(inputs.size(), 1);
    QCOMPARE(outputs.size(), 1);
    QCOMPARE(inputs.front(), QStringLiteral("input"));
    QCOMPARE(outputs.front(), QStringLiteral("output"));
}

void TestOnnxSession::identityRoundTripsFloats() {
    auto s = OnnxSession::fromFile(fixturePath());
    QVERIFY(s);
    std::vector<float> buf = {1.5f, -3.25f, 0.0f, 17.0f};
    TensorSpec in;
    in.name = QByteArrayLiteral("input");
    in.data = buf.data();
    in.shape = {static_cast<int64_t>(buf.size())};
    in.elementCount = static_cast<qsizetype>(buf.size());
    const auto outputs = s->run({in});
    QVERIFY(outputs.has_value());
    QCOMPARE(outputs->size(), 1u);
    const auto &r = outputs->front();
    QCOMPARE(r.name, QByteArrayLiteral("output"));
    QCOMPARE(r.data.size(), buf.size());
    for (size_t i = 0; i < buf.size(); ++i) {
        QCOMPARE(r.data[i], buf[i]);
    }
}

void TestOnnxSession::malformedBytesReturnNullptr() {
    // Garbage bytes must not crash; ORT throws and we catch.
    QByteArray junk = QByteArrayLiteral("not an onnx model at all");
    auto s = OnnxSession::fromBytes(junk);
    QVERIFY(s == nullptr);
}

void TestOnnxSession::emptyBytesReturnNullptr() {
    auto s = OnnxSession::fromBytes(QByteArray{});
    QVERIFY(s == nullptr);
}

void TestOnnxSession::mismatchedElementCountRejected() {
    auto s = OnnxSession::fromFile(fixturePath());
    QVERIFY(s);
    std::vector<float> buf = {1.0f, 2.0f};
    TensorSpec in;
    in.name = QByteArrayLiteral("input");
    in.data = buf.data();
    in.shape = {5}; // claim 5 elements but only provide 2
    in.elementCount = 2;
    const auto outputs = s->run({in});
    QVERIFY(!outputs.has_value());
}

QTEST_MAIN(TestOnnxSession)
#include "test_onnx_session.moc"
