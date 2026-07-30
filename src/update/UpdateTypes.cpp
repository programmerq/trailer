#include "UpdateTypes.h"

namespace trailer::Update {

QString channelToString(Channel value) {
    switch (value) {
    case Channel::Nightly:
        return QStringLiteral("nightly");
    case Channel::Stable:
        return QStringLiteral("stable");
    }
    return QStringLiteral("nightly");
}

Channel channelFromString(const QString &value) {
    if (value == QLatin1String("stable"))
        return Channel::Stable;
    return Channel::Nightly;
}

bool FeedEntry::hasAssetForCurrentPlatform() const {
    return !assetUrlForCurrentPlatform().isEmpty() && !assetSha256ForCurrentPlatform().isEmpty();
}

QString FeedEntry::assetUrlForCurrentPlatform() const {
#if defined(Q_OS_MACOS)
    return macosAssetUrl;
#elif defined(Q_OS_WIN)
    return windowsAssetUrl;
#else
    return linuxAssetUrl;
#endif
}

QString FeedEntry::assetSha256ForCurrentPlatform() const {
#if defined(Q_OS_MACOS)
    return macosSha256;
#elif defined(Q_OS_WIN)
    return windowsSha256;
#else
    return linuxSha256;
#endif
}

bool isBuildNewer(qint64 candidateBuildNumber, qint64 currentBuildNumber) {
    return candidateBuildNumber > currentBuildNumber;
}

} // namespace trailer::Update
