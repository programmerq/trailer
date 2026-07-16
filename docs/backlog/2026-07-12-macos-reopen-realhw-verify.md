---
id: 2026-07-12-macos-reopen-realhw-verify
title: macOS reopen path — real-hardware verification (dock activation → open panel exactly once)
priority: P3
status: open
source: platform-honesty follow-ups; consolidated docket 2026-07-10
created: 2026-07-12
---

> **Update 2026-07-15 — co-resolved in part by `2026-07-12-macos-launch-no-open-panel`.**
> That item landed: the automatic Open panel on zero-window activation has
> been **removed** (`src/app/Application.cpp` no longer calls
> `openFilesFromDialog()` from the `ApplicationStateChange` handler), and
> `setQuitOnLastWindowClosed(false)` now guarantees no dialog dismissal quits
> the app. This item's scope therefore **narrows** to a real-Mac check that
> **activation with no windows never quits and never double-panels** — there is
> no longer a panel that must appear "exactly once"; the check is that
> activation is inert (dock icon + menu bar only) and stable across repeated
> dock clicks / Cmd-Tab. Still a real-Mac verification item (the path is guarded
> and not exercisable off-Mac). No code change here.

## Threshold

On real macOS hardware, activating the app with no windows (dock click)
does **nothing automatic** — it presents **no** panel (the auto-panel was
removed) and, critically, **never quits** and **never double-panels** across
repeated activations. (Superseded sub-goal, retained for provenance: the
original threshold was "presents the open panel exactly once"; the launch-item
fix removed the automatic panel, so "exactly once" is no longer the target.)
Verified by hand on a real Mac (the path is guarded and not exercisable
off-mac).

## Context / Body

The macOS reopen/activation path (`ApplicationStateChange → active` with zero
windows, decision `docs/decisions/empty-state-window-model.md` D3) has only
ever been asserted from the shared code path, never verified on real hardware.
This item is the real-HW verification that dock activation opens the panel
exactly once.

**Cross-reference — this is distinct from, and partly superseded by,
`2026-07-12-macos-launch-no-open-panel`:**
- That new item asks *whether there should be an automatic open panel at all*
  (owner's dogfood ruling: skip the file-open on the Mac build; launch should
  be dock + menu bar only).
- **This** item is the reopen/activation *behaviour* verified on real hardware
  (that whatever the launch shape settles to, dismissing a dialog never quits
  and activation behaves exactly once).

Keep both: if the launch item removes the automatic panel, this item's scope
narrows to verifying activation-with-no-windows behaves correctly (no quit, no
double-panel) rather than verifying a panel appears.

## Provenance

Platform-honesty follow-ups, harvested into the consolidated docket 2026-07-10
(P3 platform honesty).
