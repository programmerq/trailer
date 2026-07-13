---
id: 2026-07-12-gate-reference-rig-and-corpus
title: Reference rig spec + checked-in perf corpus to make budgets B1–B4 binding
priority: P1
status: open
source: criteria-gates session harvest; consolidated docket 2026-07-10
created: 2026-07-12
---

## Threshold

A reference rig is specified and a `docs/perf/corpus/` of **≥3 files** is
checked in, and a written G2 ruling on offscreen `grab()` exists — together
these make performance budgets **B1–B4** binding (a reviewer can run a named
budget row against a named rig and named corpus and declare pass/fail). See
[`../performance-budgets.md`](../performance-budgets.md).

## Context / Body

The perf budgets B1–B4 are currently advisory because there is no named
measurement rig and no fixed corpus to measure against. This item stands up:

1. A reference rig spec (the hardware/OS baseline a budget number is quoted
   against).
2. A checked-in `docs/perf/corpus/` with at least three representative files.
3. A written G2 ruling on offscreen `grab()` as the capture method.

Note (2026-07-12): the G2 offscreen-`grab()` ruling has since landed in
`AGENTS.md` gate **G2** ("Capture method (ruled)") and commit `fd01de0`; the
rig spec + checked-in corpus remain the open part of this item.

## Provenance

Criteria-gates work session, harvested into the consolidated follow-up docket
2026-07-10 (P1 item a). Related: perf budget ratification B1–B8 in the owner
decision package.
