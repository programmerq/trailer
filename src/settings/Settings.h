#pragma once

#include <QString>

namespace trailer {

enum class OpenFilesIn {
    NewTab,
    NewWindow,
    SameWindow,
};

enum class Theme {
    System,
    Light,
    Dark,
};

class Settings {
public:
    Settings();
    explicit Settings(QString filePath);

    void load();
    void save() const;

    Theme theme() const { return m_theme; }
    void setTheme(Theme value);

    OpenFilesIn openFilesIn() const { return m_openFilesIn; }
    void setOpenFilesIn(OpenFilesIn value);

    bool autoSave() const { return m_autoSave; }
    void setAutoSave(bool value);

    int recentMax() const { return m_recentMax; }
    void setRecentMax(int value);

    QString filePath() const { return m_filePath; }

private:
    QString m_filePath;
    Theme m_theme = Theme::System;
    OpenFilesIn m_openFilesIn = OpenFilesIn::NewTab;
    bool m_autoSave = true;
    int m_recentMax = 50;
};

QString themeToString(Theme value);
Theme themeFromString(const QString& value);
QString openFilesInToString(OpenFilesIn value);
OpenFilesIn openFilesInFromString(const QString& value);

}  // namespace trailer
