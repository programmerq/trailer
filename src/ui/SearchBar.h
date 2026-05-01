#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;
class QToolButton;

namespace trailer {

class SearchBar : public QWidget {
    Q_OBJECT

public:
    explicit SearchBar(QWidget* parent = nullptr);

    void focusInput();
    QString query() const;
    // Show "<current> of <total>" between the input and the
    // arrow buttons. Pass total = 0 to clear (no query yet).
    // Pass current = 0 with total > 0 for "no current match
    // selected" — common while the search is still running.
    void setMatchCounter(int current, int total);

signals:
    void queryChanged(const QString& query);
    void findNextRequested();
    void findPreviousRequested();
    void dismissed();

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    QLineEdit* m_input = nullptr;
    QLabel* m_counter = nullptr;
    QToolButton* m_prev = nullptr;
    QToolButton* m_next = nullptr;
    QToolButton* m_close = nullptr;
};

}  // namespace trailer
