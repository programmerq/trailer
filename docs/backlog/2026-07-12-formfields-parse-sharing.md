---
id: 2026-07-12-formfields-parse-sharing
title: Share the AcroForm field-count parse between sidebar defaults and form-fill auto-enable
priority: unranked
status: open
source: Copilot review flag on PR #36, 2026-07-12; consolidated docket
created: 2026-07-12
---

## Threshold

Exactly **one** `formFields()` parse per document open, shared between the
content-aware sidebar defaults and the form-fill auto-enable. Verified by a
counter test.

## Context / Body

`ContentAwareDefaults` eagerly calls `doc->formFields()` even when the ≥20-page
rule short-circuits, so the AcroForm field-count parse can run more than once
per open. Accepted as-is by coordinator default (one-time open cost); this
follow-up computes the field count once and shares it between the two
consumers.

Priority: the source declared no P-level for this bullet — recorded as
`unranked` per the "don't invent a priority" rule.

## Provenance

Copilot flag on PR #36 (2026-07-12), carried as a loose bullet in the
consolidated follow-up docket 2026-07-10.
