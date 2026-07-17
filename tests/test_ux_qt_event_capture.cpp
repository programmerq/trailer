#include "uxrecord/UxQtEventCapture.h"

#include <QApplication>
#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QObject>
#include <QWidget>
#include <QtTest/QtTest>

using namespace trailer;

namespace {

struct Captured {
    QString type;
    QJsonObject data;
};

} // namespace

// Coalescing / batching behaviour of the Qt-side observer. Events are
// synthesised with explicit device timestamps and delivered through
// QApplication::sendEvent so the tests are deterministic under the
// offscreen platform.
class TestUxQtEventCapture : public QObject {
    Q_OBJECT
  private slots:
    void mouseMovesCoalesceIntoOnePath();
    void pathFlushPrecedesButtonEvent();
    void duplicatePropagatedDeliveryIsRecordedOnce();
    void keyEventsCarryTextOnlyWhenEnabled();
    void wheelAccumulates();
    void dialogShowHideReported();

  private:
    QList<Captured> m_events;
    UxQtEventCapture *makeCapture(QObject *parent) {
        m_events.clear();
        return new UxQtEventCapture(
            [this](const QString &type, const QJsonObject &data) { m_events.append({type, data}); },
            parent);
    }
    int countOfType(const QString &type) const {
        int n = 0;
        for (const auto &e : m_events) {
            if (e.type == type) {
                ++n;
            }
        }
        return n;
    }
    static QMouseEvent *moveEvent(const QPointF &pos, quint64 timestamp) {
        auto *event = new QMouseEvent(QEvent::MouseMove, pos, pos, pos, Qt::NoButton, Qt::NoButton,
                                      Qt::NoModifier);
        event->setTimestamp(timestamp);
        return event;
    }
};

void TestUxQtEventCapture::mouseMovesCoalesceIntoOnePath() {
    QWidget widget;
    auto *capture = makeCapture(&widget);

    // 12 moves 5 ms apart: far below the 30 ms sampling interval, so
    // the batch must contain far fewer samples than raw events, and
    // nothing is emitted until flushPending().
    for (int i = 0; i < 12; ++i) {
        QScopedPointer<QMouseEvent> e(
            moveEvent(QPointF(10 + i, 20), static_cast<quint64>(1000 + i * 5)));
        capture->eventFilter(&widget, e.data());
    }
    QCOMPARE(countOfType(QStringLiteral("mouse_path")), 0);

    capture->flushPending();
    QCOMPARE(countOfType(QStringLiteral("mouse_path")), 1);
    const QJsonObject path = m_events.last().data;
    const int samples = path.value(QStringLiteral("sample_count")).toInt();
    QVERIFY2(samples >= 1 && samples <= 4,
             qPrintable(QStringLiteral("expected heavy coalescing, got %1").arg(samples)));

    // Nothing pending → flush again emits nothing.
    const auto before = m_events.size();
    capture->flushPending();
    QCOMPARE(m_events.size(), before);
}

void TestUxQtEventCapture::pathFlushPrecedesButtonEvent() {
    QWidget widget;
    auto *capture = makeCapture(&widget);

    QScopedPointer<QMouseEvent> move(moveEvent(QPointF(5, 5), 2000));
    capture->eventFilter(&widget, move.data());

    QMouseEvent press(QEvent::MouseButtonPress, QPointF(6, 6), QPointF(6, 6), QPointF(6, 6),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    press.setTimestamp(2050);
    capture->eventFilter(&widget, &press);

    QCOMPARE(m_events.size(), 2);
    QCOMPARE(m_events[0].type, QStringLiteral("mouse_path"));
    QCOMPARE(m_events[1].type, QStringLiteral("mouse_button"));
    QCOMPARE(m_events[1].data.value(QStringLiteral("action")).toString(), QStringLiteral("press"));
    QCOMPARE(m_events[1].data.value(QStringLiteral("button")).toString(), QStringLiteral("left"));
}

void TestUxQtEventCapture::duplicatePropagatedDeliveryIsRecordedOnce() {
    QWidget parent;
    QWidget child(&parent);
    auto *capture = makeCapture(&parent);

    // Qt propagates an ignored input event up the parent chain as the
    // *same* event object — an application-level filter sees one
    // delivery per widget. The (type, timestamp) dedup must keep one.
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(1, 1), QPointF(1, 1), QPointF(1, 1),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    press.setTimestamp(3000);
    capture->eventFilter(&child, &press);
    capture->eventFilter(&parent, &press);
    QCOMPARE(countOfType(QStringLiteral("mouse_button")), 1);

    // A genuinely new event (different timestamp) records again.
    QMouseEvent press2(QEvent::MouseButtonPress, QPointF(1, 1), QPointF(1, 1), QPointF(1, 1),
                       Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    press2.setTimestamp(3100);
    capture->eventFilter(&child, &press2);
    QCOMPARE(countOfType(QStringLiteral("mouse_button")), 2);
}

void TestUxQtEventCapture::keyEventsCarryTextOnlyWhenEnabled() {
    QWidget widget;
    auto *capture = makeCapture(&widget);

    QKeyEvent press(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, QStringLiteral("a"));
    press.setTimestamp(4000);
    capture->eventFilter(&widget, &press);
    QCOMPARE(countOfType(QStringLiteral("key")), 1);
    QCOMPARE(m_events.last().data.value(QStringLiteral("text")).toString(), QStringLiteral("a"));

    // The privacy escape hatch: identity stays, printable text goes.
    capture->setCaptureKeyText(false);
    QKeyEvent press2(QEvent::KeyPress, Qt::Key_B, Qt::NoModifier, QStringLiteral("b"));
    press2.setTimestamp(4100);
    capture->eventFilter(&widget, &press2);
    QCOMPARE(countOfType(QStringLiteral("key")), 2);
    QVERIFY(!m_events.last().data.contains(QStringLiteral("text")));
    QVERIFY(!m_events.last().data.value(QStringLiteral("key")).toString().isEmpty());
}

void TestUxQtEventCapture::wheelAccumulates() {
    QWidget widget;
    auto *capture = makeCapture(&widget);

    for (int i = 0; i < 5; ++i) {
        QWheelEvent wheel(QPointF(10, 10), QPointF(10, 10), QPoint(), QPoint(0, -120), Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false);
        wheel.setTimestamp(static_cast<quint64>(5000 + i * 10));
        capture->eventFilter(&widget, &wheel);
    }
    QCOMPARE(countOfType(QStringLiteral("wheel")), 0);

    capture->flushPending();
    QCOMPARE(countOfType(QStringLiteral("wheel")), 1);
    const QJsonObject wheel = m_events.last().data;
    QCOMPARE(wheel.value(QStringLiteral("events")).toInt(), 5);
    QCOMPARE(wheel.value(QStringLiteral("angle_y")).toInt(), -600);
}

void TestUxQtEventCapture::dialogShowHideReported() {
    QDialog dialog;
    auto *capture = makeCapture(&dialog);

    QEvent show(QEvent::Show);
    capture->eventFilter(&dialog, &show);
    QCOMPARE(countOfType(QStringLiteral("dialog_opened")), 1);

    QEvent hide(QEvent::Hide);
    capture->eventFilter(&dialog, &hide);
    QCOMPARE(countOfType(QStringLiteral("dialog_closed")), 1);
}

QTEST_MAIN(TestUxQtEventCapture)
#include "test_ux_qt_event_capture.moc"
