---
id: 2026-07-20-image-open-zoom-readout-fix-unmerged
title: Image opens upscaled past 100% and the zoom-% readout lies on open — fix exists unmerged on fix/image-open-zoom-readout
priority: TBD
status: open
source: UX-walkthrough driven-mode audit 2026-07-20 (Persona A workers A1-F1, A2-F1, A3-F1/F2 — triple-confirmed)
created: 2026-07-20
---

## Threshold

On open of an image whose pixel size is at or below the viewport (dpr=1), after
the window has been mapped/resized/maximized once:

1. The rendered image is at true 1:1 (pixel width ± rounding), NOT upscaled to
   fill the viewport.
2. The status-bar zoom readout matches the actual on-screen magnification (reads
   `100%` only when the image is actually drawn 1:1). Readout and render agree.

Concretely checkable: open a 600×420 (and an 800×600) PNG in a larger window,
maximize, and confirm the grid renders at pixel size while the readout reads
~100% — never "5%"/"8%"/"9%" while drawn at ~100%, and never "100%" while drawn
~135%/195%.

**A fix already exists** on branch `fix/image-open-zoom-readout` (tip
**`05657d9`**, verified NOT an ancestor of main `6aab23f` — the merge train
#76/#80/#83/#85/#86/#89/#91 shipped without it). The fix parks an ordinary open
in `Actual` when fit ≥ 1 and adds a deferred `QTimer::singleShot(0)` readout
refresh after the async fit settles. **This item is Done when that fix is
merged/rebased onto main and both threshold facets pass** — no fresh
implementation is expected unless the rebase reveals the fix is incomplete.

## Context

Two facets of the same image-open path, triple-confirmed by three independent
Persona A workers on different fixtures:

- **Readout lie (CF-1):** `MainWindow::updateZoomIndicator()`
  ([`src/ui/MainWindow.cpp:2770`](../../src/ui/MainWindow.cpp)) prints
  `qRound(doc->zoomFactor()*100)`, but the indicator is set only synchronously in
  `onCurrentDocumentChanged` — before the async initial-fit tick settles — and
  `IDocument` is not a `QObject`, so there is no `zoomFactorChanged` to hook. The
  readout freezes on a transient value ("5%"/"8%"/"9%"/"7%") while the image is
  drawn at ~100%; Ctrl+0 flips the label to a correct "100%" without moving a
  pixel, proving the label, not the render, is the lie.
- **Upscale on resize (CF-2):** `applyInitialFitZoom` caps at 100% but parks mode
  `FitInView` ([`src/document/ImageAdapter.cpp:633`](../../src/document/ImageAdapter.cpp));
  the first post-map Resize fires `reapplyFitMode`, which is uncapped
  (`applyScale(std::min(scaleW,scaleH))`, `ImageAdapter.cpp:535`) and upscales to
  fill, contradicting the code's own "Cap at 100%" comment and the oracle
  ("image ≤ viewport opens 1:1").

The status-bar readout was added by #80 specifically to satisfy the
2026-07-16 decision record's finding #5 (H1 visibility of system status); it now
lies at the most common moment. Relates to the branch-only backlog item
`2026-07-19-image-open-zoom-readout-mismatch` (present only on the unmerged PR
#87 harness branch, not in the main working tree), and to record findings #2
(open at wrong size) and #5 (zoom indicator). A user-visible default is changing,
so the merging PR needs a G6 in-code rationale citation for the parked-mode
constant.

## Provenance

First full driven-mode `ux-walkthrough` run, 2026-07-20, real `build/trailer`
(main `6aab23f`) under Xvfb+xdotool, dpr=1. Evidence:
`shipped/step-05-open600-reads100pct-renders195.png`,
`draw-zoom/step-04-clean-open.png`, `draw-zoom/crop-statusbar-02.png`,
`menu-ext/05-image-clipboard-opens-doc.png`. Curated evidence to commit under
`docs/uat/images/2026-07-20-zoom-open-readout-mismatch.png`.
