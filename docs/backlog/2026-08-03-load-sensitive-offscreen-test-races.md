---
id: 2026-08-03-load-sensitive-offscreen-test-races
title: Two offscreen tests fail only under CPU load (ocr_ev_20 terminal state, perf_lazy_open)
priority: TBD
status: open
source: flaky-test hunt during the 2026-08-02 update-pubkey branch review
created: 2026-08-03
---

## Threshold

Both tests pass **50 consecutive** offscreen runs while the machine is
saturated (e.g. a parallel `cmake --build` running alongside):

```sh
for i in $(seq 1 50); do
  QT_QPA_PLATFORM=offscreen ./build/tests/uat/test_uat_ocr_evidence ocr_ev_20_noTextFoundAfter || echo "OCR FAIL $i"
  QT_QPA_PLATFORM=offscreen ./build/tests/test_perf_lazy_open || echo "LAZY FAIL $i"
done
```

Load is the operative condition: both were **0/30 on an idle machine** and
only reproduce when CPU is contended, so an idle-machine run is not evidence
of a fix.

## Context

**Pre-existing, not introduced by any current branch.** Measured side by side
from an `origin/main` build tree and a
`claude/trailer-coordinator-elk85x` tree:

| test | idle | under load (main) | under load (branch) |
|---|---|---|---|
| `test_uat_ocr_evidence::ocr_ev_20_noTextFoundAfter` | 0/30 | 2/20 | 7/20 |
| `test_perf_lazy_open` | 0/30 | 1/30 | 0/30 |

The differing under-load rates track how contended the machine happened to
be during each sample, not the branch — neither test's code, nor the code it
exercises, differs between the two trees.

### `ocr_ev_20_noTextFoundAfter`

`tests/uat/test_uat_ocr_evidence.cpp:244` asserts the status widget reached
the terminal state:

```
Actual   (mlp->state())              : 0   // MlProgressWidget::Idle
Expected (MlProgressWidget::Terminal): 2
```

`Idle` is the *initial* value, so the widget never entered `Terminal` at all
— the test observes the state too early rather than observing it and then
losing it. The test sets `setProgressRevealDelayMs(0)` and
`setTerminalHoldMs(60000)`, then waits only on
`QTRY_VERIFY(finished.count() >= 1)` (`OcrController::ocrBatchFinished`). If
the status wiring that drives the widget into `Terminal` runs on a queued
connection ordered *after* that signal, the `QCOMPARE` on the next line can
observe `Idle`. Under load the queued slot is more likely to still be
pending.

Likely fix: wait on the observable being asserted rather than on a proxy —
`QTRY_COMPARE(mlp->state(), MlProgressWidget::Terminal)` instead of
`QCOMPARE` after a `QTRY_VERIFY` on a different signal.

### `test_perf_lazy_open`

Fails cleanly (assertion, not crash) under the same conditions. Per
`docs/CONVENTIONS.md` the perf tests are supposed to assert
**counts/ordering, never wall-clock**; a load-sensitive failure suggests one
assertion in this file still has a timing dependency. Identify it and
re-express it structurally.

## Related

- `2026-08-03-quit-teardown-segfault-mlscheduler` — the third flake found in
  the same hunt. Distinct: that one was a `SIGSEGV` in teardown, a
  memory-safety bug rather than a test-timing bug. **Closed**: the diagnosis
  in that item (MlScheduler threads parked on a destroyed condvar) was wrong
  — the parked-worker frames are idle bystanders the crash handler prints for
  every thread. The real cause was an ML worker posting its result to a raw
  `MainWindow *`; fixed via `MlScheduler::postResultToGuiThread()`, guarded by
  `tests/test_ml_callback_lifetime.cpp`.
