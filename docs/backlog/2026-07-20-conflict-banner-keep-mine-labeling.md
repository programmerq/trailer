---
id: 2026-07-20-conflict-banner-keep-mine-labeling
title: External-change conflict banner — "Keep mine" silently overwrites disk now, and no button carries default emphasis
priority: TBD
status: open
source: UX-walkthrough driven-mode audit 2026-07-20 (Persona A worker A2-F4 + Persona B B5)
created: 2026-07-20
---

## Threshold

For the dirty-conflict external-change banner:

1. The button that overwrites the newer on-disk copy states that consequence in
   its label (e.g. "Keep mine (overwrite disk)"), mirroring "Reload (discard my
   edits)" — checkable by reading the rendered button text.
2. Exactly one button carries visual default/primary emphasis (the safe/
   recommended action), so the safe path is distinguishable at a glance from the
   destructive one — checkable in a grab of the banner.
3. The true-conflict mode presents the real fork (Reload vs Keep-mine) without a
   redundant equal-weight "Dismiss" sitting beside the destructive action.

Pass = all three observable in a screenshot of the conflict banner; today all
three fail (bare "Keep mine", four visually identical flat buttons, "Dismiss"
adjacent to the immediate-overwrite action).

## Context

The banner offers `[Reload (discard my edits)] [Keep mine] [Compare (disabled)]
[Dismiss]` as four visually identical flat push-buttons with no default emphasis.
In the wiring at
[`src/ui/MainWindow.cpp:266`](../../src/ui/MainWindow.cpp) (~lines 266–276),
**"Keep mine" is destructive and immediate**: it arms
`setForceSaveOverExternalChange(true)` and calls `saveDocumentAsync` right away,
overwriting the newer VERSION-B on disk *now* — while "Dismiss", right beside it,
merely hides the banner (the next Save re-triggers the guard). The labels convey
none of that gap, and nothing signals which button is safe.

This merges two audit findings on the same control: Persona A's data-loss/labeling
angle (relabel the destructive button) and Persona B's missing-button-hierarchy
parity note (native "file changed" bars give the recommended action visual
primacy). It is orthogonal to the existing external-change edge-case items
(`2026-07-19-autosave-skips-conflict-doc`,
`2026-07-19-external-change-parent-dir-removed`,
`2026-07-19-external-change-same-size-blind-spot`), none of which touch button
labeling or hierarchy. Class TASTE. Relates to "no lying controls" in
[`PHILOSOPHY.md`](../../PHILOSOPHY.md) and the external-change ADR
([`docs/decision-records/2026-07-19-external-file-change-handling.md`](../decision-records/2026-07-19-external-file-change-handling.md)).
A user-visible label/emphasis change may want a G6 note if it touches a ratified default.

## Provenance

Driven against real `build/trailer` (main `6aab23f`), Xvfb+xdotool, dpr=1.
Persona B convention is LLM-recalled (provisional, owner spot-check). Evidence:
`menu-ext/ext-04-dirty-external-change-shows-banner.png`. Curated evidence to
commit under `docs/uat/images/2026-07-20-conflict-banner-keep-mine.png`.
