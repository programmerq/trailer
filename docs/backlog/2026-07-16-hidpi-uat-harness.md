---
id: 2026-07-16-hidpi-uat-harness
title: UAT harness is HiDPI-blind — run key visual UATs under injected devicePixelRatio (dpr ∈ {1, 1.5, 2})
priority: P1
status: open
source: PR #70 / owner Retina dogfood 2026-07-16
created: 2026-07-16
---

## Threshold

The thumbnail width-fill UAT (`test_uat_thumbnail_sidebar`) and the
toolbar-geometry UAT execute under **dpr ∈ {1, 1.5, 2}** in CI and fail on any
dpr-dependent divergence (e.g. painted logical width ≠ column width, badge /
anchor drift). New or extended UATs carry no `perf` label so CI runs them.
Note the offscreen platform reports dpr = 1 by default, so the dpr injection
must be explicit — a passing run at the ambient dpr is not evidence.

## Context

The offscreen UAT harness (`tests/uat`, `QT_QPA_PLATFORM=offscreen`) runs only
at devicePixelRatio = 1, so display-scale bugs are structurally invisible to
it. Concretely: PR #55 shipped the thumbnail scale-to-width feature "verified"
— its UAT `test_uat_thumbnail_sidebar` passed — yet it broke on the owner's
Retina Mac. `ThumbnailDelegate::paint()` scaled a dpr-stamped pixmap with
`QPixmap::scaledToWidth(availW)`, painting at `availW/dpr` (half width) on
dpr = 2. PR #70 fixes it, but pins it only with a **unit** test
(`tests/test_thumbnail_paint.cpp`) that injects synthetic dpr pixmaps; the UAT
harness that actually renders the widget still runs dpr = 1 only, so the same
class of bug can recur in other visual surfaces (e.g. toolbar geometry)
undetected.

## Fix direction

Parameterize the UAT harness over devicePixelRatio so key visual UATs run under
dpr ∈ {1, 1.5, 2}. Candidate mechanisms: per-test `QT_SCALE_FACTOR` /
`QT_ENABLE_HIGHDPI_SCALING`, a test-time primary-screen dpr override, or
injecting dpr into the render path.

Design around this caveat: `QT_SCALE_FACTOR` scales the **whole** UI, so
existing UATs' absolute pixel oracles (e.g. `availW = viewport - 12`, the
width-scan) will false-fail unless made dpr-relative. Express geometry
assertions in **logical** units and scale the pixel-scan tolerances by dpr.

## Cross-links

- `docs/decision-records/0006-thumbnail-scale-to-width.md` — the ADR for the
  scale-to-width behaviour this guards.
- PR #55 — the original scale-to-width feature (UAT-passed, Retina-broken).
- PR #70 — the Retina fix plus the unit test that currently pins it.
- `src/ui/ThumbnailPaint.h`, `tests/test_thumbnail_paint.cpp` — the paint
  helper under test and the synthetic-dpr unit that stands in for the missing
  HiDPI UAT coverage.
