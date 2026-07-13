---
id: 2026-07-12-macos-launch-no-open-panel
title: macOS launch should be dock + menu bar only — no automatic open panel, and no dialog dismissal may quit the app
priority: high
status: open
source: owner dogfood report 2026-07-12
created: 2026-07-12
---

## Threshold

On the macOS build, launching `Trailer.app` presents **no automatic
file-open panel** — the running-with-no-windows shape is first-class: dock
icon + menu bar only. Dismissing any dialog must **never** quit the app.
Verified on macOS: launch shows no open panel, and cancelling/dismissing any
dialog leaves the app running.

## Context / Body

Owner dogfood report (2026-07-12). Opening the app on macOS today "pops open a
finder picker dialog. This isn't what a MacOS app should do... dismissing that
unwanted file open dialog, made the entire app quit." Owner's ruling: "I'd be
willing to completely skip the file open on the Mac build."

Requirement:
- macOS's "app running with no windows" shape is first-class; launch is dock +
  menu bar only, with **no** automatic open panel.
- Dismissing any dialog must **never** quit the app.

This **refines** the empty-state model: `DESIGN.md` §2.4.2 macOS paragraph and
`docs/decisions/empty-state-window-model.md` (decision **D3**) currently say
activation with no windows opens the file-open panel. The docs amendment rides
with the future fix — update §2.4.2 and the empty-state decision record when
this lands, do not amend them ahead of the code.

**Explicitly NOT for the in-flight 0.3.0 release.**

Cross-link: `2026-07-12-macos-reopen-realhw-verify` — that item verifies the
reopen/activation behaviour on real hardware (no quit, activation behaves
exactly once); **this** item decides whether an automatic open panel should
exist at launch at all. Distinct concerns; keep both.

## Provenance

Owner dogfood report, 2026-07-12 (direct owner message, quoted above).
