#include "UpdateChecker.h"

#include "UpdateFeedParser.h"
#include "UpdatePublicKey.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace trailer::Update {

namespace {

// Public, unauthenticated GitHub REST endpoint — no token, no account
// (PHILOSOPHY "no accounts"). Rate-limited to 60 req/hr per source IP
// for unauthenticated callers, which a user-initiated "Check for
// Updates…" plus an opt-in once-daily auto-check stays comfortably
// under.
QString releasesListUrl() {
    return QStringLiteral("https://api.github.com/repos/programmerq/trailer/releases?per_page=10");
}

const char *kAppcastAssetName = "appcast-nightly.json";

} // namespace

UpdateChecker::UpdateChecker(QObject *parent)
    : QObject(parent), m_net(new QNetworkAccessManager(this)) {}

UpdateChecker::~UpdateChecker() {
    cancel();
}

void UpdateChecker::checkNow() {
    if (isActive())
        return;
    const QString url = releasesListUrl();
    emit checkStarted(url);

    QNetworkRequest req{QUrl(url)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setRawHeader("User-Agent", "Trailer-UpdateChecker/1.0 (+https://github.com/programmerq/trailer)");
    m_releasesReply = m_net->get(req);
    connect(m_releasesReply, &QNetworkReply::finished, this, &UpdateChecker::onReleasesListFinished);
}

void UpdateChecker::onReleasesListFinished() {
    if (!m_releasesReply)
        return;
    QNetworkReply *reply = m_releasesReply;
    m_releasesReply = nullptr;

    if (reply->error() != QNetworkReply::NoError) {
        const QString err = reply->errorString();
        reply->deleteLater();
        failCheck(tr("Could not reach GitHub: %1").arg(err));
        return;
    }
    const QByteArray body = reply->readAll();
    reply->deleteLater();

    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isArray()) {
        failCheck(tr("Unexpected response listing releases."));
        return;
    }

    // Find the newest prerelease tagged nightly-*. GitHub returns
    // releases newest-first, but scan defensively rather than trusting
    // ordering.
    QString feedUrl;
    for (const QJsonValue &v : doc.array()) {
        if (!v.isObject())
            continue;
        const QJsonObject rel = v.toObject();
        if (!rel.value(QStringLiteral("prerelease")).toBool(false))
            continue;
        const QString tag = rel.value(QStringLiteral("tag_name")).toString();
        if (!tag.startsWith(QStringLiteral("nightly-")))
            continue;
        for (const QJsonValue &av : rel.value(QStringLiteral("assets")).toArray()) {
            const QJsonObject asset = av.toObject();
            if (asset.value(QStringLiteral("name")).toString() == QLatin1String(kAppcastAssetName)) {
                feedUrl = asset.value(QStringLiteral("browser_download_url")).toString();
                break;
            }
        }
        if (!feedUrl.isEmpty())
            break;
    }

    if (feedUrl.isEmpty()) {
        failCheck(tr("No nightly release with a signed update feed was found."));
        return;
    }

    QNetworkRequest req{QUrl(feedUrl)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setRawHeader("User-Agent", "Trailer-UpdateChecker/1.0 (+https://github.com/programmerq/trailer)");
    m_feedReply = m_net->get(req);
    connect(m_feedReply, &QNetworkReply::finished, this, &UpdateChecker::onFeedFinished);
}

void UpdateChecker::onFeedFinished() {
    if (!m_feedReply)
        return;
    QNetworkReply *reply = m_feedReply;
    m_feedReply = nullptr;

    if (reply->error() != QNetworkReply::NoError) {
        const QString err = reply->errorString();
        reply->deleteLater();
        failCheck(tr("Could not download the update feed: %1").arg(err));
        return;
    }
    const QByteArray body = reply->readAll();
    reply->deleteLater();

    // Verify BEFORE trusting anything in the feed — see
    // UpdateFeedParser.h for why this ordering matters.
    const ParsedFeed parsed = parseAndVerifyFeed(body, kNightlyPublicKey);
    if (!parsed.ok) {
        failCheck(parsed.error);
        return;
    }
    emit updateAvailable(parsed.entry);
}

void UpdateChecker::downloadArtifact(const QString &url, const QString &destPath,
                                     const QString &expectedSha256Hex) {
    if (isActive())
        return;
    m_downloadDestPath = destPath;
    m_downloadTempPath = destPath + QLatin1String(".part");
    m_downloadExpectedSha256 = expectedSha256Hex.toLower();

    QFileInfo info(destPath);
    if (!QDir().mkpath(info.absolutePath())) {
        failDownload(tr("Cannot create directory: %1").arg(info.absolutePath()));
        return;
    }
    m_downloadSink = new QFile(m_downloadTempPath, this);
    if (!m_downloadSink->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const QString err = m_downloadSink->errorString();
        delete m_downloadSink;
        m_downloadSink = nullptr;
        failDownload(tr("Cannot open %1 for writing: %2").arg(m_downloadTempPath, err));
        return;
    }

    emit downloadStarted(url);
    QNetworkRequest req{QUrl(url)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setRawHeader("User-Agent", "Trailer-UpdateChecker/1.0 (+https://github.com/programmerq/trailer)");
    m_downloadReply = m_net->get(req);
    connect(m_downloadReply, &QNetworkReply::readyRead, this, &UpdateChecker::onDownloadReadyRead);
    connect(m_downloadReply, &QNetworkReply::finished, this, &UpdateChecker::onDownloadFinished);
    connect(m_downloadReply, &QNetworkReply::downloadProgress, this,
            &UpdateChecker::onDownloadProgress);
}

void UpdateChecker::onDownloadReadyRead() {
    if (!m_downloadReply || !m_downloadSink)
        return;
    const QByteArray chunk = m_downloadReply->readAll();
    if (!chunk.isEmpty())
        m_downloadSink->write(chunk);
}

void UpdateChecker::onDownloadProgress(qint64 received, qint64 total) {
    emit downloadProgress(received, total);
}

void UpdateChecker::onDownloadFinished() {
    if (m_downloadReply && m_downloadSink) {
        const QByteArray chunk = m_downloadReply->readAll();
        if (!chunk.isEmpty())
            m_downloadSink->write(chunk);
    }
    if (!m_downloadReply)
        return;

    if (m_downloadReply->error() != QNetworkReply::NoError) {
        const QString err = m_downloadReply->errorString();
        failDownload(tr("Download failed: %1").arg(err));
        return;
    }
    if (m_downloadSink) {
        m_downloadSink->flush();
        m_downloadSink->close();
    }

    // The downloaded artifact is verified against the SHA256 the signed
    // feed committed to — this is the second half of the trust chain:
    // the feed's signature proves the feed is ours, and the hash proves
    // the bytes on disk are exactly what the feed named. Never skipped
    // in production (UpdateManager always supplies a real hash from a
    // verified FeedEntry).
    if (!m_downloadExpectedSha256.isEmpty()) {
        QFile file(m_downloadTempPath);
        if (!file.open(QIODevice::ReadOnly)) {
            failDownload(tr("Cannot read downloaded file for hashing"));
            return;
        }
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!hash.addData(&file)) {
            failDownload(tr("Failed to hash downloaded file"));
            return;
        }
        file.close();
        const QString got = QString::fromLatin1(hash.result().toHex());
        if (got.toLower() != m_downloadExpectedSha256) {
            failDownload(tr("Downloaded file does not match the signed feed's SHA256 "
                            "(expected %1, got %2) — refusing to install it.")
                             .arg(m_downloadExpectedSha256, got));
            return;
        }
    }

    QFile::remove(m_downloadDestPath);
    if (!QFile::rename(m_downloadTempPath, m_downloadDestPath)) {
        failDownload(tr("Cannot move %1 to %2").arg(m_downloadTempPath, m_downloadDestPath));
        return;
    }
    const QString finalPath = m_downloadDestPath;
    cleanupDownload();
    emit downloadFinished(finalPath);
}

void UpdateChecker::cancel() {
    if (m_releasesReply)
        m_releasesReply->abort();
    if (m_feedReply)
        m_feedReply->abort();
    if (m_downloadReply)
        m_downloadReply->abort();
    cleanupDownload();
    m_releasesReply = nullptr;
    m_feedReply = nullptr;
}

void UpdateChecker::failCheck(const QString &message) {
    emit checkFailed(message);
}

void UpdateChecker::failDownload(const QString &message) {
    cleanupDownload();
    emit downloadFailed(message);
}

void UpdateChecker::cleanupDownload() {
    if (m_downloadSink) {
        m_downloadSink->close();
        m_downloadSink->deleteLater();
        m_downloadSink = nullptr;
    }
    if (m_downloadReply) {
        m_downloadReply->deleteLater();
        m_downloadReply = nullptr;
    }
    if (!m_downloadTempPath.isEmpty())
        QFile::remove(m_downloadTempPath);
}

} // namespace trailer::Update
