---
id: 2026-07-25-pdf-zoom-readout-resize-staleness
title: PDF zoom-% readout may go stale after a live window resize in Fit mode (same shape as the image bug fixed in 2026-07-24-test-image-scale-macos-failure)
priority: TBD
status: open
source: HIG-polish reviewer pass on claude/fix-test-image-scale-macos (2026-07-25), independently corroborated by the author's own investigation
created: 2026-07-25
---

## Threshold

TBD — declare before work begins. Candidate shape, to be confirmed by a
repro first: with a PDF open in Fit-to-Width or Fit-Page mode, resizing the
window (or, offscreen, forcing a `QPdfView` viewport resize) changes the
rendered zoom factor; the status-bar zoom-percentage readout
(`MainWindow::updateZoomIndicator()`) must update to match within one event-
loop tick, the same invariant `2026-07-24-test-image-scale-macos-failure`
established for `ImageDocument`. Needs a confirmed repro (this item is
currently a suspected gap, not a demonstrated failure) before a threshold
can be finalized.

## Context

While fixing the macOS-only `test_image_scale` failure (readout/render
divergence for images — see the PR that closed
`docs/backlog/2026-07-24-test-image-scale-macos-failure.md`), both the
author and an independent HIG-polish reviewer pass noticed the same class
of bug likely exists on the PDF side too, untested either way:

- `PdfDocument`'s Fit-to-Width / Fit-Page delegates to native
  `QPdfView::ZoomMode`, which recomputes `zoomFactor()` internally on
  viewport resize (`src/document/PdfAdapter.cpp`).
- `QPdfView::zoomFactorChanged` already drives the overlay / text-layer /
  form-overlay / `TwoPageView` relayout live
  (`src/document/PdfAdapter.cpp:853,909,943,961`), but
  `src/ui/MainWindow.cpp` never connects that signal (or any resize signal)
  to `updateZoomIndicator()` — the readout is only refreshed from explicit
  zoom-action triggers, tab-switch, and the one `capabilityNotifier()`
  fire reserved for the async AcroForm-detection landing
  (`PdfAdapter.cpp:454`).
- `tests/uat/test_uat_zoom_indicator.cpp` (and the unit
  `tests/test_image_scale.cpp` suite) only exercise the image path.

The fix for images was two `m_capabilityNotifier.notifyChanged()` calls in
`ImageDocument::reapplyFitMode()` (`src/document/ImageAdapter.cpp`). The
PDF equivalent, if the repro confirms the bug, is likely wiring
`QPdfView::zoomFactorChanged` (already available) to
`MainWindow::updateZoomIndicator()` for the current document — but this
was explicitly kept out of scope for that PR (which was scoped to the
reported/confirmed image-only CI failure) and needs its own repro +
threshold before implementation, per this project's G1 gate.
