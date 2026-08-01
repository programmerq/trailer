#pragma once

#include <QString>
#include <QWidget>

class QLabel;
class QProgressBar;
class QTimer;
class QToolButton;

namespace trailer {

// Status-bar progress + cancel affordance for long-running ML
// operations (ADR 0002). Deliberately self-contained: it knows nothing
// about OcrController, MlScheduler or any ML type — MainWindow drives it
// by calling begin*/setProgress/finishWithMessage in response to a
// controller's batch signals, and forwards the widget's cancelRequested()
// back to the controller. This keeps the widget trivially unit-testable
// and reusable by future ops (background removal) without a dependency
// edge into the ML layer.
//
// One indicator style is fixed per run (ADR 0002 §1 / B5 "never switch
// spinner↔bar"): determinate when the unit count is known (OCR batch of
// N pages), indeterminate otherwise (background removal, single-page).
// setElapsedSeconds() appends reassurance text past 10s WITHOUT changing
// the style — it stays a busy bar.
class MlProgressWidget : public QWidget {
    Q_OBJECT
  public:
    // Lifecycle of a single run. Idle = hidden. Running = a task is in
    // flight and the ✕ is offered. Terminal = a brief "done/cancelled"
    // message is showing before the widget hides itself.
    enum State { Idle, Running, Terminal };

    explicit MlProgressWidget(QWidget *parent = nullptr);

    // Determinate: proportional bar over [0, total] with an
    // "<label> — done / total pages" counter. total < 1 is clamped to a
    // busy bar so the widget never divides by zero.
    void beginDeterminate(const QString &label, int total);

    // Indeterminate: busy bar for unknown-length work.
    void beginIndeterminate(const QString &label);

    // Determinate only: advance the counter/bar. Ignored while
    // indeterminate.
    void setProgress(int done);

    // Indeterminate only: once s >= 10, append " · Ns" to the label as
    // reassurance (ADR 0002 §1). Not a style switch — the bar stays busy.
    void setElapsedSeconds(int s);

    // Hide the bar and ✕, show `msg` for a short hold, then goIdle().
    void finishWithMessage(const QString &msg);

    // Hide the whole widget and reset run state.
    void goIdle();

    // --- state accessors (mainly for tests) ---
    bool isDeterminate() const { return m_determinate; }
    int total() const { return m_total; }
    int value() const;
    bool cancelVisible() const { return m_state == Running; }
    QString labelText() const;
    State state() const { return m_state; }

    // Test seam: how long the terminal message lingers before goIdle().
    // Production default (~3s) matches ADR 0002; tests set it to 0 to
    // observe the return-to-idle without wall-clock waiting.
    void setTerminalHoldMs(int ms) { m_terminalHoldMs = ms; }

    // G10 (spatial constancy, AGENTS.md): MainWindow reserves a fixed-width
    // status-bar slot for this widget (src/ui/MainWindow.cpp,
    // reserveStatusBarSlot()) so an unrelated sibling widget toggling never
    // nudges the Cancel button the user may be mid-click on (SC-CRIT-1,
    // docs/audit-2026-07-31-g10-deference.md). kMaxWidth is the width that
    // slot reserves; it must stay >= this widget's true maximum sizeHint in
    // every state (Running/determinate, Running/indeterminate, Terminal) or
    // the reservation is a lie. The Running states pair a SHORTER label cap
    // (kRunningLabelMaxWidth) with the bar+cancel button; Terminal hides
    // both and affords a WIDER cap (kTerminalLabelMaxWidth) for the longer
    // completion sentence — so the reservation is sized to
    // max(Running total, Terminal total), not their sum. Running total =
    // left+right layout margins (4+4) + kRunningLabelMaxWidth + inter-
    // widget spacing (6+6) + the progress bar (kBarWidth, fixed) + the
    // cancel button (~27px measured for a single-glyph autoRaise
    // QToolButton) = 397px; Terminal total = margins (4+4) +
    // kTerminalLabelMaxWidth = 308px. 400 rounds the larger of the two up
    // for a safety margin against font-metrics jitter — measured via an
    // offscreen probe against the real widget tree, not guessed. This
    // budget also has to fit alongside the other status-bar reserved
    // slots within the app's default launch width (MainWindow::
    // MainWindow() `resize(1100, 750)`) in the worst-case conjunction of
    // every permanent widget active at once — see
    // reserveStatusBarSlot()'s comment in MainWindow.cpp. Revisit if a
    // future locale's cancel glyph or label font measures wider than this
    // budget (the widget would then silently overflow its reserved slot).
    static constexpr int kMaxWidth = 400;

  signals:
    void cancelRequested();

  private:
    void updateDeterminateLabel();
    // Elides to `maxWidth` (via QFontMetrics) rather than growing the
    // label unboundedly, so this widget's own sizeHint never exceeds its
    // caller's width budget for the current state -- progress counters
    // and elapsed seconds grow, and completion messages vary. The full,
    // un-elided text is always reachable via the label's tooltip.
    void setLabelText(const QString &text, int maxWidth);

    QLabel *m_label = nullptr;
    QProgressBar *m_bar = nullptr;
    QToolButton *m_cancel = nullptr;
    QTimer *m_terminalTimer = nullptr;
    // Hand-tuned (PHILOSOPHY.md), Running state: paired with the bar and
    // cancel button, so this stays narrow to keep kMaxWidth's reserved
    // status-bar footprint bounded. 230px comfortably fits the longest
    // realistic Running label ("Recognising text — 123 / 456 pages",
    // measured 217px) without eliding; revisit if a real multi-thousand-
    // page batch is reported with a truncated running counter.
    static constexpr int kRunningLabelMaxWidth = 230;
    // Hand-tuned, Terminal state: the bar and cancel button are hidden
    // here, so this label can afford to be wider without growing
    // kMaxWidth — sized to fit the longest shipped completion sentence,
    // "Text recognition cancelled — no changes saved" (measured 285px),
    // with a safety margin; revisit if a future completion message is
    // reported truncated.
    static constexpr int kTerminalLabelMaxWidth = 300;
    // Fixed so the bar never itself contributes to width variance;
    // pre-existing value, named here so kMaxWidth's arithmetic is
    // traceable to one source instead of a second copy of "120".
    static constexpr int kBarWidth = 120;

    State m_state = Idle;
    bool m_determinate = false;
    int m_total = 0;
    int m_done = 0;
    int m_elapsed = 0;
    QString m_baseLabel;
    // PHILOSOPHY: hand-tuned values stay hand-tuned. How long the terminal
    // "complete / cancelled — no changes saved" message lingers before the
    // widget hides itself. The range considered was 2000–4000ms: below ~2s
    // the outcome message is gone before a glancing user reads it; above
    // ~4s a dead status-bar message overstays and reads as stuck. 3000ms
    // reads as an acknowledgement without lingering. Change it only if the
    // terminal message is reported as missed (raise) or as stale/stuck
    // (lower); tests set it to 0/10ms to observe return-to-idle instantly.
    int m_terminalHoldMs = 3000;
};

} // namespace trailer
