#pragma once

#include "UpdateChecker.h"
#include "UpdateTypes.h"

#include <QObject>
#include <QString>

namespace trailer {
class Settings;
}

namespace trailer::Update {

// Orchestrates the nightly update channel: owns the one networking
// object (UpdateChecker), applies the Settings-driven auto-check policy,
// decides "is this actually newer" (the downgrade-proof version
// compare), and drives the platform install step. No QNetworkAccessManager
// / QNetworkReply here — see UpdateChecker.h for why that separation
// matters.
//
// Auto-check policy (owner decision, 2026-07-30): OFF by default. When
// the user turns "Automatically check for updates" on in Preferences,
// Trailer checks once at startup and once per 24h while running — never
// more often, never silently downloading. The "Check for Updates…" menu
// action is independent of this setting and always available (G3): it
// always performs one manual check, whether or not auto-check is on.
//
// Download/install policy: ALWAYS a user action. Finding an update never
// downloads it — the caller (Preferences pane or the manual-check
// dialog) must call startDownload() itself, and installAndRelaunch()
// only runs after that download has verified. Nothing writes to the
// application directory without the user clicking a button that says so.
class UpdateManager : public QObject {
    Q_OBJECT

  public:
    enum class State { Idle, Checking, UpToDate, UpdateAvailable, Downloading, ReadyToInstall, Error };

    explicit UpdateManager(Settings &settings, QObject *parent = nullptr);

    // Always available regardless of the auto-check setting — the menu
    // action's one-shot manual check (G3: never a dead control).
    void checkNow();

    // Called once at startup (and safe to call any time, e.g. from a
    // 24h timer): a no-op unless Settings::updatesAutoCheckEnabled() is
    // true AND at least 24h have passed since the last check.
    void maybeAutoCheck();

    // Begins downloading the currently-known available update. No-op if
    // state() is not UpdateAvailable.
    void startDownload();

    // Installs the downloaded artifact and relaunches. macOS: clears the
    // quarantine bit on the new bundle, swaps it into place, relaunches.
    // Windows/Linux: no automatic in-place swap exists yet (tracked for
    // the Windows/Linux follow-on — ROADMAP "Next" item 7), so this
    // reveals the downloaded file in the platform file manager instead
    // of pretending to install it (G3: no lying controls). No-op if
    // state() is not ReadyToInstall.
    void installAndRelaunch();

    // Test-only seam: forces the manager into an arbitrary state without a
    // real network round-trip, so UAT/G2 evidence harnesses (see
    // tests/uat/test_uat_preferences.cpp) can render every Updates-pane
    // state deterministically (Checking / UpdateAvailable / UpToDate /
    // Error / ReadyToInstall). Never called from any production code
    // path — the real state machine only moves via UpdateChecker's
    // signals (see the private slots below).
    void debugForceStateForTesting(State state, FeedEntry entry = {}, QString error = {},
                                   QString checkUrl = {});

    State state() const { return m_state; }
    FeedEntry latestEntry() const { return m_latest; }
    QString lastError() const { return m_lastError; }
    QString lastCheckUrl() const { return m_lastCheckUrl; }
    QString lastDownloadUrl() const { return m_lastDownloadUrl; }
    QString downloadedPath() const { return m_downloadedPath; }

  signals:
    void stateChanged();
    // Disclosed BEFORE the request fires — mirrors ModelDownloader's
    // show-the-URL consent framing (AGENTS.md "Networking").
    void checkStarted(QString url);
    void downloadProgress(qint64 received, qint64 total);

  private:
    void setState(State s);
    void onUpdateAvailable(const FeedEntry &entry);
    void onUpToDate();
    void onCheckFailed(const QString &message);
    void onDownloadFinished(const QString &path);
    void onDownloadFailed(const QString &message);

    Settings &m_settings;
    UpdateChecker m_checker;
    State m_state = State::Idle;
    FeedEntry m_latest;
    QString m_lastError;
    QString m_lastCheckUrl;
    QString m_lastDownloadUrl;
    QString m_downloadedPath;
};

} // namespace trailer::Update
