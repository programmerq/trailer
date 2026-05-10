#pragma once

#include <QObject>
#include <QString>

class QFile;
class QNetworkAccessManager;
class QNetworkReply;

namespace trailer {

// Streams a single file over HTTP(S) into a local path, reporting
// progress and verifying SHA256 at the end. One downloader serves one
// request at a time — create fresh instances for concurrent fetches.
//
// Lives in its own class so ModelRegistry stays testable without a
// real network (the registry treats a downloader as an injected
// dependency via `setDownloader`).
class ModelDownloader : public QObject {
    Q_OBJECT

  public:
    explicit ModelDownloader(QObject *parent = nullptr);
    ~ModelDownloader() override;

    // Kick off a download. Returns immediately; watch the signals for
    // completion. If a download is already in flight this is a no-op.
    //
    // `expectedSha256` is the lowercase hex digest the file must match
    // after download; mismatch is a failure. Pass an empty string to
    // skip verification (not recommended — used only by tests).
    void start(const QString &url, const QString &destPath, const QString &expectedSha256);

    // Abort whatever is in flight, if anything. Safe to call at any time.
    void cancel();

    bool isActive() const { return m_reply != nullptr; }

  signals:
    // Fired periodically while bytes arrive. `total` is -1 if the
    // server didn't send Content-Length.
    void progress(qint64 received, qint64 total);

    // Fired once when the file lands on disk and hashes OK.
    void finished(const QString &destPath);

    // Fired once on any failure path: network error, bad hash, write
    // failure, cancellation. `message` is human-readable.
    void failed(const QString &message);

  private slots:
    void onReadyRead();
    void onFinished();
    void onProgress(qint64 rec, qint64 total);

  private:
    void emitFailure(const QString &message);
    void cleanup();

    QNetworkAccessManager *m_net = nullptr;
    QNetworkReply *m_reply = nullptr;
    QString m_destPath;
    QString m_tempPath;
    QString m_expectedSha256;
    QFile *m_sink = nullptr;
};

} // namespace trailer
