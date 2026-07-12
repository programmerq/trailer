---
id: 2026-07-12-bg-removal-progress-cancel-widget
title: Route the background-removal ML op through the progress/cancel widget
priority: unranked
status: open
source: disclosed in PR #49, 2026-07-12; consolidated docket
created: 2026-07-12
---

## Threshold

Background removal shows the same **B5-compliant** progress and per-op-
appropriate cancel semantics as OCR, verified by a state test **and** a
screenshot.

## Context / Body

The progress/cancel widget mechanism landed in PR #49 is general, but wired for
OCR only today. This follow-up routes the background-removal ML op through the
same widget so it gets B5-compliant progress and appropriate cancel semantics.
See `docs/decision-records/0002-ml-background-removal-progress-cancel.md` and
the B5 budget in `docs/performance-budgets.md`.

Priority: the source declared no P-level for this bullet — recorded as
`unranked` per the "don't invent a priority" rule.

## Provenance

Disclosed in PR #49 (2026-07-12), carried as a loose bullet in the
consolidated follow-up docket 2026-07-10.
