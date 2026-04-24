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

    // Whether the user has seen the one-time "redaction is not
    // defence-grade" warning (DESIGN §6.11.6). True = do not show
    // again; false = show on next redaction attempt.
    bool redactionWarningAcknowledged() const {
        return m_redactionWarningAcknowledged;
    }
    void setRedactionWarningAcknowledged(bool value);

    QString filePath() const { return m_filePath; }

private:
    QString m_filePath;
    Theme m_theme = Theme::System;
    OpenFilesIn m_openFilesIn = OpenFilesIn::NewTab;
    bool m_autoSave = true;
    int m_recentMax = 50;
    bool m_redactionWarningAcknowledged = false;
};

QString themeToString(Theme value);
Theme themeFromString(const QString& value);
QString openFilesInToString(OpenFilesIn value);
OpenFilesIn openFilesInFromString(const QString& value);

}  // namespace trailer
