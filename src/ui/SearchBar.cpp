#include "SearchBar.h"

#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QStyle>
#include <QToolButton>

namespace trailer {

namespace {
// Cross-platform correctness (2026-08-01, PR #141 CI failure under
// Windows-cross-build-under-Wine): this used to be a `constexpr int
// kCounterWidth = 92` measured once via an offscreen probe on Linux
// (DejaVu Sans, "9999 matches"-scale text at 86px + a small margin). A
// pixel width measured against one platform's font does not predict
// another platform's rendering of the same string (Windows/Wine resolve a
// different font, at minimum) — see MlProgressWidget::maxWidth()'s doc
// comment for the class of bug this caused elsewhere in the same PR.
// Fixed the same way: measured from THIS platform's real, live font
// metrics in the constructor, against the same representative worst-case
// string ("9999 matches" — a document with four-digit match counts is
// already an extreme case), rather than a literal baked from one
// platform's measurement. kCounterWidthPad absorbs centered-alignment
// rounding at the elision boundary itself (not cross-platform variance —
// the live QFontMetrics call already IS the cross-platform-correct
// measurement).
constexpr int kCounterWidthPad = 8;
} // namespace

SearchBar::SearchBar(QWidget *parent) : QWidget(parent) {
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);

    m_input = new QLineEdit(this);
    m_input->setPlaceholderText(tr("Find in document…"));
    m_input->setClearButtonEnabled(true);
    connect(m_input, &QLineEdit::textChanged, this, &SearchBar::queryChanged);
    connect(m_input, &QLineEdit::returnPressed, this, &SearchBar::findNextRequested);
    // QLineEdit::returnPressed carries no modifier information, so Shift+Enter
    // needs to be intercepted BEFORE it reaches the line edit's own Return
    // handling (which would fire returnPressed → findNextRequested regardless
    // of Shift). An event filter on the child is the standard pattern for a
    // parent observing events a child widget would otherwise consume
    // (docs/CONVENTIONS.md §6) — SearchBar::keyPressEvent alone can't see this
    // key press because QLineEdit accepts Return itself.
    m_input->installEventFilter(this);

    // "X of Y" counter that lives between the input and the arrows.
    //
    // G10 (spatial constancy, AGENTS.md; SC-MOD-1,
    // docs/audit-2026-07-31-g10-deference.md): this used to hide() with no
    // query and show() once matches existed, which collapsed/restored its
    // slot in this shared QHBoxLayout and shifted Prev/Next/Close sideways
    // by its own width the moment the match count crossed zero — typing
    // the first matching character, or clearing the query, moved the
    // buttons the user's mouse was tracking. Fixed by never hiding it:
    // it stays visible at a FIXED width (kCounterWidth) always, blank when
    // there is nothing to report — see setMatchCounter().
    m_counter = new QLabel(this);
    m_counter->setForegroundRole(QPalette::Mid);
    const QFontMetrics counterFm(m_counter->font());
    m_counterWidth =
        counterFm.horizontalAdvance(QStringLiteral("9999 matches")) + kCounterWidthPad;
    m_counter->setFixedWidth(m_counterWidth);
    m_counter->setAlignment(Qt::AlignCenter);

    m_prev = new QToolButton(this);
    m_prev->setIcon(style()->standardIcon(QStyle::SP_ArrowUp));
    m_prev->setToolTip(tr("Previous match"));
    m_prev->setAccessibleName(tr("Previous match"));
    connect(m_prev, &QToolButton::clicked, this, &SearchBar::findPreviousRequested);

    m_next = new QToolButton(this);
    m_next->setIcon(style()->standardIcon(QStyle::SP_ArrowDown));
    m_next->setToolTip(tr("Next match"));
    m_next->setAccessibleName(tr("Next match"));
    connect(m_next, &QToolButton::clicked, this, &SearchBar::findNextRequested);

    m_close = new QToolButton(this);
    m_close->setIcon(style()->standardIcon(QStyle::SP_DialogCloseButton));
    m_close->setToolTip(tr("Close search"));
    m_close->setAccessibleName(tr("Close search"));
    connect(m_close, &QToolButton::clicked, this, &SearchBar::dismissed);

    layout->addWidget(m_input, 1);
    layout->addWidget(m_counter);
    layout->addWidget(m_prev);
    layout->addWidget(m_next);
    layout->addWidget(m_close);
}

void SearchBar::setMatchCounter(int current, int total) {
    // G10/SC-MOD-1: m_counter is always visible at a fixed width (see the
    // constructor comment) — blank it rather than hide() it so Prev/Next/
    // Close never move as the count crosses zero.
    if (total <= 0) {
        m_counter->clear();
        return;
    }
    const QString text = current <= 0 ? tr("%1 matches").arg(total)
                                       : tr("%1 of %2").arg(current).arg(total);
    // Elide rather than let an extreme match count grow past the fixed
    // width — a real count would have to be very large to trigger this
    // (m_counterWidth already covers "9999 matches"-scale text on this
    // platform's own font), but a truncated-with-tooltip label beats one
    // that silently forces the reserved slot wider and reopens the reflow
    // this fix closes.
    const QFontMetrics fm(m_counter->font());
    const QString elided = fm.elidedText(text, Qt::ElideRight, m_counterWidth);
    m_counter->setText(elided);
    m_counter->setToolTip(elided == text ? QString() : text);
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

bool SearchBar::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_input && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if ((keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) &&
            (keyEvent->modifiers() & Qt::ShiftModifier)) {
            // Shift+Enter = previous match, mirroring the Find bar convention
            // most viewers use for reverse-direction search. Consume the
            // event so the line edit never sees it — otherwise it would
            // additionally fire returnPressed (findNextRequested) for the
            // same key press.
            emit findPreviousRequested();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace trailer
