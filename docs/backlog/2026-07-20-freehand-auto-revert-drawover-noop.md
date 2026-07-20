---
id: 2026-07-20-freehand-auto-revert-drawover-noop
title: Freehand auto-reverts to Select after every stroke; a second drag silently no-ops
priority: TBD
status: open
source: UX-walkthrough driven-mode audit 2026-07-20 (Persona A worker A3-F3)
created: 2026-07-20
---

## Threshold

After committing one freehand stroke, the Freehand tool stays active so a second
consecutive drag draws a second stroke without a toolbar round-trip. Checkable:
select Freehand, draw a stroke, drag again — the second drag produces a visible
stroke (not a silent rubber-band select). If the product decision keeps
auto-revert, then the failed drag must NOT be silent: the tool change is signalled
and/or the inert drag gives feedback (G3 spirit / cognitive-walkthrough Q4). One
of those two observable outcomes must hold; the current "second drag draws
nothing, with no feedback" state fails.

## Context

`MainWindow::onAnnotationCommitted`
([`src/ui/MainWindow.cpp:3573`](../../src/ui/MainWindow.cpp)) unconditionally
calls `setActiveTool(Select)` after **every** annotation commit — no exclusion
for Ink/Freehand and no sticky-draw mode (the in-code comment notes sticky "would
be an opt-in setting", i.e. unimplemented). Freehand is inherently multi-stroke
(sketching, ticking boxes, circling several things), so reverting after each
squiggle forces a toolbar round-trip per stroke, and the failed second drag
becomes a silent rubber-band select with no visible result.

Recommended direction: exclude Ink from the auto-revert, or add a sticky-draw
toggle. Rectangle/Line/Arrow reverting is defensible; Freehand reverting is the
friction case. Predates #91 (the comment cites the 2026-05-20 HITL pass); #91
reworked freehand latency/selection but did not revisit the revert. Not covered
by any existing backlog item (TODO.md marks the Freehand tool "Done").

## Provenance

Driven against real `build/trailer` (main `6aab23f`), Xvfb+xdotool, dpr=1.
Evidence: `draw-zoom/step-15-drawover-at-156.png` (empty second drag),
`draw-zoom/step-15-toolbar-crop.png` (Select highlighted), vs
`draw-zoom/step-16-drawover-reselected-156.png` (draws after re-selecting
Freehand). Curated evidence to commit under
`docs/uat/images/2026-07-20-freehand-drawover-noop.png`.
