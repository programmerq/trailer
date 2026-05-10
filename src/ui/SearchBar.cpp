#include "SearchBar.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QStyle>
#include <QToolButton>

namespace trailer {

SearchBar::SearchBar(QWidget *parent) : QWidget(parent) {
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);

    m_input = new QLineEdit(this);
    m_input->setPlaceholderText(tr("Find in document…"));
    m_input->setClearButtonEnabled(true);
    connect(m_input, &QLineEdit::textChanged, this, &SearchBar::queryChanged);
    connect(m_input, &QLineEdit::returnPressed, this, &SearchBar::findNextRequested);

    // "X of Y" counter that lives between the input and the
    // arrows. Hidden until the document has populated match data.
    m_counter = new QLabel(this);
    m_counter->setForegroundRole(QPalette::Mid);
    m_counter->setMinimumWidth(60);
    m_counter->setAlignment(Qt::AlignCenter);
    m_counter->hide();

    m_prev = new QToolButton(this);
    m_prev->setIcon(style()->standardIcon(QStyle::SP_ArrowUp));
    m_prev->setToolTip(tr("Previous match"));
    connect(m_prev, &QToolButton::clicked, this, &SearchBar::findPreviousRequested);

    m_next = new QToolButton(this);
    m_next->setIcon(style()->standardIcon(QStyle::SP_ArrowDown));
    m_next->setToolTip(tr("Next match"));
    connect(m_next, &QToolButton::clicked, this, &SearchBar::findNextRequested);

    m_close = new QToolButton(this);
    m_close->setIcon(style()->standardIcon(QStyle::SP_DialogCloseButton));
    m_close->setToolTip(tr("Close search"));
    connect(m_close, &QToolButton::clicked, this, &SearchBar::dismissed);

    layout->addWidget(m_input, 1);
    layout->addWidget(m_counter);
    layout->addWidget(m_prev);
    layout->addWidget(m_next);
    layout->addWidget(m_close);
}

void SearchBar::setMatchCounter(int current, int total) {
    if (total <= 0) {
        m_counter->clear();
        m_counter->hide();
        return;
    }
    if (current <= 0) {
        m_counter->setText(tr("%1 matches").arg(total));
    } else {
        m_counter->setText(tr("%1 of %2").arg(current).arg(total));
    }
    m_counter->show();
}

void SearchBar::focusInput() {
    m_input->setFocus();
    m_input->selectAll();
}

QString SearchBar::query() const {
    return m_input->text();
}

void SearchBar::setQuery(const QString &q) {
    m_input->setText(q);
}

void SearchBar::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        emit dismissed();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

} // namespace trailer
