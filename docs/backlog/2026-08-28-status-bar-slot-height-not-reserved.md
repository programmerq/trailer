---
id: 2026-08-28-status-bar-slot-height-not-reserved
title: Status-bar reserved slots pin width but not height — revealing taller content nudges every permanent widget vertically
priority: TBD
status: open
source: first run of the generalized constancy sweep (UAT-XCT-093, tests/uat/test_uat_constancy_sweep.cpp)
created: 2026-08-28
---

## Threshold

The four `KnownDefect` tolerance entries referencing this item in
`tests/uat/test_uat_constancy_sweep.cpp` (toggle rows `ml-indicator`,
`read-only-badge`, `large-doc-ocr-hint`, `ml-progress`) are **deleted**, and

```sh
QT_QPA_PLATFORM=offscreen ./build/tests/uat/test_uat_constancy_sweep
```

passes with the strict (no-tolerance) oracle: revealing any status-bar
content widget moves **no** furniture element by any amount, in either
axis.

## Context

`reserveStatusBarSlot()` (src/ui/MainWindow.cpp) fixed the SC-CRIT-1
class horizontally: each permanent widget sits in a fixed-**width** slot,
so toggling one never changes a sibling's x position — the sweep confirms
x holds everywhere. But slot **height** was never reserved. Content that
is taller at reveal than the bar's at-rest height — `mlIndicator` (frame
+ 2px margin, +3px), `twoPageReadOnlyBadge` (styled border/padding,
+1px), `largeDocOcrHint` (+4px), `mlProgress` (+4px) — grows the whole
QStatusBar, which moves the bar's top edge up and re-centres every
permanent widget vertically. Measured on the first sweep run
(offscreen, 1100x750):

```
ml-indicator:       QStatusBar (0,728)->(0,725); every slot y 731->728
read-only-badge:    QStatusBar (0,728)->(0,727); every slot y 731->730
large-doc-ocr-hint: QStatusBar (0,728)->(0,724); every slot y 731->727
ml-progress:        QStatusBar (0,728)->(0,724); every slot y 731->727
```

Same G10 defect class as the 41px horizontal shift `uat_zoom_ind_070`
caught — the ML progress widget's Cancel button (the SC-CRIT-1 concrete
repro) still moves under the pointer, just vertically and by less.
`ocrModelMissingHint` (a plain unstyled QLabel) does not exceed the
at-rest height, which is why that row passes strict today.

Likely fix shape: reserve slot height the way width is reserved — size
each slot (or the status bar) to the tallest content it can ever show,
measured from the content's own sizeHint at construction time, per the
`slotWidthFor()` cross-platform note. That is a user-visible layout
change, so it takes the usual gate evidence; this item exists so the
sweep can gate at release meanwhile without burying the finding.
