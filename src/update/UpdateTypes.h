#pragma once

#include <QMetaType>
#include <QString>

namespace trailer::Update {

// Only "nightly" is wired end-to-end today (owner decision, 2026-07-30 —
// see docs/decision-records/2026-07-30-nightly-auto-update-channel.md).
// Stable exists as a UI-visible, deliberately-disabled option so the
// Preferences control doesn't need reshaping when release.yml grows its
// own signed feed later.
enum class Channel { Nightly, Stable };

QString channelToString(Channel value);
Channel channelFromString(const QString &value);

// One parsed-and-verified entry from the signed appcast feed: the
// newest build available on a channel, for the current platform.
// `buildNumber` is the git commit count on main at publish time (see
// TrailerVersion.h.in / TRAILER_BUILD_COMMIT_COUNT) — monotonically
// increasing for every nightly built off main, so "is this newer than
// what I'm running" is a plain integer compare with no date parsing.
struct FeedEntry {
    Channel channel = Channel::Nightly;
    QString tag;         // e.g. "nightly-20260730"
    qint64 buildNumber = 0; // git commit count at publish time
    QString notes;        // short human-readable summary (release body excerpt)
    QString publishedAtUtc; // ISO-8601, informational only

    // Per-platform download: only the platforms the feed actually
    // publishes are populated. Empty url/sha256 means "no build for
    // this platform in this entry" — checked explicitly rather than
    // assumed, so a partial-success nightly (see nightly.yml) that
    // shipped only Linux+Windows doesn't lie to a macOS client.
    QString macosAssetUrl;
    QString macosSha256; // lowercase hex

    QString windowsAssetUrl;
    QString windowsSha256;

    QString linuxAssetUrl;
    QString linuxSha256;

    bool hasAssetForCurrentPlatform() const;
    QString assetUrlForCurrentPlatform() const;
    QString assetSha256ForCurrentPlatform() const;
};

// The downgrade guard, factored out as a pure function so it's
// unit-testable without a live UpdateManager/UpdateChecker: a feed
// entry is only ever treated as "an update" if its buildNumber is
// STRICTLY greater than what's currently running. Equal or lower
// (a stale cached feed, a replay, or — the case this guards against —
// a signed-but-older entry someone tries to serve to force a downgrade)
// is never "available", regardless of how the feed got there.
bool isBuildNewer(qint64 candidateBuildNumber, qint64 currentBuildNumber);

} // namespace trailer::Update

Q_DECLARE_METATYPE(trailer::Update::FeedEntry)
