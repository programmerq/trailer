---
id: 2026-07-21-two-page-overlay-search-parity
title: Two-up mode — restore annotation-overlay, text-selection, and search-highlight parity (PR2)
priority: P4
status: open
source: committed follow-up of decision record 2026-07-21-two-page-layout (D2 honest degradation)
created: 2026-07-21
---

## Threshold

In Two-Pages (facing / continuous-spread) mode, annotation/markup,
text-selection, and search-highlight work with **full parity** to Single and
Continuous modes, and the disabled-with-tooltip degradation shipped in the first
view increment (PR1) is **removed**. Concretely checkable, all in two-up mode:

1. **Markup/annotation.** Every markup tool that is enabled in Single mode is
   enabled in two-up mode; creating an annotation on either page of a spread
   places it on the **correct page** at the correct page-space coordinates
   (verified by round-tripping the annotation and re-opening in Single mode).
   The "switch to Single or Continuous to mark up" tooltip no longer appears.
2. **Text selection.** Dragging a selection over text on either page of a spread
   selects that page's text (no cross-page bleed at the spread gutter); Copy
   yields the selected text.
3. **Search-highlight.** A search match on any page paints its highlight on the
   correct page within the spread, and "next/previous match" scrolls the spread
   containing the match into view.

Pass/fail is per-clause and observable; no "feels right" acceptance.

## Context

Decision record
[`docs/decision-records/2026-07-21-two-page-layout.md`](../decision-records/2026-07-21-two-page-layout.md)
(D2) ratified **honest degradation** for the first shipping two-up increment:
markup, text-selection, and search-highlight **may** ship
disabled-with-tooltip in Two-Pages mode, with full parity committed as **this**
tracked follow-up (PR2) — not optional. This item is that commitment.

The work is to reproject the annotation overlay, the selectable-text layer, and
the search-highlight layer onto the custom `TwoPageView` spread geometry (each
page in a spread has its own page-space → view-space transform, per the pairing
rule in
[`src/document/SpreadLayout.h`](../../src/document/SpreadLayout.h) /
`spreadsFor`). Anchors: `src/ui/AnnotationOverlay.cpp` (overlay render +
hit-test), the selectable-text layer, and the search-highlight path — all
currently wired to the `QPdfView`-driven Single/Continuous surfaces.

Do this only **after** the PR1 view increment lands (the increment that meets
the layout/zoom/DPR/enablement clauses of the two-page-layout record and closes
[`docs/backlog/2026-07-12-real-two-page-layout.md`](2026-07-12-real-two-page-layout.md)).

## Provenance

Created alongside PR0 (the two-page-layout threshold record + the pure
`spreadsFor` pairing function) as the D2-committed parity follow-up, so the
honest-degradation increment ships with its parity work tracked to done rather
than left implicit.
