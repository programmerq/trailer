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

std::string toStd(const QString& s) {
    return s.toStdString();
}

QString fromStd(std::string_view s) {
    return QString::fromUtf8(s.data(), static_cast<int>(s.size()));
}

}  // namespace

QString themeToString(Theme value) {
    switch (value) {
        case Theme::System: return QStringLiteral("system");
        case Theme::Light:  return QStringLiteral("light");
        case Theme::Dark:   return QStringLiteral("dark");
    }
    return QStringLiteral("system");
}

Theme themeFromString(const QString& value) {
    if (value == QLatin1String("light")) return Theme::Light;
    if (value == QLatin1String("dark"))  return Theme::Dark;
    return Theme::System;
}

QString openFilesInToString(OpenFilesIn value) {
    switch (value) {
        case OpenFilesIn::NewTab:     return QStringLiteral("new_tab");
        case OpenFilesIn::NewWindow:  return QStringLiteral("new_window");
        case OpenFilesIn::SameWindow: return QStringLiteral("same_window");
    }
    return QStringLiteral("new_tab");
}

OpenFilesIn openFilesInFromString(const QString& value) {
    if (value == QLatin1String("new_tab"))     return OpenFilesIn::NewTab;
    if (value == QLatin1String("same_window")) return OpenFilesIn::SameWindow;
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
    } catch (const toml::parse_error&) {
        return;
    }

    if (auto* general = tbl["general"].as_table()) {
        if (auto v = (*general)["theme"].value<std::string>()) {
            m_theme = themeFromString(fromStd(*v));
        }
        if (auto v = (*general)["open_files_in"].value<std::string>()) {
            m_openFilesIn = openFilesInFromString(fromStd(*v));
        }
    }
    if (auto* files = tbl["files"].as_table()) {
        if (auto v = (*files)["auto_save"].value<bool>()) {
            m_autoSave = *v;
        }
        if (auto v = (*files)["recent_max"].value<int64_t>()) {
            m_recentMax = static_cast<int>(*v);
        }
    }
    if (auto* redaction = tbl["redaction"].as_table()) {
        if (auto v = (*redaction)["warning_acknowledged"].value<bool>()) {
            m_redactionWarningAcknowledged = *v;
        }
    }
}

void Settings::save() const {
    toml::table tbl{
        {"general", toml::table{
            {"theme", toStd(themeToString(m_theme))},
            {"open_files_in", toStd(openFilesInToString(m_openFilesIn))},
        }},
        {"files", toml::table{
            {"auto_save", m_autoSave},
            {"recent_max", static_cast<int64_t>(m_recentMax)},
        }},
        {"redaction", toml::table{
            {"warning_acknowledged", m_redactionWarningAcknowledged},
        }},
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

void Settings::setTheme(Theme value) { m_theme = value; }
void Settings::setOpenFilesIn(OpenFilesIn value) { m_openFilesIn = value; }
void Settings::setAutoSave(bool value) { m_autoSave = value; }
void Settings::setRecentMax(int value) { m_recentMax = value; }
void Settings::setRedactionWarningAcknowledged(bool value) {
    m_redactionWarningAcknowledged = value;
}

}  // namespace trailer
