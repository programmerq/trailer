---
id: 2026-07-21-two-page-search
title: Two-up mode — find/search highlighting on visible spreads (PR2)
priority: P3
status: open
source: committed follow-up of PR #113 (two-page view increment, PR1)
created: 2026-07-21
---

## Threshold

A search match on either page of a visible spread is highlighted in Two-Pages
mode: run Find, and a match on the left **or** the right page of a facing spread
paints its highlight on the correct page at the correct page-space coordinates,
and next/previous-match scrolls the spread containing the match into view.
Pass/fail is observable per match: the highlight lands on the page that holds
the matched text, with no cross-page bleed at the gutter.

## Context

PR1 ships Find **disabled-with-tooltip** in Two-Pages mode (honest degradation,
per decision record
[`docs/decision-records/2026-07-21-two-page-layout.md`](../decision-records/2026-07-21-two-page-layout.md)
D2) because the search-highlight layer is wired to the `QPdfView`-driven
Single/Continuous surfaces and does not yet project onto the custom
`TwoPageView` spread geometry. This item restores search in two-up.

This is a sibling of
[`2026-07-21-two-page-overlay-search-parity`](2026-07-21-two-page-overlay-search-parity.md),
which tracks the broader overlay/text-selection/search parity work; this file
narrows the checkable threshold to the **search-highlight** clause so it can be
picked up and closed on its own. The work is to reproject the search-highlight
path onto each page's page-space → view-space transform in `TwoPageView`
(pairing per `spreadsFor` in
[`src/document/SpreadLayout.h`](../../src/document/SpreadLayout.h)) and re-enable
the Find action in Two-Pages mode.

Do this after the PR1 view increment (PR #113) lands.
