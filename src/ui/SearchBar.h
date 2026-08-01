#pragma once

#include <QWidget>

class QEvent;
class QLabel;
class QLineEdit;
class QToolButton;

namespace trailer {

class SearchBar : public QWidget {
    Q_OBJECT

  public:
    explicit SearchBar(QWidget *parent = nullptr);

    void focusInput();
    QString query() const;
    // Programmatic set — fires the same textChanged → queryChanged
    // chain a real keystroke would. Used by UATs to drive the
    // search without depending on which QLineEdit findChild()
    // happens to return first (the markup toolbar's QSpinBox /
    // QComboBox carry internal QLineEdits that show up earlier in
    // the depth-first walk).
    void setQuery(const QString &q);
    // Show "<current> of <total>" between the input and the
    // arrow buttons. Pass total = 0 to clear (no query yet).
    // Pass current = 0 with total > 0 for "no current match
    // selected" — common while the search is still running.
    //
    // G10 (spatial constancy, AGENTS.md; SC-MOD-1,
    // docs/audit-2026-07-31-g10-deference.md): the counter never hides —
    // see the constructor comment on m_counter's fixed width — so Prev /
    // Next / Close never move as the match count crosses zero.
    void setMatchCounter(int current, int total);

  signals:
    void queryChanged(const QString &query);
    void findNextRequested();
    void findPreviousRequested();
    void dismissed();

  protected:
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

  private:
    QLineEdit *m_input = nullptr;
    QLabel *m_counter = nullptr;
    QToolButton *m_prev = nullptr;
    QToolButton *m_next = nullptr;
    QToolButton *m_close = nullptr;
};

} // namespace trailer
