#pragma once

#include <QFrame>

class QLabel;
class QPushButton;

namespace trailer {

// Non-modal in-window banner shown above the document view when the file on
// disk diverges from our buffer in a way we must NOT auto-resolve. Two modes:
//
//   * Conflict — the file changed externally while we have unsaved edits.
//     Offers [Reload (discard my edits)] / [Keep mine] / [Compare]. Compare is
//     a disabled-with-tooltip placeholder (no diff view is built yet) so it is
//     G3-honest rather than a lying control.
//   * Deleted — the file was removed on disk. Offers [Save] (recreates it).
//
// A clean-doc external change is handled silently by an auto-reload and never
// shows this banner (Preview-style, per the no-narration-dialogs taste rule).
// See docs/decision-records/2026-07-19-external-file-change-handling.md.
class FileChangeBanner : public QFrame {
    Q_OBJECT

  public:
    enum class Mode { Hidden, Conflict, Deleted };

    explicit FileChangeBanner(QWidget *parent = nullptr);

    void showConflict();
    void showDeleted();
    void dismiss();

    Mode mode() const { return m_mode; }

    // --- accessors for tests ---
    QString messageText() const;
    bool reloadEnabled() const;
    bool keepMineEnabled() const;
    bool compareEnabled() const; // must be false (G3 placeholder)
    bool saveEnabled() const;
    QString compareTooltip() const;
    QString reloadText() const;
    QString keepMineText() const;
    // The visually-weighted primary/default action of the conflict banner
    // (CF-6). "Keep mine" is the default — it preserves the user's active work
    // and writes nothing until an explicit Save, the non-destructive-until-save
    // choice — while Reload (which discards edits) is secondary and Dismiss is
    // flat/passive.
    bool keepMineIsDefault() const;
    bool dismissIsFlat() const;

    // Fire the corresponding signal without a real click (offscreen tests).
    void clickReloadForTest();
    void clickKeepMineForTest();
    void clickSaveForTest();
    void clickDismissForTest();

  signals:
    void reloadRequested();
    void keepMineRequested();
    void saveRequested();
    void dismissed();

  private:
    void applyMode(Mode mode);

    Mode m_mode = Mode::Hidden;
    QLabel *m_icon = nullptr;
    QLabel *m_message = nullptr;
    QPushButton *m_reloadButton = nullptr;
    QPushButton *m_keepMineButton = nullptr;
    QPushButton *m_compareButton = nullptr;
    QPushButton *m_saveButton = nullptr;
    QPushButton *m_dismissButton = nullptr;
};

} // namespace trailer
