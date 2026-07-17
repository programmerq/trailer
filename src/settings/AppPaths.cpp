#include "AppPaths.h"

#include <QDir>
#include <QProcessEnvironment>
#include <QStandardPaths>

namespace trailer {

namespace {

QString joinPath(const QString &base, const QString &leaf) {
    return QDir::cleanPath(base + QLatin1Char('/') + leaf);
}

#if defined(Q_OS_MACOS)
QString macAppSupport() {
    const QString home = QDir::homePath();
    return joinPath(home, QStringLiteral("Library/Application Support/Trailer"));
}
#endif

#if defined(Q_OS_LINUX) || defined(Q_OS_FREEBSD)
QString xdgPath(const char *env, const QString &fallbackRelativeToHome) {
    const QString value = QProcessEnvironment::systemEnvironment().value(QString::fromLatin1(env));
    if (!value.isEmpty()) {
        return joinPath(value, QStringLiteral("trailer"));
    }
    return joinPath(QDir::homePath(), fallbackRelativeToHome);
}
#endif

} // namespace

QString AppPaths::settingsDir() {
#if defined(Q_OS_MACOS)
    return macAppSupport();
#elif defined(Q_OS_WIN)
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
#else
    return xdgPath("XDG_CONFIG_HOME", QStringLiteral(".config/trailer"));
#endif
}

QString AppPaths::dataDir() {
#if defined(Q_OS_MACOS)
    return macAppSupport();
#elif defined(Q_OS_WIN)
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
#else
    return xdgPath("XDG_DATA_HOME", QStringLiteral(".local/share/trailer"));
#endif
}

QString AppPaths::settingsFile() {
    return joinPath(settingsDir(), QStringLiteral("settings.toml"));
}

QString AppPaths::recentFile() {
    return joinPath(dataDir(), QStringLiteral("recent.json"));
}

QString AppPaths::cardsFile() {
    return joinPath(dataDir(), QStringLiteral("cards.toml"));
}

QString AppPaths::signaturesDir() {
    return joinPath(dataDir(), QStringLiteral("signatures"));
}

QString AppPaths::autofillDir() {
    return joinPath(dataDir(), QStringLiteral("autofill"));
}

QString AppPaths::versionsDir() {
    return joinPath(dataDir(), QStringLiteral("versions"));
}

QString AppPaths::ocrCacheDir() {
    return joinPath(dataDir(), QStringLiteral("ocr_cache"));
}

QString AppPaths::iccDir() {
    return joinPath(dataDir(), QStringLiteral("icc"));
}

QString AppPaths::filtersDir() {
    return joinPath(dataDir(), QStringLiteral("filters"));
}

QString AppPaths::pluginsDir() {
    return joinPath(dataDir(), QStringLiteral("plugins"));
}

QString AppPaths::logsDir() {
    return joinPath(dataDir(), QStringLiteral("logs"));
}

QString AppPaths::modelsDir() {
    return joinPath(dataDir(), QStringLiteral("models"));
}

QString AppPaths::uxSessionsDir() {
    return joinPath(dataDir(), QStringLiteral("ux-sessions"));
}

void AppPaths::ensureDirExists(const QString &path) {
    QDir().mkpath(path);
}

} // namespace trailer
