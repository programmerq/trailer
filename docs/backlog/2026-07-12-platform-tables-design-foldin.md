---
id: 2026-07-12-platform-tables-design-foldin
title: Platform command-surface / shortcut tables folded into DESIGN (verify rescue landed)
priority: P1
status: open
source: criteria-gates session harvest; consolidated docket 2026-07-10
created: 2026-07-12
---

## Threshold

The per-OS platform command-surface and shortcut tables are folded into
`DESIGN.md` (with the full reference reachable from it), so gate **G4** has a
concrete checkable per-OS mapping to point at.

## Context / Body

**Verification finding (2026-07-12): this appears already landed on `main`.**
`DESIGN.md` §5.4 carries the per-OS command-surface table (menu bar, modifier,
close/quit shortcuts, file dialogs, app data, settings) and cites
[`../platform-conventions.md`](../platform-conventions.md), which holds the
full unified 1:1 shortcut mapping and command-surface reference (added in
commit `d0d7349`; §5.4 fold-in present in the criteria-gates merge `a41fd8b`).

Treatment: retained as a backlog item only as a **final-verification / close**
task — confirm the fold-in is complete to satisfaction and then delete this
item. No further authoring appears required.

## Provenance

Criteria-gates work session; a rescue commit was requested before archive and
this item existed to verify it landed. Docket 2026-07-10 (P1 item d).
