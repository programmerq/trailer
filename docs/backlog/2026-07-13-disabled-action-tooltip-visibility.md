---
id: 2026-07-13-disabled-action-tooltip-visibility
title: Disabled menu-action tooltips never show without setToolTipsVisible(true) — the G3 affordance is silently inert in menus
priority: TBD
status: open
source: recurring nit (history mine, 2026-07-13)
created: 2026-07-13
---

## Threshold

A disabled menu action carrying a G3 explanatory tooltip actually shows that
tooltip on hover.

Declared pass/fail: for every `QMenu` that hosts a disabled action with a
`setToolTip(...)`, `QMenu::setToolTipsVisible(true)` is set, and a test asserts
the tooltip text is retrievable/visible for the disabled action. No disabled
menu action relies on a tooltip that Qt will never render.

Verified: hovering a disabled Tools-menu (or other menu) item with an
explanatory tooltip displays the tooltip; a regression test guards
`toolTipsVisible()` on the hosting menus.

## Context

Recurring nit surfaced by the history mine (raised on 3 PRs). Qt does **not**
render `QAction::setToolTip(...)` on a menu item unless the hosting `QMenu` has
`setToolTipsVisible(true)`. Trailer relies on disabled-control tooltips as its
G3 "no lying controls" affordance (AGENTS.md G3 — a surfaced-but-inert control is
disabled with a tooltip stating why and where to go), so a menu that omits
`setToolTipsVisible(true)` has an affordance that is silently inert.

Recurrence trail:
- PR #19 — Copilot flagged disabled Tools-menu tooltips never show without
  `QMenu::setToolTipsVisible(true)`; declined / no reply
  (`discussion_r3245257517`).
- PR #45 — "restore G3 disabled-state tooltip on Edit > Copy Page as Image" (a
  regression of exactly this affordance).
- PR #48 — "Harden three regression classes: disabled-action tooltips, enum
  switch fallthrough, Settings restart-surprise."

The affordance regressing three times without a standing guard is the signal to
track it: audit the menus that host disabled G3-tooltip actions, ensure
`setToolTipsVisible(true)`, and add a regression test so it cannot silently
regress again.

Cross-links: AGENTS.md G3 ("no lying controls"); PR #45, PR #48, PR #19.

## Provenance

Recurring nit from the PR history mine, 2026-07-13. Not owner-ranked at source;
priority recorded as `TBD` per the "don't invent a priority" rule — re-triage
when picked up.
