#include "ModelDownloader.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace trailer {

ModelDownloader::ModelDownloader(QObject* parent)
    : QObject(parent), m_net(new QNetworkAccessManager(this)) {}

ModelDownloader::~ModelDownloader() { cancel(); }

void ModelDownloader::start(const QString& url,
                            const QString& destPath,
                            const QString& expectedSha256) {
    if (m_reply) return;  // already busy
    m_destPath = destPath;
    m_tempPath = destPath + QLatin1String(".part");
    m_expectedSha256 = expectedSha256.toLower();

    QFileInfo info(destPath);
    if (!QDir().mkpath(info.absolutePath())) {
        emitFailure(tr("Cannot create directory: %1").arg(info.absolutePath()));
        return;
    }

    m_sink = new QFile(m_tempPath, this);
    if (!m_sink->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const QString err = m_sink->errorString();
        delete m_sink; m_sink = nullptr;
        emitFailure(tr("Cannot open %1 for writing: %2").arg(m_tempPath, err));
        return;
    }

    QNetworkRequest req{QUrl(url)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setRawHeader("User-Agent", "Trailer/0.1");
    m_reply = m_net->get(req);
    connect(m_reply, &QNetworkReply::readyRead, this, &ModelDownloader::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &ModelDownloader::onFinished);
    connect(m_reply, &QNetworkReply::downloadProgress,
            this, &ModelDownloader::onProgress);
}

void ModelDownloader::cancel() {
    if (m_reply) m_reply->abort();
    cleanup();
}

void ModelDownloader::onReadyRead() {
    if (!m_reply || !m_sink) return;
    const QByteArray chunk = m_reply->readAll();
    if (!chunk.isEmpty()) m_sink->write(chunk);
}

void ModelDownloader::onProgress(qint64 rec, qint64 total) {
    emit progress(rec, total);
}

void ModelDownloader::onFinished() {
    // Drain whatever's still buffered before flushing.
    if (m_reply && m_sink) {
        const QByteArray chunk = m_reply->readAll();
        if (!chunk.isEmpty()) m_sink->write(chunk);
    }

    if (!m_reply) return;

    if (m_reply->error() != QNetworkReply::NoError) {
        const QString err = m_reply->errorString();
        emitFailure(tr("Download failed: %1").arg(err));
        return;
    }

    if (m_sink) {
        m_sink->flush();
        m_sink->close();
    }

    // Verify the SHA256 before moving into place. Skipping verification
    // is an explicit decision (empty expected hash) — used only by
    // tests that serve tiny files from a local URL.
    if (!m_expectedSha256.isEmpty()) {
        QFile file(m_tempPath);
        if (!file.open(QIODevice::ReadOnly)) {
            emitFailure(tr("Cannot read downloaded file for hashing"));
            return;
        }
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!hash.addData(&file)) {
            emitFailure(tr("Failed to hash downloaded file"));
            return;
        }
        file.close();
        const QString got = QString::fromLatin1(hash.result().toHex());
        if (got.toLower() != m_expectedSha256) {
            emitFailure(tr("SHA256 mismatch: expected %1, got %2")
                            .arg(m_expectedSha256, got));
            return;
        }
    }

    // Atomic-ish move: remove any existing file at destPath, then rename
    // the .part file. On POSIX this is atomic; on Windows it's not but
    // it's the best we can do without extra dance.
    QFile::remove(m_destPath);
    if (!QFile::rename(m_tempPath, m_destPath)) {
        emitFailure(tr("Cannot move %1 to %2").arg(m_tempPath, m_destPath));
        return;
    }
    const QString finalPath = m_destPath;
    cleanup();
    emit finished(finalPath);
}

void ModelDownloader::emitFailure(const QString& message) {
    const QString msg = message;
    cleanup();
    emit failed(msg);
}

void ModelDownloader::cleanup() {
    if (m_sink) {
        m_sink->close();
        m_sink->deleteLater();
        m_sink = nullptr;
    }
    if (m_reply) {
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    QFile::remove(m_tempPath);
}

}  // namespace trailer
