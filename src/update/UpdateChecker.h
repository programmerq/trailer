#pragma once

#include "UpdateTypes.h"

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;
class QFile;

namespace trailer::Update {

// The ONE file in the update-channel feature that touches the network
// (QNetworkAccessManager / QNetworkReply). Per AGENTS.md's networking
// rule, every other file under src/update/ is pure parsing/verification/
// orchestration with no network types in scope. Mirrors the shape of
// src/ml/ModelDownloader.cpp, the repo's other sanctioned networking
// file: one class, one QNetworkAccessManager, explicit signals for
// progress/finished/failed, nothing implicit.
//
// checkNow() does a two-step, fully public/unauthenticated GET chain
// against the GitHub REST API (no token, no account, matches PHILOSOPHY
// "no accounts"):
//   1. GET /repos/<owner>/<repo>/releases?per_page=10 — find the newest
//      prerelease whose tag starts "nightly-".
//   2. GET that release's "appcast-nightly.json" asset (published by
//      nightly.yml, see docs/decision-records/2026-07-30-nightly-auto-
//      update-channel.md) and hand the bytes to
//      UpdateFeedParser::parseAndVerifyFeed BEFORE trusting anything in
//      it.
//
// Every outbound URL this class will fetch is disclosed to the caller
// via requestingUrl() before the request fires, mirroring
// ModelDownloader's URL-before-download consent framing — UpdateManager
// surfaces that URL in the manual "Check for Updates…" flow.
class UpdateChecker : public QObject {
    Q_OBJECT

  public:
    explicit UpdateChecker(QObject *parent = nullptr);
    ~UpdateChecker() override;

    // Kicks off the two-step check described above. No-op if a check or
    // download is already in flight.
    void checkNow();

    // Downloads `url` to `destPath`, verifying the SHA256 against
    // `expectedSha256Hex` (lowercase hex, from the verified feed entry —
    // never skippable in production; UpdateManager always supplies a
    // real hash). No-op if a check or download is already in flight.
    void downloadArtifact(const QString &url, const QString &destPath,
                          const QString &expectedSha256Hex);

    void cancel();
    bool isActive() const { return m_releasesReply != nullptr || m_feedReply != nullptr ||
                                    m_downloadReply != nullptr; }

  signals:
    void checkStarted(QString url);
    void updateAvailable(trailer::Update::FeedEntry entry);
    void upToDate();
    void checkFailed(QString message);

    void downloadStarted(QString url);
    void downloadProgress(qint64 received, qint64 total);
    void downloadFinished(QString destPath);
    void downloadFailed(QString message);

  private:
    void onReleasesListFinished();
    void onFeedFinished();
    void onDownloadReadyRead();
    void onDownloadFinished();
    void onDownloadProgress(qint64 received, qint64 total);

    void failCheck(const QString &message);
    void failDownload(const QString &message);
    void cleanupDownload();

    QNetworkAccessManager *m_net = nullptr;
    QNetworkReply *m_releasesReply = nullptr;
    QNetworkReply *m_feedReply = nullptr;
    QNetworkReply *m_downloadReply = nullptr;

    QFile *m_downloadSink = nullptr;
    QString m_downloadDestPath;
    QString m_downloadTempPath;
    QString m_downloadExpectedSha256;
};

} // namespace trailer::Update
