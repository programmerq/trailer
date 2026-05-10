#include "AnimationBar.h"

#include "document/IDocument.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QStyle>
#include <QToolButton>

namespace trailer {

AnimationBar::AnimationBar(QWidget *parent) : QWidget(parent) {
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);

    m_playButton = new QToolButton(this);
    m_playButton->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
    m_playButton->setToolTip(tr("Play / Pause"));
    connect(m_playButton, &QToolButton::clicked, this, &AnimationBar::onPlayToggled);

    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setMinimum(0);
    m_slider->setSingleStep(1);
    m_slider->setPageStep(1);
    connect(m_slider, &QSlider::sliderMoved, this, &AnimationBar::onSliderMoved);

    m_counter = new QLabel(this);
    m_counter->setMinimumWidth(80);

    layout->addWidget(m_playButton);
    layout->addWidget(m_slider, 1);
    layout->addWidget(m_counter);

    m_pollTimer.setInterval(80);
    connect(&m_pollTimer, &QTimer::timeout, this, &AnimationBar::poll);
}

void AnimationBar::setDocument(IDocument *doc) {
    m_doc = doc;
    if (!doc || !doc->supportsAnimation()) {
        m_pollTimer.stop();
        hide();
        return;
    }
    const int frames = doc->frameCount();
    m_slider->setMaximum(frames > 0 ? frames - 1 : 0);
    m_slider->setValue(doc->currentFrame());
    refreshPlayIcon();
    m_counter->setText(tr("%1 / %2").arg(doc->currentFrame() + 1).arg(frames));
    show();
    m_pollTimer.start();
}

void AnimationBar::onPlayToggled() {
    if (!m_doc)
        return;
    m_doc->setAnimationPlaying(!m_doc->isAnimationPlaying());
    refreshPlayIcon();
}

void AnimationBar::onSliderMoved(int value) {
    if (!m_doc)
        return;
    m_doc->setCurrentFrame(value);
    refreshPlayIcon();
    const int total = m_doc->frameCount();
    m_counter->setText(tr("%1 / %2").arg(value + 1).arg(total));
}

void AnimationBar::poll() {
    if (!m_doc || !m_doc->supportsAnimation()) {
        return;
    }
    if (!m_slider->isSliderDown()) {
        const int current = m_doc->currentFrame();
        if (current != m_slider->value()) {
            QSignalBlocker block(m_slider);
            m_slider->setValue(current);
        }
        m_counter->setText(tr("%1 / %2").arg(current + 1).arg(m_doc->frameCount()));
    }
}

void AnimationBar::refreshPlayIcon() {
    if (!m_doc)
        return;
    const bool playing = m_doc->isAnimationPlaying();
    m_playButton->setIcon(
        style()->standardIcon(playing ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
}

} // namespace trailer
