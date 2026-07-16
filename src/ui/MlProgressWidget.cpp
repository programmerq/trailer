#include "MlProgressWidget.h"

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
    m_bar->setFixedWidth(120);
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
    m_label->setText(label);
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
        m_label->setText(m_baseLabel + tr(" · %1s").arg(s));
    } else {
        m_label->setText(m_baseLabel);
    }
}

void MlProgressWidget::finishWithMessage(const QString &msg) {
    m_state = Terminal;
    m_bar->setVisible(false);
    m_cancel->setVisible(false);
    m_label->setText(msg);
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
        m_label->setText(tr("%1 — %2 / %3 pages").arg(m_baseLabel).arg(m_done).arg(m_total));
    } else {
        m_label->setText(m_baseLabel);
    }
}

} // namespace trailer
