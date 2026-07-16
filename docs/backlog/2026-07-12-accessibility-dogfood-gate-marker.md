---
id: 2026-07-12-accessibility-dogfood-gate-marker
title: Accessibility checklist first real audit activates at the dogfood-default milestone (G8)
priority: unranked
status: open
source: housekeeping; consolidated docket 2026-07-10
created: 2026-07-12
---

## Threshold

When the owner adds the observable `dogfood-default` marker (a dated
`dogfood-default` entry in `ROADMAP.md` / the changelog, or a git tag named
`dogfood-default`), gate **G8** goes live and the full
[`../accessibility-checklist.md`](../accessibility-checklist.md) is run once
against the running app with every row passing. Until that marker exists, G8 is
dormant (per-PR no-regress only) and this item is a standing reminder, not
actionable work.

## Context / Body

Marker item, not code work: it records that the first real accessibility audit
is gated on the owner-declared `dogfood-default` milestone (G8 in `AGENTS.md`).
It exists so the audit is not forgotten when the marker lands. Close it by
running the checklist once the marker exists.

## Provenance

Housekeeping note in the consolidated follow-up docket 2026-07-10
("accessibility-checklist first real audit activates at the dogfood-default
milestone (G8)").
