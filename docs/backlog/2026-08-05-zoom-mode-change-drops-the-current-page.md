---
id: 2026-08-05-zoom-mode-change-drops-the-current-page
title: Switching zoom mode carries the reader off the page, and the page readout keeps claiming the old one
priority: TBD
status: open
source: measured while fixing the resize page-anchor (UAT-VWR-111) on the gating Linux nightly lane
created: 2026-08-05
---

## Threshold

Changing zoom mode while reading page N of a long PDF leaves the reader on
page N, and the reported page and the visible page agree. Concretely,
checkable pass/fail, offscreen:

- Open a 200-page A4 PDF in a 900x700 window, `View > Continuous`,
  `View > Fit Page`.
- `goToPage(140)`; confirm `currentPage() == 140`.
- `View > Fit Width`; let the layout settle.
- **Both** must hold: `currentPage() == 140`, **and** the vertical scroll
  value equals the position `goToPage(140)` produces from a settled
  Fit-Width layout (i.e. the readout is not merely stale). Today the second
  fails and the first passes only because nothing recomputed it.

## Context

Measured on `origin/main` 6606081, Linux/offscreen, while adding
UAT-VWR-111:

```
after Fit Page,  goToPage(140):  value 83166   page 140   (805 px/page after refit)
after Fit Width, settled:        value 83166   page 140   <- value never re-anchored
observed truth:                  83166 / 805 = page 103
```

`QPdfView` re-lays-out for the new zoom mode but keeps the vertical
scrollbar's absolute pixel value, so the reader is carried ~37 pages
backwards. `QPdfPageNavigator::currentPage()` is *not* recomputed at that
moment, so the page readout keeps reporting 140 — the model and the view
disagree, which is the lying-readout half of the defect and arguably the
worse half. The disagreement only surfaces later, when something (a resize,
a scroll) makes `QPdfViewPrivate::setViewport` recompute the page from the
real offset, at which point the readout snaps to the honest value.

A second consequence worth knowing when writing tests here:
`PdfDocument::goToPage()` is a **no-op** while the navigator already
believes it is on the requested page, so calling `goToPage(140)` after a
zoom-mode change does not repair the position.

## Relationship to the resize fix

Same family, different trigger. The resize case is fixed in
`NavigablePdfView::resizeEvent` (`src/document/PdfAdapter.cpp`), guarded by
`uat_vwr_111_resizingTheWindowKeepsTheCurrentPage`. That fix deliberately
hooks the resize path only: a zoom-mode change is a *user command* that
re-lays-out for its own reasons, and anchoring it belongs with the zoom
code (`zoomFitPage()` / `zoomFitWidth()` / `applyZoomState()` in
`src/document/PdfAdapter.cpp`), not in a resize handler. Filed rather than
folded in, so the resize fix stays a change a reviewer can check in one
sitting.

Likely shape of the fix: capture the page before the mode change and
re-navigate after the layout settles — the pattern
`PdfDocument::setViewMode()` already uses for the same class of bug
("capture the page BEFORE swapping surfaces and explicitly re-navigate the
new one to it, through the same goToPage() every other page-change uses,
so the model and the view can never disagree after a mode switch"). Note
the goToPage no-op above: repairing this needs the navigator's belief
refreshed, not just a jump to the page it already claims.

## Provenance

Found by `uat_vwr_111_nonRescalingResizeLeavesTheViewAlone`'s first draft,
which tried to set up its Fit-Width precondition by navigating and then
switching modes, and could not make the precondition true. The slot was
restructured to choose its zoom mode before navigating; the comment there
points back at this item.
