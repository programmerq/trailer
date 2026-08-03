---
id: 2026-08-03-twopageview-recomputes-canvas-per-paint
title: TwoPageView recomputes the whole-document canvas size (O(pages) pdfium lookups) on every relayout and paint
priority: TBD
status: open
source: surfaced by the correctness-skeptic reviewer pass on the page-geometry cache PR, 2026-08-03
created: 2026-08-03
---

## Threshold

Painting or relaying out `TwoPageView` on an already-open document performs
**zero** `QPdfDocument::pagePointSize()` engine calls — the per-page geometry it
needs is resolved once per loaded page graph, as `PdfDocument::PageMetrics`
already does for the continuous/single-page path.

Checkable pass/fail, in the established structural style (counts, never
wall-clock — see `tests/test_perf_page_geometry.cpp`):

- Open a multi-page PDF, switch to Two Pages view, let it settle.
- Reset the engine-call counter, then force N relayouts/paints.
- `PdfDocument::pagePointSizeEngineCalls()` has not increased.

## Context

`TwoPageView::canvasSize()` (`src/ui/TwoPageView.cpp`) walks **every spread**
and calls `spreadWidth()` + `spreadHeight()` on each, and each of those issues
up to 2 `m_doc->pagePointSize()` calls — so `canvasSize()` costs roughly
`4 x pageCount` pdfium page-dictionary lookups. It is called from `relayout()`
and again from the paint path, so a 2603-page document pays ~10,000 engine
lookups per relayout/paint. `fitWidthZoom()` and `fitPageZoom()` walk the
spreads the same way.

This is the **same defect class** as the one fixed in the page-geometry cache PR
(a whole-document aggregate re-derived from pdfium instead of cached), but a
much smaller instance, and it was deliberately left out of that PR:

- It is **O(pages) per paint**, not O(points x pages) per paint — roughly two
  orders of magnitude smaller than the continuous-mode defect that PR fixed
  (739,417 engine calls to open one 2603-page document).
- It only runs in **Two Pages** view mode. The slow-open report that triggered
  the investigation was on **Continuous**, so this path was not implicated in
  it and fixing it would not have moved the reported number.
- The fix is not a one-liner reuse: `TwoPageView` holds a **raw, non-owning**
  `QPdfDocument *` and has no access to `PdfDocument`'s private `PageMetrics`.
  Closing this means either lifting the metrics behind a small shared
  accessor or giving `TwoPageView` its own cache with its own invalidation —
  a design choice worth making deliberately rather than as a drive-by.

The instrumentation the guard above needs already exists:
`PdfDocument::pagePointSizeEngineCalls()` / `resetInstrumentation()` in
`src/document/PdfAdapter.h`.

## Provenance

Found by the correctness-skeptic reviewer pass while auditing whether any
`pagePointSize()` call site remained outside the new cache
(`grep -rn "pagePointSize" src/ | grep -v PdfAdapter`). Filed rather than fixed
so the page-geometry PR stays scoped to the one measured cause, per the
one-PR-per-cause preference.
