---
id: 2026-07-21-two-page-ctrl-wheel-zoom
title: Two-up mode — Ctrl+mouse-wheel zoom on the spread (PR2)
priority: P3
status: open
source: committed follow-up of PR #113 (two-page view increment, PR1)
created: 2026-07-21
---

## Threshold

Ctrl+mouse-wheel changes zoom in Two-Pages mode and the status-bar readout
tracks it: with the pointer over the spread, Ctrl+wheel-up zooms the spread in
and Ctrl+wheel-down zooms it out, the painted spread scales accordingly, and the
zoom-% readout updates to match the new render scale (the same truthful-readout
invariant guarded by `uat_vwr_079`). Pass/fail is observable: Ctrl+wheel over
the spread moves the zoom and the readout follows.

## Context

PR1 wires zoom in Two-Pages mode through the toolbar/menu zoom actions
(Actual Size, Zoom In/Out, spread-aware Fit-Width/Fit-Page) sharing the readout
path, but the custom `TwoPageView` does not yet handle Ctrl+wheel as a zoom
gesture — the wheel scrolls the spread canvas. Single/Continuous modes get
Ctrl+wheel zoom from `QPdfView`; two-up should match for parity.

The work is to intercept `wheelEvent` in `TwoPageView` when Ctrl is held, route
it through the same shared-zoom path the zoom actions use (so the readout stays
truthful), and let a plain wheel keep scrolling the spread. Anchor:
[`src/ui/TwoPageView.cpp`](../../src/ui/TwoPageView.cpp).

Do this after the PR1 view increment (PR #113) lands.
