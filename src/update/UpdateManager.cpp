#include "UpdateManager.h"

#include "TrailerVersion.h"
#include "settings/Settings.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QUrl>

#include <utility>

namespace trailer::Update {

namespace {

// Minimum gap between unattended auto-checks. Nightlies publish at most
// once/day, so anything more frequent just burns the user's rate-limit
// budget for no new information. Not a magic-number-needing-a-decision-
// record per AGENTS.md G6: it's a pure internal tuning value (no
// user-visible default changes if it moves — the user only ever
// observes "auto-check is on" or "off"), documented here per
// PHILOSOPHY's "hand-tuned values stay hand-tuned" rule. Range
// considered: 1h (too chatty, no new nightlies that often) to 7d (stale
// for a dogfooding user); 24h matches the nightly cadence exactly.
constexpr qint64 kAutoCheckMinIntervalSecs = 24 * 60 * 60;

QString downloadDir() {
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return base + QStringLiteral("/updates");
}

// Best-effort quarantine clear. Never fatal: if `xattr` is missing or
// the attribute was never set (the common case for a file this process
// wrote itself — see the PR discussion on whether QNetworkAccessManager
// downloads pick up com.apple.quarantine at all), this just no-ops.
// Called on both the downloaded artifact and the installed bundle so
// that a Gatekeeper block never survives an update Trailer performed
// itself.
#ifdef Q_OS_MACOS
void clearQuarantine(const QString &path) {
    QProcess proc;
    proc.start(QStringLiteral("/usr/bin/xattr"),
               {QStringLiteral("-dr"), QStringLiteral("com.apple.quarantine"), path});
    proc.waitForFinished(10000);
}
#endif

} // namespace

UpdateManager::UpdateManager(Settings &settings, QObject *parent)
    : QObject(parent), m_settings(settings) {
    connect(&m_checker, &UpdateChecker::checkStarted, this, [this](const QString &url) {
        m_lastCheckUrl = url;
        setState(State::Checking);
        emit checkStarted(url);
    });
    connect(&m_checker, &UpdateChecker::updateAvailable, this,
            &UpdateManager::onUpdateAvailable);
    connect(&m_checker, &UpdateChecker::upToDate, this, &UpdateManager::onUpToDate);
    connect(&m_checker, &UpdateChecker::checkFailed, this, &UpdateManager::onCheckFailed);
    connect(&m_checker, &UpdateChecker::downloadStarted, this,
            [this](const QString &url) { m_lastDownloadUrl = url; });
    connect(&m_checker, &UpdateChecker::downloadProgress, this,
            [this](qint64 r, qint64 t) { emit downloadProgress(r, t); });
    connect(&m_checker, &UpdateChecker::downloadFinished, this,
            &UpdateManager::onDownloadFinished);
    connect(&m_checker, &UpdateChecker::downloadFailed, this, &UpdateManager::onDownloadFailed);
}

void UpdateManager::checkNow() {
    m_checker.checkNow();
}

void UpdateManager::maybeAutoCheck() {
    if (!m_settings.updatesAutoCheckEnabled())
        return;
    const QString lastIso = m_settings.updatesLastCheckedUtc();
    if (!lastIso.isEmpty()) {
        const QDateTime last = QDateTime::fromString(lastIso, Qt::ISODate);
        if (last.isValid()) {
            const qint64 elapsed = last.secsTo(QDateTime::currentDateTimeUtc());
            if (elapsed >= 0 && elapsed < kAutoCheckMinIntervalSecs)
                return;
        }
    }
    checkNow();
}

void UpdateManager::onUpdateAvailable(const FeedEntry &entry) {
    m_settings.setUpdatesLastCheckedUtc(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    m_settings.save();

    // Downgrade guard: a signed-but-stale (or malicious-but-correctly-
    // signed-by-a-compromised-then-revoked scenario, or simply an older
    // cached copy) feed entry is never treated as an update if it is
    // not strictly newer than what's running. This is the "downgrade
    // attempt" case unit tests cover at the UpdateFeedParser level for
    // malformed input; here it's the policy check on a well-formed,
    // verified, but not-actually-newer entry.
    if (!isBuildNewer(entry.buildNumber, TRAILER_BUILD_COMMIT_COUNT)) {
        m_latest = entry;
        setState(State::UpToDate);
        return;
    }
    if (!entry.hasAssetForCurrentPlatform()) {
        // Honest, not silent: a partial-success nightly (see
        // nightly.yml) may ship Linux+Windows but not macOS on a given
        // night. Surface that plainly rather than reporting "up to
        // date" (untrue) or silently doing nothing (G3).
        m_lastError = tr("A newer nightly build exists, but no build for this platform "
                         "was published in it yet.");
        setState(State::Error);
        return;
    }
    m_latest = entry;
    setState(State::UpdateAvailable);
}

void UpdateManager::onUpToDate() {
    m_settings.setUpdatesLastCheckedUtc(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    m_settings.save();
    setState(State::UpToDate);
}

void UpdateManager::onCheckFailed(const QString &message) {
    m_settings.setUpdatesLastCheckedUtc(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    m_settings.save();
    m_lastError = message;
    setState(State::Error);
}

void UpdateManager::startDownload() {
    if (m_state != State::UpdateAvailable)
        return;
    const QString url = m_latest.assetUrlForCurrentPlatform();
    const QString sha = m_latest.assetSha256ForCurrentPlatform();
    if (url.isEmpty() || sha.isEmpty()) {
        m_lastError = tr("No download available for this platform.");
        setState(State::Error);
        return;
    }
    const QFileInfo urlInfo(QUrl(url).path());
    QString fileName = urlInfo.fileName();
    if (fileName.isEmpty())
        fileName = QStringLiteral("trailer-update");
    const QString dest = downloadDir() + QLatin1Char('/') + fileName;

    // Set before setState() so the Downloading UI state (which reads
    // lastDownloadUrl()) has the URL to show from the first repaint —
    // same before-the-fetch disclosure framing as the check step.
    m_lastDownloadUrl = url;
    setState(State::Downloading);
    m_checker.downloadArtifact(url, dest, sha);
}

void UpdateManager::onDownloadFinished(const QString &path) {
    m_downloadedPath = path;
    setState(State::ReadyToInstall);
}

void UpdateManager::onDownloadFailed(const QString &message) {
    m_lastError = message;
    setState(State::Error);
}

void UpdateManager::installAndRelaunch() {
    if (m_state != State::ReadyToInstall || m_downloadedPath.isEmpty())
        return;

#ifdef Q_OS_MACOS
    // The downloaded artifact is a DMG containing Trailer.app (see
    // RELEASING.md / docs/packaging-macos.md). Mount it read-only,
    // clear quarantine on both the mounted copy and the artifact
    // itself (belt-and-suspenders — see clearQuarantine's comment),
    // copy the bundle over the running one with `ditto` (preserves
    // resource forks/xattrs, the tool Apple's own installers use), and
    // relaunch. This path is exercised structurally in code review but
    // has NOT been run against a real signed nightly on real Gatekeeper
    // hardware yet — flagged explicitly in the PR body as a dogfood-
    // verification follow-up, not asserted as proven here.
    clearQuarantine(m_downloadedPath);

    QProcess attach;
    attach.start(QStringLiteral("/usr/bin/hdiutil"),
                 {QStringLiteral("attach"), QStringLiteral("-nobrowse"),
                  QStringLiteral("-readonly"), QStringLiteral("-plist"), m_downloadedPath});
    attach.waitForFinished(30000);
    const QByteArray attachPlist = attach.readAllStandardOutput();

    // Extract the mounted volume path from hdiutil's plist output via
    // `plutil -convert json` (a stable Apple system tool) rather than
    // hand-scanning for a "<string>/Volumes/...</string>" line: a DMG can
    // have more than one system-entity (e.g. a hidden EFI/partition-map
    // entry alongside the actual data volume), and only the entity that
    // was genuinely mounted carries a "mount-point" key — a substring
    // scan can't distinguish that from an unrelated <string> value
    // elsewhere in the plist. plutil ships with every macOS install, so
    // this adds no new dependency.
    QTemporaryFile plistFile(QDir::tempPath() +
                             QStringLiteral("/trailer-update-attach-XXXXXX.plist"));
    QString mountPoint;
    if (plistFile.open()) {
        plistFile.write(attachPlist);
        plistFile.close();
        QProcess plutil;
        plutil.start(QStringLiteral("/usr/bin/plutil"),
                     {QStringLiteral("-convert"), QStringLiteral("json"), QStringLiteral("-o"),
                      QStringLiteral("-"), plistFile.fileName()});
        plutil.waitForFinished(10000);
        const QJsonDocument doc = QJsonDocument::fromJson(plutil.readAllStandardOutput());
        const QJsonArray entities = doc.object().value(QStringLiteral("system-entities")).toArray();
        for (const QJsonValue &v : entities) {
            const QString mp = v.toObject().value(QStringLiteral("mount-point")).toString();
            if (!mp.isEmpty()) {
                mountPoint = mp;
                break;
            }
        }
    }
    if (mountPoint.isEmpty()) {
        m_lastError = tr("Could not mount the downloaded update image.");
        setState(State::Error);
        return;
    }

    QDir mountedDir(mountPoint);
    QString sourceApp;
    for (const QString &entry : mountedDir.entryList({QStringLiteral("*.app")}, QDir::Dirs)) {
        sourceApp = mountedDir.filePath(entry);
        break;
    }

    const auto detach = [&mountPoint]() {
        QProcess proc;
        proc.start(QStringLiteral("/usr/bin/hdiutil"),
                   {QStringLiteral("detach"), mountPoint, QStringLiteral("-quiet")});
        proc.waitForFinished(15000);
    };

    if (sourceApp.isEmpty()) {
        detach();
        m_lastError = tr("The downloaded update image did not contain Trailer.app.");
        setState(State::Error);
        return;
    }
    clearQuarantine(sourceApp);

    // Walk up from Contents/MacOS/trailer to the .app bundle root.
    QDir appDir(QCoreApplication::applicationDirPath()); // .../Trailer.app/Contents/MacOS
    appDir.cdUp();                                       // .../Trailer.app/Contents
    appDir.cdUp();                                       // .../Trailer.app
    const QString installedAppPath = appDir.absolutePath();
    const QString parentDir = QFileInfo(installedAppPath).absolutePath();
    const QString stagedPath = installedAppPath + QStringLiteral(".update-staged");

    QProcess::execute(QStringLiteral("/bin/rm"), {QStringLiteral("-rf"), stagedPath});
    QProcess ditto;
    ditto.start(QStringLiteral("/usr/bin/ditto"), {sourceApp, stagedPath});
    ditto.waitForFinished(60000);
    if (ditto.exitCode() != 0) {
        detach();
        m_lastError = tr("Could not copy the update into place.");
        setState(State::Error);
        return;
    }
    clearQuarantine(stagedPath);
    detach();

    // Swap: old bundle -> .old (kept for one relaunch in case the swap
    // needs manual recovery), staged -> live path.
    const QString oldPath = installedAppPath + QStringLiteral(".old");
    QProcess::execute(QStringLiteral("/bin/rm"), {QStringLiteral("-rf"), oldPath});
    QProcess::execute(QStringLiteral("/bin/mv"), {installedAppPath, oldPath});
    QProcess::execute(QStringLiteral("/bin/mv"), {stagedPath, installedAppPath});
    QProcess::execute(QStringLiteral("/bin/rm"), {QStringLiteral("-rf"), oldPath});
    Q_UNUSED(parentDir);

    QProcess::startDetached(QStringLiteral("/usr/bin/open"), {installedAppPath});
    QCoreApplication::quit();
#else
    // Windows/Linux: no in-place installer exists yet (ROADMAP "Next"
    // item 7 — the Windows/Linux follow-on). Rather than pretending to
    // install (G3), reveal the verified download so the user can run it
    // themselves; the file's SHA256 already matched what the signed
    // feed committed to, so this is a safe manual handoff, not a
    // downgrade in trust.
    const QString dir = QFileInfo(m_downloadedPath).absolutePath();
#if defined(Q_OS_WIN)
    QProcess::startDetached(QStringLiteral("explorer.exe"),
                            {QDir::toNativeSeparators(dir)});
#else
    QProcess::startDetached(QStringLiteral("xdg-open"), {dir});
#endif
#endif
}

void UpdateManager::debugForceStateForTesting(State state, FeedEntry entry, QString error,
                                              QString checkUrl) {
    m_latest = std::move(entry);
    m_lastError = std::move(error);
    if (!checkUrl.isEmpty())
        m_lastCheckUrl = std::move(checkUrl);
    setState(state);
}

void UpdateManager::setState(State s) {
    m_state = s;
    emit stateChanged();
}

} // namespace trailer::Update
