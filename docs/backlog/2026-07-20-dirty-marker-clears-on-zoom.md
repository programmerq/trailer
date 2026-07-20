---
id: 2026-07-20-dirty-marker-clears-on-zoom
title: Title "•" unsaved marker disappears on zoom when a doc is dirty only via annotations
priority: TBD
status: open
source: UX-walkthrough driven-mode audit 2026-07-20 (Persona A worker A3-F4)
created: 2026-07-20
---

## Threshold

The window-title "•" unsaved marker reflects `doc->isDirty()` at all times,
including after any zoom action. Checkable: open an image, draw a freehand stroke
(title gains "• "), zoom in and out — the "•" persists as long as the annotation
is unsaved. It must NOT vanish on zoom while the annotation still exists. (Rotate,
which sets pixel dirtiness, already keeps the "•" across zoom — annotation-only
dirtiness must behave the same.)

## Context

`ImageDocument::isDirty()`
([`src/document/ImageAdapter.h:110`](../../src/document/ImageAdapter.h)) is true
when either `m_dirty` OR `!m_annotations.annotations().empty()`.
`updateTitleForDocument`
([`src/ui/MainWindow.cpp:2807`](../../src/ui/MainWindow.cpp)) keys the "•" on
`isDirty()`, so a fresh annotation correctly shows the marker — but on the zoom
path the title recompute drops the annotation term, so after a zoom an
annotation-only-dirty doc loses its "•" even though the annotation persists and
the doc is unsaved. Discriminator confirming the scope: after a **rotate**
(pixel edit, `m_dirty=true`) the "•" survives zoom; the loss is specific to
annotation-only dirtiness.

Harm is confined to the visible title cue (a "lying" status signal) — the
close-save-guard reads `doc->isDirty()` directly
(`MainWindow.cpp:198,740`), so annotations are still protected on close, hence
Sev 2 not higher. Class is REGRESSION-SUSPECT: the marker should track dirtiness
unconditionally. Relates to the "no lying controls / honest surfaces" spirit in
[`PHILOSOPHY.md`](../../PHILOSOPHY.md).

## Provenance

Driven against real `build/trailer` (main `6aab23f`), Xvfb+xdotool, dpr=1.
Evidence: `draw-zoom/montage-titles.png` (4-way title crop across step-12 "•" →
step-13 none → step-16 "•" → step-17 none). Curated evidence to commit under
`docs/uat/images/2026-07-20-dirty-marker-clears-on-zoom.png`.
