# DR-4: Theme control shown-but-disabled with an explanatory tooltip

- Status: Superseded by DR 2026-07-20-theme-applies-live
  (`docs/decision-records/2026-07-20-theme-applies-live.md`)
- Date: 2026-07-09
- Superseded: 2026-07-20
- Area: Preferences pane (ROADMAP #8)

> **Superseded (2026-07-20).** The live theming this record deferred
> ("planned for a future release") has landed: the Theme control now applies
> light/dark/system live without a restart and is enabled. See
> DR 2026-07-20-theme-applies-live for the current decision. The context
> below is retained as the historical rationale for the interim
> shown-but-disabled state.

## Context

`theme` (System / Light / Dark) is persisted in settings.toml and round-trips
correctly, but **nothing in the codebase reads it to apply a palette or colour
scheme** — it is inert today. The General tab has an obvious slot for a Theme
control. Three options: (a) omit it, (b) show a working-looking combo that does
nothing, (c) show it disabled with an honest explanation.

## The debate

- **Office non-technical user.** "If I pick 'Dark' and nothing happens, I'll
  assume the app is broken and lose trust in *every* other setting in this
  window." — concrete problem: a no-op working control erodes trust globally,
  not just for theme.
- **Older, careful user.** "I'd rather see it greyed out with a note saying
  'coming later' than click it and wonder whether I did something wrong or my
  screen is faulty." — concrete problem: a live-but-inert control produces
  self-blame and uncertainty.
- **Power migrator.** "I hand-edit settings.toml. If the pane hides `theme`
  entirely, a future me who set `theme = \"dark\"` in the file sees a Preferences
  window that silently omits a key that exists on disk — that's a worse lie than
  a disabled control." — concrete problem: omission hides a real, hand-settable
  key and desyncs the UI from the file.
- **Occasional user.** "Wiring real live theming now is a lot of work for a
  release that's about surfacing existing settings. I don't need it today; I just
  don't want to be misled." — concrete problem: full theming is out of the
  frugality scope of this change.

## Arbiter verdict

**Show it disabled**, populated to reflect the stored value, with a **visible
muted helper label** beneath the combo reading "Not applied yet — planned for a
future release." (a matching tooltip is kept, but because Qt suppresses tooltip
events on disabled widgets the always-visible label is what actually carries the
explanation — a bare disabled combo with only a tooltip would read as broken).
Omitting hides a hand-editable TOML key (power migrator); a no-op working combo
erodes trust (non-technical) and induces self-blame (older-careful); wiring live
theming is out of scope for a surface-existing-settings release (occasional).
The disabled-plus-visible-note option is the only one no persona objects to —
consensus on the merits, nothing to escalate.
