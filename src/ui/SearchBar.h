#pragma once

#include <QWidget>

class QLineEdit;
class QToolButton;

namespace trailer {

class SearchBar : public QWidget {
    Q_OBJECT

public:
    explicit SearchBar(QWidget* parent = nullptr);

    void focusInput();
    QString query() const;

signals:
    void queryChanged(const QString& query);
    void findNextRequested();
    void findPreviousRequested();
    void dismissed();

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    QLineEdit* m_input = nullptr;
    QToolButton* m_prev = nullptr;
    QToolButton* m_next = nullptr;
    QToolButton* m_close = nullptr;
};

}  // namespace trailer
