#include "MlProgressWidget.h"

#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QTimer>
#include <QToolButton>

namespace trailer {

MlProgressWidget::MlProgressWidget(QWidget *parent) : QWidget(parent) {
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 0, 4, 0);
    layout->setSpacing(6);

    m_label = new QLabel(this);
    // Match the sunken-panel idiom of the sibling m_mlIndicator so the
    // status bar reads as one family of ML affordances (MainWindow.cpp).
    m_label->setFrameStyle(QFrame::NoFrame);

    m_bar = new QProgressBar(this);
    m_bar->setTextVisible(false);
    m_bar->setFixedWidth(kBarWidth);
    // A compact bar sits comfortably in the status bar's fixed height.
    m_bar->setMaximumHeight(14);

    m_cancel = new QToolButton(this);
    m_cancel->setText(QStringLiteral("✕")); // ✕
    m_cancel->setAutoRaise(true);
    m_cancel->setAccessibleName(tr("Cancel"));
    m_cancel->setToolTip(tr("Cancel"));
    connect(m_cancel, &QToolButton::clicked, this, &MlProgressWidget::cancelRequested);

    layout->addWidget(m_label);
    layout->addWidget(m_bar);
    layout->addWidget(m_cancel);

    m_terminalTimer = new QTimer(this);
    m_terminalTimer->setSingleShot(true);
    connect(m_terminalTimer, &QTimer::timeout, this, &MlProgressWidget::goIdle);

    // G10 (spatial constancy): pin the widget's own HEIGHT, not just its
    // reserved status-bar WIDTH (MainWindow.cpp's reserveStatusBarSlot()).
    // finishWithMessage() hides m_bar/m_cancel for the Terminal state,
    // which — left alone — shrinks this widget's sizeHint().height() by a
    // few px (the Cancel QToolButton is taller than the plain label),
    // which shrinks the WHOLE status bar's height (it sizes to its
    // tallest child) and shifts every OTHER permanent widget vertically.
    // Measuring sizeHint() here, with every child still nominally visible
    // (only `this` is hidden below, which does not affect a widget's own
    // sizeHint), captures the tallest — Running — state once, so Terminal
    // narrows in width only, never in height.
    setFixedHeight(sizeHint().height());

    // Start hidden — an idle status bar stays clean.
    setVisible(false);
}

void MlProgressWidget::beginDeterminate(const QString &label, int total) {
    m_terminalTimer->stop();
    m_state = Running;
    m_baseLabel = label;
    m_elapsed = 0;
    m_done = 0;
    if (total >= 1) {
        m_determinate = true;
        m_total = total;
        m_bar->setRange(0, total);
        m_bar->setValue(0);
    } else {
        // Degenerate count — fall back to a busy bar rather than divide
        // by zero.
        m_determinate = false;
        m_total = 0;
        m_bar->setRange(0, 0);
    }
    m_bar->setVisible(true);
    m_cancel->setVisible(true);
    updateDeterminateLabel();
    setVisible(true);
}

void MlProgressWidget::beginIndeterminate(const QString &label) {
    m_terminalTimer->stop();
    m_state = Running;
    m_determinate = false;
    m_baseLabel = label;
    m_total = 0;
    m_done = 0;
    m_elapsed = 0;
    m_bar->setRange(0, 0); // busy
    m_bar->setVisible(true);
    m_cancel->setVisible(true);
    setLabelText(label, kRunningLabelMaxWidth);
    setVisible(true);
}

void MlProgressWidget::setProgress(int done) {
    if (!m_determinate || m_state != Running)
        return;
    m_done = qBound(0, done, m_total);
    m_bar->setValue(m_done);
    updateDeterminateLabel();
}

void MlProgressWidget::setElapsedSeconds(int s) {
    if (m_determinate || m_state != Running)
        return;
    m_elapsed = s;
    if (s >= 10) {
        setLabelText(m_baseLabel + tr(" · %1s").arg(s), kRunningLabelMaxWidth);
    } else {
        setLabelText(m_baseLabel, kRunningLabelMaxWidth);
    }
}

void MlProgressWidget::finishWithMessage(const QString &msg) {
    m_state = Terminal;
    m_bar->setVisible(false);
    m_cancel->setVisible(false);
    // Terminal: the bar and cancel button are hidden, so the label can use
    // the wider Terminal cap (see kTerminalLabelMaxWidth's comment).
    setLabelText(msg, kTerminalLabelMaxWidth);
    setVisible(true);
    if (m_terminalHoldMs <= 0) {
        goIdle();
        return;
    }
    m_terminalTimer->start(m_terminalHoldMs);
}

void MlProgressWidget::goIdle() {
    m_terminalTimer->stop();
    m_state = Idle;
    m_determinate = false;
    m_total = 0;
    m_done = 0;
    m_elapsed = 0;
    m_baseLabel.clear();
    setVisible(false);
}

int MlProgressWidget::value() const {
    return m_determinate ? m_bar->value() : 0;
}

QString MlProgressWidget::labelText() const {
    return m_label->text();
}

void MlProgressWidget::updateDeterminateLabel() {
    if (m_determinate) {
        setLabelText(tr("%1 — %2 / %3 pages").arg(m_baseLabel).arg(m_done).arg(m_total),
                     kRunningLabelMaxWidth);
    } else {
        setLabelText(m_baseLabel, kRunningLabelMaxWidth);
    }
}

void MlProgressWidget::setLabelText(const QString &text, int maxWidth) {
    const QFontMetrics fm(m_label->font());
    const QString elided = fm.elidedText(text, Qt::ElideRight, maxWidth);
    m_label->setText(elided);
    // Full text stays reachable on hover whenever elision actually
    // truncated something; clearing the tooltip otherwise avoids a
    // no-op hover affordance on short messages.
    m_label->setToolTip(elided == text ? QString() : text);
}

} // namespace trailer
