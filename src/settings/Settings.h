#pragma once

#include <QHash>
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

    // Directory the user last saved into. Used to seed Save-As file
    // dialogs so successive saves of related documents land in the
    // same folder rather than always defaulting to ~/Documents. The
    // value is best-effort — empty string means "use platform default".
    QString lastSaveDir() const { return m_lastSaveDir; }
    void setLastSaveDir(const QString &value);

    // Whether the user has seen the one-time "redaction is not
    // defence-grade" warning (DESIGN §6.11.6). True = do not show
    // again; false = show on next redaction attempt. Convenience
    // wrapper around the generic firstUseAcknowledged store.
    bool redactionWarningAcknowledged() const {
        return firstUseAcknowledged(QStringLiteral("redaction"));
    }
    void setRedactionWarningAcknowledged(bool value) {
        setFirstUseAcknowledged(QStringLiteral("redaction"), value);
    }

    // Generic key→bool storage. Originally added for one-time prompts
    // (e.g. the redaction warning); now also reused for the
    // never-download policy bits per ML model under keys of the form
    // `ml_never_download_<model_key>`. Keys are written under the
    // [first_use] table in settings.toml; unknown keys load as false.
    // Treat the table name as legacy — the storage is a generic bool bag.
    bool firstUseAcknowledged(const QString &key) const {
        return m_firstUseFlags.value(key, false);
    }
    void setFirstUseAcknowledged(const QString &key, bool value);

    QString filePath() const { return m_filePath; }

  private:
    QString m_filePath;
    Theme m_theme = Theme::System;
    // Window-per-file is the default after the 2026-04-24 HITL review
    // ("tabs are mostly in the way"). Tabs remain available as an opt-in
    // by setting open_files_in = "new_tab" in settings.toml.
    OpenFilesIn m_openFilesIn = OpenFilesIn::NewWindow;
    bool m_autoSave = true;
    int m_recentMax = 50;
    QString m_lastSaveDir;
    QHash<QString, bool> m_firstUseFlags;
};

QString themeToString(Theme value);
Theme themeFromString(const QString &value);
QString openFilesInToString(OpenFilesIn value);
OpenFilesIn openFilesInFromString(const QString &value);

} // namespace trailer
