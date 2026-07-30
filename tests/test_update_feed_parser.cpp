#include "update/Ed25519.h"
#include "update/UpdateFeedParser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QtTest/QtTest>

using namespace trailer;
using namespace trailer::Update;

// Unit tests for the signed-feed parser: the trust boundary between
// "bytes off the network" and "a FeedEntry the rest of the app acts
// on". Every negative case here corresponds to a real attack/failure
// shape: a tampered feed, a feed signed by the wrong key, a downgrade
// attempt, and plain malformed input.
class TestUpdateFeedParser : public QObject {
    Q_OBJECT
  private slots:
    void init();

    void acceptsGenuineFeed();
    void rejectsTamperedPayload();
    void rejectsWrongSigningKey();
    void rejectsMalformedOuterJson();
    void rejectsMissingSignatureField();
    void rejectsMalformedPayloadJson();
    void rejectsEmptyEntries();
    void picksHighestBuildNumberNotArrayOrder();
    void ignoresMissingPlatformAssets();

    void isBuildNewerRejectsDowngrade();
    void isBuildNewerRejectsEqualBuild();
    void isBuildNewerAcceptsStrictIncrease();

  private:
    Ed25519::TestKeypair m_kp;

    // Builds a signed feed byte blob from a payload JSON object, signed
    // with `secretKey` (defaults to the fixture keypair; tests that want
    // a "wrong key" scenario pass a different one).
    QByteArray buildSignedFeed(const QJsonObject &payload,
                              const std::array<unsigned char, 64> &secretKey);
};

void TestUpdateFeedParser::init() {
    m_kp = Ed25519::generateKeypairForTesting();
}

QByteArray TestUpdateFeedParser::buildSignedFeed(const QJsonObject &payload,
                                                 const std::array<unsigned char, 64> &secretKey) {
    const QByteArray payloadBytes = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    const QByteArray sig = Ed25519::signForTesting(payloadBytes, secretKey);
    QJsonObject outer;
    outer[QStringLiteral("payload")] = QString::fromUtf8(payloadBytes);
    outer[QStringLiteral("signature")] = QString::fromLatin1(sig.toBase64());
    return QJsonDocument(outer).toJson(QJsonDocument::Compact);
}

namespace {

QJsonObject makeEntry(const QString &tag, qint64 buildNumber) {
    QJsonObject assets;
    QJsonObject macos;
    macos[QStringLiteral("url")] = QStringLiteral("https://example.invalid/trailer.dmg");
    macos[QStringLiteral("sha256")] =
        QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    assets[QStringLiteral("macos")] = macos;

    QJsonObject entry;
    entry[QStringLiteral("tag")] = tag;
    entry[QStringLiteral("build_number")] = buildNumber;
    entry[QStringLiteral("published_at")] = QStringLiteral("2026-07-30T10:00:00Z");
    entry[QStringLiteral("notes")] = QStringLiteral("test build");
    entry[QStringLiteral("assets")] = assets;
    return entry;
}

QJsonObject makePayload(const QJsonArray &entries) {
    QJsonObject payload;
    payload[QStringLiteral("channel")] = QStringLiteral("nightly");
    payload[QStringLiteral("entries")] = entries;
    return payload;
}

} // namespace

void TestUpdateFeedParser::acceptsGenuineFeed() {
    const QJsonObject payload = makePayload({makeEntry(QStringLiteral("nightly-20260730"), 4821)});
    const QByteArray feed = buildSignedFeed(payload, m_kp.secretKey);

    const ParsedFeed parsed = parseAndVerifyFeed(feed, m_kp.publicKey);
    QVERIFY2(parsed.ok, qPrintable(parsed.error));
    QCOMPARE(parsed.entry.tag, QStringLiteral("nightly-20260730"));
    QCOMPARE(parsed.entry.buildNumber, qint64(4821));
    QCOMPARE(parsed.entry.macosAssetUrl, QStringLiteral("https://example.invalid/trailer.dmg"));
}

void TestUpdateFeedParser::rejectsTamperedPayload() {
    const QJsonObject payload = makePayload({makeEntry(QStringLiteral("nightly-20260730"), 4821)});
    QByteArray feed = buildSignedFeed(payload, m_kp.secretKey);

    // Flip the build number that a downgrade/upgrade-spoofing attacker
    // would want to change, without re-signing — this must fail
    // verification, not silently parse the tampered value.
    feed.replace("4821", "9999999");

    const ParsedFeed parsed = parseAndVerifyFeed(feed, m_kp.publicKey);
    QVERIFY(!parsed.ok);
    QVERIFY(!parsed.error.isEmpty());
}

void TestUpdateFeedParser::rejectsWrongSigningKey() {
    const QJsonObject payload = makePayload({makeEntry(QStringLiteral("nightly-20260730"), 4821)});
    const Ed25519::TestKeypair attackerKp = Ed25519::generateKeypairForTesting();
    // Signed by an attacker's key but checked against OUR embedded
    // public key — must fail, exactly the "compromised pipeline"
    // scenario the whole feature exists to prevent.
    const QByteArray feed = buildSignedFeed(payload, attackerKp.secretKey);

    const ParsedFeed parsed = parseAndVerifyFeed(feed, m_kp.publicKey);
    QVERIFY(!parsed.ok);
}

void TestUpdateFeedParser::rejectsMalformedOuterJson() {
    const ParsedFeed parsed = parseAndVerifyFeed(QByteArray("{ not json at all"), m_kp.publicKey);
    QVERIFY(!parsed.ok);
    QVERIFY(!parsed.error.isEmpty());
}

void TestUpdateFeedParser::rejectsMissingSignatureField() {
    QJsonObject outer;
    outer[QStringLiteral("payload")] = QStringLiteral("{}");
    const QByteArray feed = QJsonDocument(outer).toJson(QJsonDocument::Compact);
    const ParsedFeed parsed = parseAndVerifyFeed(feed, m_kp.publicKey);
    QVERIFY(!parsed.ok);
}

void TestUpdateFeedParser::rejectsMalformedPayloadJson() {
    // The OUTER envelope is well-formed and genuinely signed — but the
    // payload string inside it is not valid JSON. Signature verification
    // succeeds (the bytes are exactly what was signed); the payload
    // parse must still fail cleanly rather than crash or default-construct
    // an entry.
    const QByteArray payloadBytes = "not valid json {{{";
    const QByteArray sig = Ed25519::signForTesting(payloadBytes, m_kp.secretKey);
    QJsonObject outer;
    outer[QStringLiteral("payload")] = QString::fromUtf8(payloadBytes);
    outer[QStringLiteral("signature")] = QString::fromLatin1(sig.toBase64());
    const QByteArray feed = QJsonDocument(outer).toJson(QJsonDocument::Compact);

    const ParsedFeed parsed = parseAndVerifyFeed(feed, m_kp.publicKey);
    QVERIFY(!parsed.ok);
}

void TestUpdateFeedParser::rejectsEmptyEntries() {
    const QJsonObject payload = makePayload(QJsonArray{});
    const QByteArray feed = buildSignedFeed(payload, m_kp.secretKey);
    const ParsedFeed parsed = parseAndVerifyFeed(feed, m_kp.publicKey);
    QVERIFY(!parsed.ok);
}

void TestUpdateFeedParser::picksHighestBuildNumberNotArrayOrder() {
    // Older entry listed FIRST — the parser must not just take
    // entries[0].
    QJsonArray entries;
    entries.append(makeEntry(QStringLiteral("nightly-20260701"), 100));
    entries.append(makeEntry(QStringLiteral("nightly-20260730"), 500));
    const QJsonObject payload = makePayload(entries);
    const QByteArray feed = buildSignedFeed(payload, m_kp.secretKey);

    const ParsedFeed parsed = parseAndVerifyFeed(feed, m_kp.publicKey);
    QVERIFY(parsed.ok);
    QCOMPARE(parsed.entry.buildNumber, qint64(500));
    QCOMPARE(parsed.entry.tag, QStringLiteral("nightly-20260730"));
}

void TestUpdateFeedParser::ignoresMissingPlatformAssets() {
    QJsonObject entry;
    entry[QStringLiteral("tag")] = QStringLiteral("nightly-20260730");
    entry[QStringLiteral("build_number")] = 4821;
    entry[QStringLiteral("assets")] = QJsonObject{}; // no platforms published this run
    const QJsonObject payload = makePayload({entry});
    const QByteArray feed = buildSignedFeed(payload, m_kp.secretKey);

    const ParsedFeed parsed = parseAndVerifyFeed(feed, m_kp.publicKey);
    QVERIFY(parsed.ok);
    QVERIFY(!parsed.entry.hasAssetForCurrentPlatform());
}

void TestUpdateFeedParser::isBuildNewerRejectsDowngrade() {
    QVERIFY(!isBuildNewer(100, 500));
}

void TestUpdateFeedParser::isBuildNewerRejectsEqualBuild() {
    QVERIFY(!isBuildNewer(500, 500));
}

void TestUpdateFeedParser::isBuildNewerAcceptsStrictIncrease() {
    QVERIFY(isBuildNewer(501, 500));
}

QTEST_MAIN(TestUpdateFeedParser)
#include "test_update_feed_parser.moc"
