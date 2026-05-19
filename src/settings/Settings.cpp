#include "Settings.h"

#include "AppPaths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include <toml++/toml.h>

#include <sstream>
#include <string>

namespace trailer {

namespace {

std::string toStd(const QString &s) {
    return s.toStdString();
}

QString fromStd(std::string_view s) {
    return QString::fromUtf8(s.data(), static_cast<int>(s.size()));
}

} // namespace

QString themeToString(Theme value) {
    switch (value) {
    case Theme::System:
        return QStringLiteral("system");
    case Theme::Light:
        return QStringLiteral("light");
    case Theme::Dark:
        return QStringLiteral("dark");
    }
    return QStringLiteral("system");
}

Theme themeFromString(const QString &value) {
    if (value == QLatin1String("light"))
        return Theme::Light;
    if (value == QLatin1String("dark"))
        return Theme::Dark;
    return Theme::System;
}

QString openFilesInToString(OpenFilesIn value) {
    switch (value) {
    case OpenFilesIn::NewTab:
        return QStringLiteral("new_tab");
    case OpenFilesIn::NewWindow:
        return QStringLiteral("new_window");
    case OpenFilesIn::SameWindow:
        return QStringLiteral("same_window");
    }
    return QStringLiteral("new_tab");
}

OpenFilesIn openFilesInFromString(const QString &value) {
    if (value == QLatin1String("new_tab"))
        return OpenFilesIn::NewTab;
    if (value == QLatin1String("same_window"))
        return OpenFilesIn::SameWindow;
    // Default is window-per-file (see Settings.h). Any unrecognised
    // or empty value falls through to the same NewWindow default so
    // a typo in settings.toml doesn't silently flip behaviour.
    return OpenFilesIn::NewWindow;
}

Settings::Settings() : Settings(AppPaths::settingsFile()) {}

Settings::Settings(QString filePath) : m_filePath(std::move(filePath)) {}

void Settings::load() {
    QFile file(m_filePath);
    if (!file.exists()) {
        return;
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }
    const std::string content = file.readAll().toStdString();
    file.close();

    toml::table tbl;
    try {
        tbl = toml::parse(content);
    } catch (const toml::parse_error &) {
        return;
    }

    if (auto *general = tbl["general"].as_table()) {
        if (auto v = (*general)["theme"].value<std::string>()) {
            m_theme = themeFromString(fromStd(*v));
        }
        if (auto v = (*general)["open_files_in"].value<std::string>()) {
            m_openFilesIn = openFilesInFromString(fromStd(*v));
        }
        if (auto v = (*general)["last_save_dir"].value<std::string>()) {
            m_lastSaveDir = fromStd(*v);
        }
    }
    if (auto *files = tbl["files"].as_table()) {
        if (auto v = (*files)["auto_save"].value<bool>()) {
            m_autoSave = *v;
        }
        if (auto v = (*files)["recent_max"].value<int64_t>()) {
            m_recentMax = static_cast<int>(*v);
        }
    }
    if (auto *session = tbl["session"].as_table()) {
        if (auto v = (*session)["restore_previous_windows"].value<bool>()) {
            m_restorePreviousWindows = *v;
        }
        if (auto *arr = (*session)["open_files"].as_array()) {
            m_sessionOpenFiles.clear();
            for (const auto &node : *arr) {
                if (auto v = node.value<std::string>()) {
                    m_sessionOpenFiles.append(fromStd(*v));
                }
            }
        }
    }
    // Legacy [redaction] section. Older installs persisted this single
    // first-use flag in its own table; new installs use the unified
    // [first_use] table below. Read both for backwards compatibility,
    // and write only the new table on save() so the old key fades out.
    if (auto *redaction = tbl["redaction"].as_table()) {
        if (auto v = (*redaction)["warning_acknowledged"].value<bool>()) {
            m_firstUseFlags.insert(QStringLiteral("redaction"), *v);
        }
    }
    if (auto *firstUse = tbl["first_use"].as_table()) {
        for (const auto &[k, node] : *firstUse) {
            if (auto v = node.value<bool>()) {
                m_firstUseFlags.insert(fromStd(std::string(k.str())), *v);
            }
        }
    }
    if (auto *mlScheduler = tbl["ml"]["scheduler"].as_table()) {
        if (auto v = (*mlScheduler)["recognize_text_in_background"].value<bool>()) {
            m_mlRecognizeTextInBackground = *v;
        }
        if (auto v = (*mlScheduler)["preload_segmentation_on_tool_activation"].value<bool>()) {
            m_mlPreloadSegmentationOnToolActivation = *v;
        }
        if (auto v = (*mlScheduler)["run_on_battery"].value<bool>()) {
            m_mlRunOnBattery = *v;
        }
    }
}

void Settings::save() const {
    toml::table firstUse;
    for (auto it = m_firstUseFlags.cbegin(); it != m_firstUseFlags.cend(); ++it) {
        firstUse.insert(toStd(it.key()), it.value());
    }

    toml::table generalTbl{
        {"theme", toStd(themeToString(m_theme))},
        {"open_files_in", toStd(openFilesInToString(m_openFilesIn))},
    };
    if (!m_lastSaveDir.isEmpty()) {
        generalTbl.insert("last_save_dir", toStd(m_lastSaveDir));
    }

    toml::array sessionFiles;
    for (const QString &p : m_sessionOpenFiles) {
        sessionFiles.push_back(toStd(p));
    }

    toml::table mlSchedulerTbl{
        {"recognize_text_in_background", m_mlRecognizeTextInBackground},
        {"preload_segmentation_on_tool_activation", m_mlPreloadSegmentationOnToolActivation},
        {"run_on_battery", m_mlRunOnBattery},
    };

    toml::table mlTbl{
        {"scheduler", std::move(mlSchedulerTbl)},
    };

    toml::table tbl{
        {"general", std::move(generalTbl)},
        {"files",
         toml::table{
             {"auto_save", m_autoSave},
             {"recent_max", static_cast<int64_t>(m_recentMax)},
         }},
        {"session",
         toml::table{
             {"restore_previous_windows", m_restorePreviousWindows},
             {"open_files", std::move(sessionFiles)},
         }},
        {"first_use", std::move(firstUse)},
        {"ml", std::move(mlTbl)},
    };

    AppPaths::ensureDirExists(QFileInfo(m_filePath).absolutePath());

    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return;
    }

    std::ostringstream out;
    out << tbl;
    const std::string payload = out.str();
    file.write(payload.data(), static_cast<qint64>(payload.size()));
    file.close();
}

void Settings::setTheme(Theme value) {
    m_theme = value;
}
void Settings::setOpenFilesIn(OpenFilesIn value) {
    m_openFilesIn = value;
}
void Settings::setAutoSave(bool value) {
    m_autoSave = value;
}
void Settings::setRecentMax(int value) {
    m_recentMax = value;
}
void Settings::setLastSaveDir(const QString &value) {
    m_lastSaveDir = value;
}
void Settings::setRestorePreviousWindows(bool value) {
    m_restorePreviousWindows = value;
}
void Settings::setSessionOpenFiles(const QStringList &value) {
    m_sessionOpenFiles = value;
}

void Settings::setMlRecognizeTextInBackground(bool value) {
    m_mlRecognizeTextInBackground = value;
}

void Settings::setMlPreloadSegmentationOnToolActivation(bool value) {
    m_mlPreloadSegmentationOnToolActivation = value;
}

void Settings::setMlRunOnBattery(bool value) {
    m_mlRunOnBattery = value;
}

void Settings::setFirstUseAcknowledged(const QString &key, bool value) {
    if (value) {
        m_firstUseFlags.insert(key, true);
    } else {
        m_firstUseFlags.remove(key);
    }
}

} // namespace trailer
