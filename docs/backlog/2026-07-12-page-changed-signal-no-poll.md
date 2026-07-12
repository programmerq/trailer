---
id: 2026-07-12-page-changed-signal-no-poll
title: Replace the ML page-scroll hint's 150ms poll with a real page-changed signal on IDocument
priority: unranked
status: open
source: disclosed in PR #49, 2026-07-12; consolidated docket
created: 2026-07-12
---

## Threshold

Zero polling timers for hint re-derivation; the ML page-scroll hint updates
from a signal-driven `page-changed` on `IDocument`, verified by a test.

## Context / Body

The ML page-scroll hint currently re-derives itself on a 150ms poll. This
follow-up replaces the poll with a real `page-changed` signal on `IDocument`
so the hint updates on the signal instead of on a timer.

Priority: the source declared no P-level for this bullet — recorded as
`unranked` per the "don't invent a priority" rule.

## Provenance

Disclosed in PR #49 (2026-07-12), carried as a loose bullet in the
consolidated follow-up docket 2026-07-10.
