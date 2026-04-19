#pragma once

#include <QString>

namespace trailer {

class AppPaths {
public:
    static QString settingsDir();
    static QString dataDir();

    static QString settingsFile();
    static QString recentFile();
    static QString signaturesDir();
    static QString autofillDir();
    static QString versionsDir();
    static QString ocrCacheDir();
    static QString iccDir();
    static QString filtersDir();
    static QString pluginsDir();
    static QString logsDir();

    static void ensureDirExists(const QString& path);
};

}  // namespace trailer
