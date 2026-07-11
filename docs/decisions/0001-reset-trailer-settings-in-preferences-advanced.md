# DR-1: Duplicate "Reset Trailer Settings" into Preferences → Advanced

- Status: Accepted
- Date: 2026-07-09
- Area: Preferences pane (ROADMAP #8)

## Context

"Reset Trailer Settings…" already lives in the **Tools** menu. It wipes every
Trailer-managed file (settings.toml, recent.json, cards.toml, signatures, cached
models) behind its own confirmation dialog — the DESIGN §2.4.2 "privacy wipe"
model. The new Preferences dialog needs an **Advanced** tab, and a global wipe is
the canonical Advanced-tab affordance. The question: surface it there too, or
move it, or leave it only in Tools?

## The debate

- **Office non-technical user.** "When something breaks, someone tells me 'open
  Preferences and reset it' — that's where I'd look first. Today it's buried in
  Tools next to ML-model plumbing I don't understand. If it's not in Preferences
  I'll never find it under stress." — concrete problem: discoverability at the
  exact moment of need.
- **Older, careful user.** "My worry is a second button labelled 'Reset' in a
  window that has other reset controls too. I might click the big one thinking
  it just resets one preference and nuke my saved signatures." — concrete
  problem: label collision if the per-field affordances also read "Reset".
- **Power migrator.** "Moving it out of Tools would break my muscle memory and
  any docs/screenshots that say 'Tools → Reset'. Duplicating is fine; removing
  is a regression." — concrete problem: relocation breaks an existing path.
- **Occasional user.** "I use this maybe twice a year. Two entry points don't
  confuse me as long as both ask 'are you sure' — and this one already does." —
  concrete problem: none blocking, given the existing confirm.

## Arbiter verdict

**Duplicate** the action as a button in Preferences → Advanced; keep the Tools
menu entry. This is purely additive, so no muscle memory breaks (power
migrator). The older-careful label collision is resolved because the per-field
affordance carries **no visible "Reset" text at all**: it is a small, flat,
right-aligned revert icon (a ↺ glyph) with the accessible name and tooltip
"Reset to default", enabled only when that control differs from its default —
so it reads as an unobtrusive per-row revert, never a rival to the one big,
explicitly destructive "Reset all Trailer settings and data…" button (which is
further set apart by its explanatory warning label and its own confirmation
dialog). The word "Reset" therefore appears once in the pane, on the
destructive action. The decision was reached on the merits, without escalation.
