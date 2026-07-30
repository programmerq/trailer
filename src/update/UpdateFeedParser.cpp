#include "UpdateFeedParser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

namespace trailer::Update {

namespace {

QString notNewer(const char *why) {
    return QStringLiteral("Update feed rejected: %1").arg(QString::fromLatin1(why));
}

std::optional<FeedEntry> parseEntry(const QJsonObject &obj, Channel channel) {
    if (!obj.contains(QStringLiteral("tag")) || !obj.contains(QStringLiteral("build_number")))
        return std::nullopt;

    FeedEntry entry;
    entry.channel = channel;
    entry.tag = obj.value(QStringLiteral("tag")).toString();
    entry.buildNumber = static_cast<qint64>(obj.value(QStringLiteral("build_number")).toDouble(-1));
    if (entry.tag.isEmpty() || entry.buildNumber < 0)
        return std::nullopt;
    entry.notes = obj.value(QStringLiteral("notes")).toString();
    entry.publishedAtUtc = obj.value(QStringLiteral("published_at")).toString();

    const QJsonObject assets = obj.value(QStringLiteral("assets")).toObject();
    const auto readAsset = [&](const char *key, QString &urlOut, QString &shaOut) {
        const QJsonObject a = assets.value(QLatin1String(key)).toObject();
        urlOut = a.value(QStringLiteral("url")).toString();
        shaOut = a.value(QStringLiteral("sha256")).toString().toLower();
    };
    readAsset("macos", entry.macosAssetUrl, entry.macosSha256);
    readAsset("windows", entry.windowsAssetUrl, entry.windowsSha256);
    readAsset("linux", entry.linuxAssetUrl, entry.linuxSha256);

    return entry;
}

} // namespace

ParsedFeed parseAndVerifyFeed(const QByteArray &rawFeedBytes,
                              const std::array<unsigned char, Ed25519::kPublicKeyBytes> &publicKey) {
    ParsedFeed result;

    QJsonParseError outerErr{};
    const QJsonDocument outerDoc = QJsonDocument::fromJson(rawFeedBytes, &outerErr);
    if (outerErr.error != QJsonParseError::NoError || !outerDoc.isObject()) {
        result.error = notNewer("outer JSON is malformed");
        return result;
    }
    const QJsonObject outer = outerDoc.object();
    if (!outer.contains(QStringLiteral("payload")) || !outer.contains(QStringLiteral("signature"))) {
        result.error = notNewer("missing payload or signature field");
        return result;
    }
    const QString payloadString = outer.value(QStringLiteral("payload")).toString();
    const QByteArray signature =
        QByteArray::fromBase64(outer.value(QStringLiteral("signature")).toString().toUtf8());
    if (payloadString.isEmpty() || signature.isEmpty()) {
        result.error = notNewer("empty payload or signature");
        return result;
    }

    // The bytes that were signed are EXACTLY the UTF-8 encoding of the
    // decoded payload string — not any re-serialization of it. This is
    // the whole point of verifying before parsing: nothing about
    // `payloadString`'s *content* is trusted until this call returns
    // true.
    const QByteArray payloadBytes = payloadString.toUtf8();
    if (!Ed25519::verify(payloadBytes, signature, publicKey)) {
        result.error = notNewer("signature does not verify (tampered feed or wrong key)");
        return result;
    }

    // Only now — after cryptographic verification — do we parse and
    // trust any field inside the payload.
    QJsonParseError payloadErr{};
    const QJsonDocument payloadDoc = QJsonDocument::fromJson(payloadBytes, &payloadErr);
    if (payloadErr.error != QJsonParseError::NoError || !payloadDoc.isObject()) {
        result.error = notNewer("verified payload is not valid JSON");
        return result;
    }
    const QJsonObject payload = payloadDoc.object();
    const Channel channel = channelFromString(payload.value(QStringLiteral("channel")).toString());
    const QJsonArray entries = payload.value(QStringLiteral("entries")).toArray();
    if (entries.isEmpty()) {
        result.error = notNewer("feed has no entries");
        return result;
    }

    // Pick the entry with the highest build_number — the generator is
    // expected to write newest-first, but this parser doesn't rely on
    // array order.
    bool found = false;
    FeedEntry best;
    for (const QJsonValue &v : entries) {
        if (!v.isObject())
            continue;
        auto parsed = parseEntry(v.toObject(), channel);
        if (!parsed)
            continue;
        if (!found || parsed->buildNumber > best.buildNumber) {
            best = *parsed;
            found = true;
        }
    }
    if (!found) {
        result.error = notNewer("no well-formed entry in feed");
        return result;
    }

    result.ok = true;
    result.entry = best;
    return result;
}

} // namespace trailer::Update
