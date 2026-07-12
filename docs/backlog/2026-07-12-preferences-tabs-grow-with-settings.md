---
id: 2026-07-12-preferences-tabs-grow-with-settings
title: Grow Preferences tabs only as real settings appear (Forms tab gated on ≥1 wired Forms setting)
priority: P3
status: open
source: polish backlog; consolidated docket 2026-07-10
created: 2026-07-12
---

## Threshold

A Preferences tab appears only once it has ≥1 real, wired setting. Concretely:
the **Forms** tab is not shown until at least one Forms setting is wired and
persists (no empty/stub tabs). Verified by inspecting the Preferences window —
every visible tab has at least one operable control.

## Context / Body

Polish item: avoid shipping empty Preferences tabs. The Forms tab in
particular is gated on there being at least one wired Forms setting before it
appears. Ties to the Settings volatility registry (`docs/CONVENTIONS.md` §15)
and gate G7 (each enumerated pane present and operable at 1.0).

## Provenance

Preferences polish backlog, harvested into the consolidated docket 2026-07-10
(P3 polish). One item per file per owner's one-item-per-file rule.
