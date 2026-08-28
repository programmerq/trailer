---
id: 2026-08-28-fitmode-nav-posted-event-livelock
title: GUI-thread livelock — fit-mode churn followed by navigation triggers a deferred show/hide feedback loop (scrollbar flap) with O(queue) compressEvent scan
priority: TBD
status: open
source: PBT S0 shakedown (tests/pbt/test_pbt_walk_min.cpp), 2026-08-28 — 4 hangs in ~170 seeded walks, 3 seeds recorded, each 100% reproducible on replay
created: 2026-08-28
---

## Threshold

The three recorded seeds replay to completion (all 30 steps, oracles green,
no timeout) under `tests/pbt/test_pbt_walk_min.cpp`:

    TRAILER_PBT_SEED=720458898   # 38 pages; hangs at step 19 goToPage(11)
    TRAILER_PBT_SEED=84349732    # 40 pages; hangs at step 23 viewModeToggle
    TRAILER_PBT_SEED=269782153   # 33 pages; hangs at step 24 nextPage

and each seed's minimized trace is committed as a deterministic `uat`-labelled
regression guard (the ratchet rule) in the fixing PR.

## Symptom

Main thread pegged at 99% CPU, UI fully frozen. Two independent gdb stacks,
same signature: `QFrame::event → QWidgetPrivate::setVisible →
QCoreApplication::postEvent → QApplicationPrivate::compressEvent` — a
deferred show/hide feedback loop. Shape: at a fit zoom, showing the
scrollbar changes viewport width → re-fit changes content size → scrollbar
hides → width changes back → re-fit → scrollbar shows… with compressEvent's
O(queue) scan over the growing posted-event list making the loop effectively
permanent. Common prefix across all three traces: fit-width / view-mode
churn, then a navigation (`goToPage` / `nextPage` / `viewModeToggle`).

These are the app's own menu-action paths (`goToPage(nextPageIndex())`),
so a user can plausibly hit this as a total UI freeze — it is a candidate
root cause for "the app locks up sometimes" class reports, and it violates
spine clause INV-03 (the interface never waits on the document).

## Notes

- Found by the S0 property walk on its first ~170 runs; full
  `{seed, pageSpecs, actions}` replay JSONs print via the harness's
  PBT-GENERATED stderr line for each seed above.
- Likely fix direction: Qt's classic scrollbar-flap mitigation is breaking
  the feedback cycle (e.g. `Qt::ScrollBarAlwaysOn/AlwaysOff` during fit
  recompute, or geometry-change re-entrancy guard). Diagnose before
  choosing; do NOT paper over by disabling fit modes in the walk.
