#include "update/Ed25519.h"

#include <QObject>
#include <QtTest/QtTest>

using namespace trailer;

// Unit tests for the ed25519 verify wrapper around vendored TweetNaCl
// (third_party/tweetnacl). Keys/signatures here are throwaway,
// test-generated — see Ed25519.h's test-only helper comment.
class TestEd25519 : public QObject {
    Q_OBJECT
  private slots:
    void verifyAcceptsGenuineSignature();
    void verifyRejectsTamperedMessage();
    void verifyRejectsWrongKey();
    void verifyRejectsTruncatedSignature();
    void verifyRejectsWrongLengthSignature();
    void verifyRejectsEmptyMessageMismatch();
    void differentKeypairsProduceDifferentSignatures();
};

void TestEd25519::verifyAcceptsGenuineSignature() {
    const auto kp = Ed25519::generateKeypairForTesting();
    const QByteArray message = "hello, nightly channel";
    const QByteArray sig = Ed25519::signForTesting(message, kp.secretKey);
    QVERIFY(Ed25519::verify(message, sig, kp.publicKey));
}

void TestEd25519::verifyRejectsTamperedMessage() {
    const auto kp = Ed25519::generateKeypairForTesting();
    const QByteArray message = "the original payload";
    const QByteArray sig = Ed25519::signForTesting(message, kp.secretKey);
    const QByteArray tampered = "the ORIGINAL payload"; // one bit flip's worth of change
    QVERIFY(!Ed25519::verify(tampered, sig, kp.publicKey));
}

void TestEd25519::verifyRejectsWrongKey() {
    const auto kp = Ed25519::generateKeypairForTesting();
    const auto otherKp = Ed25519::generateKeypairForTesting();
    const QByteArray message = "signed by kp, checked against otherKp";
    const QByteArray sig = Ed25519::signForTesting(message, kp.secretKey);
    QVERIFY(!Ed25519::verify(message, sig, otherKp.publicKey));
}

void TestEd25519::verifyRejectsTruncatedSignature() {
    const auto kp = Ed25519::generateKeypairForTesting();
    const QByteArray message = "message";
    QByteArray sig = Ed25519::signForTesting(message, kp.secretKey);
    sig.chop(1);
    QVERIFY(!Ed25519::verify(message, sig, kp.publicKey));
}

void TestEd25519::verifyRejectsWrongLengthSignature() {
    const auto kp = Ed25519::generateKeypairForTesting();
    const QByteArray message = "message";
    const QByteArray garbage(128, '\x01');
    QVERIFY(!Ed25519::verify(message, garbage, kp.publicKey));
}

void TestEd25519::verifyRejectsEmptyMessageMismatch() {
    const auto kp = Ed25519::generateKeypairForTesting();
    const QByteArray sig = Ed25519::signForTesting(QByteArray(), kp.secretKey);
    QVERIFY(Ed25519::verify(QByteArray(), sig, kp.publicKey));
    QVERIFY(!Ed25519::verify(QByteArray("not empty"), sig, kp.publicKey));
}

void TestEd25519::differentKeypairsProduceDifferentSignatures() {
    const auto kp1 = Ed25519::generateKeypairForTesting();
    const auto kp2 = Ed25519::generateKeypairForTesting();
    QVERIFY(kp1.publicKey != kp2.publicKey);
}

QTEST_MAIN(TestEd25519)
#include "test_ed25519.moc"
