#include "Magnifier.h"

#include <QCursor>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>

namespace trailer {

namespace {

// Magnifier widget diameter in screen px. Big enough that magnified
// pixels are inspectable but small enough that the overlay doesn't
// dominate the desktop on a 13" laptop. Drop if dogfooding on a
// small display shows the loupe blocking the area the user is
// trying to read; raise if magnified text feels claustrophobic on
// a 27"+ monitor.
constexpr int kSize = 220;

// Refresh interval. 33 ms ≈ 30 fps, fast enough to feel like the
// loupe tracks the cursor smoothly and slow enough not to chew CPU
// on a hot grab loop. Drop (toward 16 ms / 60 fps) only if motion
// trails are visible; raise if magnifier CPU shows up in profiling
// of an idle session.
constexpr int kTickMs = 33;

} // namespace

Magnifier::Magnifier(QWidget *parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint |
                          Qt::WindowTransparentForInput) {
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFocusPolicy(Qt::NoFocus);
    setFixedSize(kSize, kSize);

    m_timer.setInterval(kTickMs);
    connect(&m_timer, &QTimer::timeout, this, &Magnifier::tick);
}

void Magnifier::setTarget(QWidget *target) {
    m_target = target;
}

void Magnifier::setZoomFactor(double factor) {
    m_factor = factor > 1.0 ? factor : 1.0;
}

void Magnifier::activate() {
    if (m_active)
        return;
    m_active = true;
    m_timer.start();
    tick();
}

void Magnifier::deactivate() {
    m_active = false;
    m_timer.stop();
    hide();
    m_snapshot = QImage();
}

void Magnifier::tick() {
    if (!m_active || !m_target) {
        hide();
        return;
    }
    const QPoint globalPos = QCursor::pos();
    const QPoint targetPos = m_target->mapFromGlobal(globalPos);
    if (!m_target->rect().contains(targetPos)) {
        hide();
        return;
    }
    const int srcSize = static_cast<int>(kSize / m_factor);
    const QRect src(targetPos.x() - srcSize / 2, targetPos.y() - srcSize / 2, srcSize, srcSize);
    m_snapshot = m_target->grab(src).toImage();
    move(globalPos.x() - width() / 2, globalPos.y() - height() / 2);
    if (!isVisible()) {
        show();
        raise();
    }
    update();
}

void Magnifier::paintEvent(QPaintEvent * /*event*/) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    QPainterPath clip;
    clip.addEllipse(rect());
    p.setClipPath(clip);

    if (!m_snapshot.isNull()) {
        p.drawImage(rect(), m_snapshot);
    } else {
        p.fillRect(rect(), Qt::white);
    }

    p.setClipping(false);
    QPen pen(QColor(0, 0, 0, 180));
    pen.setWidth(2);
    p.setPen(pen);
    p.drawEllipse(rect().adjusted(1, 1, -1, -1));
}

} // namespace trailer
