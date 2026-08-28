#pragma once

// Settling primitive for the PBT harness (tests/pbt/).
//
// EVERY flake in a random-walk harness traces back to settling — an
// assertion racing an async render or a deferred relayout. So settling is
// ONE audited helper, not thirty ad-hoc qWait(50)s scattered through the
// walks (brainstorm-pbt-harness §7 risk 1 / Suggestion 2). Any test in
// tests/pbt/ that needs "the view has finished painting" calls
// waitForPaintQuiescence(); nothing else in this tree may hand-roll a
// wait. The helper has its own unit-style self-test slots in
// test_pbt_walk_min.cpp (settle_*), so a regression here fails loudly
// instead of turning every walk into noise.
//
// Definition of quiescence: no QEvent::Paint delivered to the watched
// widget across kQuietSpins consecutive spin iterations, where one spin =
// processEvents() + a short bounded wait (the wait gives worker-thread
// render completions a chance to post their repaint back to the GUI
// thread — with zero wait, an in-flight async render looks identical to
// "settled"). This is the EventCounter pattern from
// tests/test_perf_paint_budget.cpp, reduced to the one event type this
// helper needs.
//
// Quiescence is a NECESSARY, not sufficient, "done" signal: a view can be
// paint-quiet while showing a blank frame (the exact bug class the walk
// oracles hunt). Callers pair this with a content oracle; this helper only
// answers "has painting stopped for now."

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QObject>
#include <QWidget>
#include <QtTest/QtTest>

namespace trailer::pbt {

// Counts QEvent::Paint delivered to whatever it is installed on.
class PaintCounter : public QObject {
  public:
    int paints = 0;

  protected:
    bool eventFilter(QObject *obj, QEvent *e) override {
        if (e->type() == QEvent::Paint)
            ++paints;
        return QObject::eventFilter(obj, e);
    }
};

// Per-spin wait, ms. What it represents: the window a worker-thread render
// completion gets to post its update() back to the GUI thread before we
// call the spin "quiet". Range tried: 0 (declares quiescence while a
// QPdfView async render is still in flight — false-settled walks), 5..25
// (all stable offscreen; 10 keeps a settled step at ~20 ms overhead).
// Symptom it fixes: raise it if waitForPaintQuiescence returns true while
// the very next grab() is still mid-render (the walk's eventually-non-blank
// oracle then flickers); lower it only to shave walk wall-time.
inline constexpr int kSettleSpinWaitMs = 10;

// Consecutive paint-free spins required. What it represents: "two
// processEvents spins of silence" (the quiescence rule from
// test_perf_paint_budget's usage, promoted to a definition). Range tried:
// 1 (a single quiet spin can fall in the gap between a scroll's first
// paint and the async re-render's second), 2-3 (equivalent offscreen).
// Symptom to change: raise it if a settled step still has a straggler
// paint that trips the NEXT step's strict oracle.
inline constexpr int kSettleQuietSpins = 2;

// Spin the event loop until `w` has been paint-quiet for kSettleQuietSpins
// consecutive spins, or `timeoutMs` elapses. Returns true iff quiescence
// was reached. Pass the widget that actually receives the paints (for a
// QAbstractScrollArea, its viewport()).
inline bool waitForPaintQuiescence(QWidget *w, int timeoutMs) {
    PaintCounter counter;
    w->installEventFilter(&counter);
    QElapsedTimer timer;
    timer.start();
    int quietSpins = 0;
    while (timer.elapsed() < timeoutMs) {
        const int before = counter.paints;
        QCoreApplication::processEvents();
        QTest::qWait(kSettleSpinWaitMs); // pumps events too (bounded, audited — see header)
        if (counter.paints == before) {
            if (++quietSpins >= kSettleQuietSpins) {
                w->removeEventFilter(&counter);
                return true;
            }
        } else {
            quietSpins = 0;
        }
    }
    w->removeEventFilter(&counter);
    return false;
}

} // namespace trailer::pbt
