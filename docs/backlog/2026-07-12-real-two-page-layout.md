---
id: 2026-07-12-real-two-page-layout
title: Real two-page layout — custom layout/paint layer, not a QPdfView toggle
priority: P4
status: open
source: roadmap item; consolidated docket 2026-07-10
created: 2026-07-12
---

## Threshold

Real two-page (two-up) layout renders facing pages side by side. TBD — declare
the concrete acceptance line (which page pairings, cover-page handling, scroll
behaviour) before work begins.

## Context / Body

Big roadmap item. Two-page layout **cannot** come from `QPdfView` — it has no
two-up `PageMode`. It requires a custom layout/paint layer. Do **not** assume
this is a toggle; it is a substantial rendering-layer effort.

## Provenance

Roadmap follow-up, harvested into the consolidated docket 2026-07-10 (P4 big
roadmap item).
