---
id: 2026-07-12-theme-live-wire
title: Wire theme live (light/dark/system) and enable the Theme control
priority: P3
status: open
source: polish backlog; consolidated docket 2026-07-10
created: 2026-07-12
---

## Threshold

The Theme control (light / dark / system) applies live and is enabled.
Changing it re-themes the running app without restart, and the control is no
longer shown-but-disabled. Landing this flips
`docs/decisions/0004-theme-control-shown-but-disabled.md` (the control is
currently shown disabled by decision) — reference it, and the live-key
classification in `docs/CONVENTIONS.md` §15.

## Context / Body

Polish item: today the Theme control is deliberately shown-but-disabled
(decision 0004) because the live theming is not wired. This item wires
light/dark/system to apply live and enables the control. Because this changes a
user-visible default/behaviour, it needs the decision-record update per gate
G6.

## Provenance

Empty-state / preferences polish backlog, harvested into the consolidated
docket 2026-07-10 (P3 polish). One item per file per owner's one-item-per-file
rule.
