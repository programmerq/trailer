#include "UxQtEventCapture.h"

#include <QApplication>
#include <QDialog>
#include <QEvent>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMenu>
#include <QMouseEvent>
#include <QShortcutEvent>
#include <QWheelEvent>
#include <QWidget>

namespace trailer {

namespace {

// One mouse-path sample at most every this many ms. 30 ms (~33 Hz)
// keeps gesture shape recognisable while cutting the raw 60–120 Hz
// move stream to a third or less; raise it if events.jsonl growth
// ever becomes the bottleneck, lower it for fine ink-stroke analysis.
constexpr quint64 kMouseSampleIntervalMs = 30;

// Self-flush ceiling for one mouse_path event. At ~33 Hz this is
// ~4.5 s of continuous movement — longer gestures simply split into
// consecutive events.
constexpr int kMaxMousePathSamples = 150;

QString describeButton(Qt::MouseButton button) {
    switch (button) {
    case Qt::LeftButton:
        return QStringLiteral("left");
    case Qt::RightButton:
        return QStringLiteral("right");
    case Qt::MiddleButton:
        return QStringLiteral("middle");
    case Qt::BackButton:
        return QStringLiteral("back");
    case Qt::ForwardButton:
        return QStringLiteral("forward");
    default:
        return QStringLiteral("other");
    }
}

QString describeModifiers(Qt::KeyboardModifiers mods) {
    QStringList parts;
    if (mods & Qt::ControlModifier)
        parts << QStringLiteral("ctrl");
    if (mods & Qt::ShiftModifier)
        parts << QStringLiteral("shift");
    if (mods & Qt::AltModifier)
        parts << QStringLiteral("alt");
    if (mods & Qt::MetaModifier)
        parts << QStringLiteral("meta");
    if (mods & Qt::KeypadModifier)
        parts << QStringLiteral("keypad");
    return parts.join(QLatin1Char('+'));
}

// Root-first chain of className(objectName) segments, e.g.
// "trailer::MainWindow/QWidget/trailer::DocumentView(documentView)".
// Identifies *where* input landed without the recorder knowing any
// widget specifically.
QString widgetPath(const QObject *object) {
    QStringList chain;
    int depth = 0;
    for (const QObject *o = object; o && depth < 10; o = o->parent(), ++depth) {
        QString segment = QString::fromLatin1(o->metaObject()->className());
        const QString name = o->objectName();
        if (!name.isEmpty()) {
            segment += QLatin1Char('(') + name + QLatin1Char(')');
        }
        chain.prepend(segment);
    }
    return chain.join(QLatin1Char('/'));
}

QString windowTitle(const QWidget *w) {
    // Window titles routinely carry the document name — useful
    // context, already local-only by design.
    return w ? w->windowTitle() : QString();
}

} // namespace

UxQtEventCapture::UxQtEventCapture(Sink sink, QObject *parent)
    : QObject(parent), m_sink(std::move(sink)) {}

bool UxQtEventCapture::eventFilter(QObject *watched, QEvent *event) {
    // Hot path: this filter sees every event for every object in the
    // application. Dispatch on type first and fall through fast for
    // anything we don't record.
    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
        if (!isDuplicateDelivery(event)) {
            recordMouseButton(watched, event);
        }
        break;
    case QEvent::MouseMove:
        if (!isDuplicateDelivery(event)) {
            sampleMouseMove(event);
        }
        break;
    case QEvent::Wheel:
        if (!isDuplicateDelivery(event)) {
            accumulateWheel(watched, event);
        }
        break;
    case QEvent::KeyPress:
    case QEvent::KeyRelease:
        if (!isDuplicateDelivery(event)) {
            recordKey(watched, event);
        }
        break;
    case QEvent::Shortcut: {
        auto *se = static_cast<QShortcutEvent *>(event);
        m_sink(QStringLiteral("shortcut"),
               QJsonObject{{QStringLiteral("sequence"), se->key().toString()},
                           {QStringLiteral("widget"), widgetPath(watched)}});
        break;
    }
    case QEvent::FocusIn:
        if (watched->isWidgetType()) {
            auto *fe = static_cast<QFocusEvent *>(event);
            m_sink(QStringLiteral("focus_changed"),
                   QJsonObject{{QStringLiteral("widget"), widgetPath(watched)},
                               {QStringLiteral("reason"), static_cast<int>(fe->reason())}});
        }
        break;
    case QEvent::WindowActivate:
        if (watched->isWidgetType()) {
            auto *w = static_cast<QWidget *>(watched);
            if (w->isWindow()) {
                m_sink(QStringLiteral("window_activated"),
                       QJsonObject{{QStringLiteral("widget"), widgetPath(w)},
                                   {QStringLiteral("title"), windowTitle(w)}});
            }
        }
        break;
    case QEvent::Show:
    case QEvent::Hide:
        recordShowHide(watched, event);
        break;
    default:
        break;
    }
    // Pure observer: never consume.
    return false;
}

bool UxQtEventCapture::isDuplicateDelivery(QEvent *event) {
    auto *input = static_cast<QInputEvent *>(event);
    const int type = static_cast<int>(event->type());
    const quint64 timestamp = input->timestamp();
    if (type == m_lastInputType && timestamp != 0 && timestamp == m_lastInputTimestamp) {
        return true;
    }
    m_lastInputType = type;
    m_lastInputTimestamp = timestamp;
    return false;
}

void UxQtEventCapture::recordMouseButton(QObject *watched, QEvent *event) {
    // Flush movement first so the click lands *after* the path that
    // led to it in the stream.
    flushMousePath();

    auto *me = static_cast<QMouseEvent *>(event);
    QString action;
    switch (event->type()) {
    case QEvent::MouseButtonPress:
        action = QStringLiteral("press");
        break;
    case QEvent::MouseButtonRelease:
        action = QStringLiteral("release");
        break;
    default:
        action = QStringLiteral("double_click");
        break;
    }
    const QPointF global = me->globalPosition();
    const QPointF local = me->position();
    m_sink(
        QStringLiteral("mouse_button"),
        QJsonObject{{QStringLiteral("action"), action},
                    {QStringLiteral("button"), describeButton(me->button())},
                    {QStringLiteral("global"), QJsonArray{qRound(global.x()), qRound(global.y())}},
                    {QStringLiteral("local"), QJsonArray{qRound(local.x()), qRound(local.y())}},
                    {QStringLiteral("modifiers"), describeModifiers(me->modifiers())},
                    {QStringLiteral("widget"), widgetPath(watched)}});
}

void UxQtEventCapture::sampleMouseMove(QEvent *event) {
    auto *me = static_cast<QMouseEvent *>(event);
    const quint64 timestamp = me->timestamp();
    if (timestamp != 0 && m_lastMouseSampleTimestamp != 0 &&
        timestamp - m_lastMouseSampleTimestamp < kMouseSampleIntervalMs) {
        return;
    }
    m_lastMouseSampleTimestamp = timestamp;
    const QPointF global = me->globalPosition();
    // buttons() distinguishes a drag (non-zero) from a hover move.
    m_mouseSamples.append(
        QJsonArray{qRound(global.x()), qRound(global.y()), static_cast<int>(me->buttons())});
    if (m_mouseSamples.size() >= kMaxMousePathSamples) {
        flushMousePath();
    }
}

void UxQtEventCapture::accumulateWheel(QObject *watched, QEvent *event) {
    auto *we = static_cast<QWheelEvent *>(event);
    m_wheelEventCount++;
    m_wheelAngleX += we->angleDelta().x();
    m_wheelAngleY += we->angleDelta().y();
    if (m_wheelWidget.isEmpty()) {
        m_wheelWidget = widgetPath(watched);
    }
}

void UxQtEventCapture::recordKey(QObject *watched, QEvent *event) {
    auto *ke = static_cast<QKeyEvent *>(event);
    QJsonObject data{
        {QStringLiteral("action"),
         event->type() == QEvent::KeyPress ? QStringLiteral("press") : QStringLiteral("release")},
        {QStringLiteral("key"),
         QKeySequence(QKeyCombination(ke->modifiers(), static_cast<Qt::Key>(ke->key())))
             .toString(QKeySequence::PortableText)},
        {QStringLiteral("modifiers"), describeModifiers(ke->modifiers())},
        {QStringLiteral("auto_repeat"), ke->isAutoRepeat()},
        {QStringLiteral("widget"), widgetPath(watched)},
    };
    if (m_captureKeyText && !ke->text().isEmpty() && ke->text().at(0).isPrint()) {
        data.insert(QStringLiteral("text"), ke->text());
    }
    m_sink(QStringLiteral("key"), data);
}

void UxQtEventCapture::recordShowHide(QObject *watched, QEvent *event) {
    if (!watched->isWidgetType()) {
        return;
    }
    auto *w = static_cast<QWidget *>(watched);
    if (!w->isWindow()) {
        return;
    }
    const QByteArray className(w->metaObject()->className());
    // Tooltips appear/disappear constantly under a moving cursor and
    // say nothing the hover position doesn't; drop them.
    if (className.contains("QTipLabel")) {
        return;
    }
    const bool shown = event->type() == QEvent::Show;
    QString type;
    if (qobject_cast<QDialog *>(w)) {
        type = shown ? QStringLiteral("dialog_opened") : QStringLiteral("dialog_closed");
    } else if (qobject_cast<QMenu *>(w)) {
        type = shown ? QStringLiteral("menu_opened") : QStringLiteral("menu_closed");
    } else {
        type = shown ? QStringLiteral("window_shown") : QStringLiteral("window_hidden");
    }
    QJsonObject data{{QStringLiteral("class"), QString::fromLatin1(className)},
                     {QStringLiteral("title"), windowTitle(w)}};
    if (auto *menu = qobject_cast<QMenu *>(w)) {
        data.insert(QStringLiteral("menu"), menu->title());
    }
    if (auto *dialog = qobject_cast<QDialog *>(w)) {
        data.insert(QStringLiteral("modal"), dialog->isModal());
    }
    m_sink(type, data);
}

void UxQtEventCapture::flushPending() {
    flushMousePath();
    flushWheel();
}

void UxQtEventCapture::flushMousePath() {
    if (m_mouseSamples.isEmpty()) {
        return;
    }
    QJsonArray samples;
    std::swap(samples, m_mouseSamples);
    m_sink(QStringLiteral("mouse_path"),
           QJsonObject{{QStringLiteral("sample_count"), samples.size()},
                       {QStringLiteral("samples"), samples}});
}

void UxQtEventCapture::flushWheel() {
    if (m_wheelEventCount == 0) {
        return;
    }
    m_sink(QStringLiteral("wheel"), QJsonObject{{QStringLiteral("events"), m_wheelEventCount},
                                                {QStringLiteral("angle_x"), m_wheelAngleX},
                                                {QStringLiteral("angle_y"), m_wheelAngleY},
                                                {QStringLiteral("widget"), m_wheelWidget}});
    m_wheelEventCount = 0;
    m_wheelAngleX = 0;
    m_wheelAngleY = 0;
    m_wheelWidget.clear();
}

} // namespace trailer
