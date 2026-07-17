#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include <functional>

namespace trailer {

// Application-wide Qt input/UI observer for the UX recorder.
//
// Installed as an event filter on the QApplication while a session is
// active, so Trailer-local activity is captured without touching any
// widget code. Emits structured events (source "qt") through the sink
// supplied at construction:
//
//   mouse_button      press / release / double-click with widget path
//   mouse_path        coalesced movement samples (see below)
//   wheel             coalesced scroll deltas
//   key               press/release, shortcut text, printable text
//   shortcut          QShortcutEvent activations
//   focus_changed     keyboard focus moves between widgets
//   window_activated  a top-level Trailer window became active
//   dialog_opened / dialog_closed / menu_opened / menu_closed /
//   window_shown / window_hidden
//
// High-frequency traffic never writes one disk record per event:
// mouse moves are sampled at kMouseSampleIntervalMs and batched into
// a single mouse_path event (flushed by the recorder's timer, before
// any button event so gestures stay ordered, or when the batch is
// full); wheel ticks accumulate into one event per flush.
//
// Everything here runs on the GUI thread (Qt delivers events there);
// flushPending() must be called from the GUI thread too.
class UxQtEventCapture : public QObject {
    Q_OBJECT

  public:
    using Sink = std::function<void(const QString &type, const QJsonObject &data)>;

    explicit UxQtEventCapture(Sink sink, QObject *parent = nullptr);

    // When false, key events omit the printable text() payload and
    // keep only key/modifier identity. Default true — this recorder
    // is for the developer's own sessions (docs/ux-recorder.md).
    void setCaptureKeyText(bool capture) { m_captureKeyText = capture; }
    bool captureKeyText() const { return m_captureKeyText; }

    // Emit any batched mouse-path / wheel data. Called by the
    // recorder's flush timer and before stop.
    void flushPending();

    bool eventFilter(QObject *watched, QEvent *event) override;

  private:
    void recordMouseButton(QObject *watched, QEvent *event);
    void sampleMouseMove(QEvent *event);
    void accumulateWheel(QObject *watched, QEvent *event);
    void recordKey(QObject *watched, QEvent *event);
    void recordShowHide(QObject *watched, QEvent *event);
    void flushMousePath();
    void flushWheel();
    // Application-level filters see one delivery per object on the
    // propagation chain (key and mouse events bubble to parents).
    // Input events keep their device timestamp through propagation,
    // so a (type, timestamp) pair identifies the underlying event.
    bool isDuplicateDelivery(QEvent *event);

    Sink m_sink;
    bool m_captureKeyText = true;

    // Last recorded (type, QInputEvent::timestamp) for dedup.
    int m_lastInputType = 0;
    quint64 m_lastInputTimestamp = 0;

    // Mouse-path batch: [elapsed-relative ms handled by stream] —
    // each sample is [x, y, buttons] in global coordinates plus the
    // device timestamp, downsampled to one per kMouseSampleIntervalMs.
    QJsonArray m_mouseSamples;
    quint64 m_lastMouseSampleTimestamp = 0;

    // Wheel accumulator.
    int m_wheelEventCount = 0;
    int m_wheelAngleX = 0;
    int m_wheelAngleY = 0;
    QString m_wheelWidget;
};

} // namespace trailer
