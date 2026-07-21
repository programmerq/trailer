# Theme control applies live (light / dark / system), superseding the shown-but-disabled decision

- **Status:** accepted <!-- proposed | accepted | superseded-by <YYYY-MM-DD-slug> -->
- **Arbiter:** the Preferences-pane arbiter role named for this record; the owner (programmerq) is the escalation-only override.
- **Date proposed:** 2026-07-20
- **Date accepted / superseded:** 2026-07-20 (accepted)
- **Supersedes:** DR-4 (`docs/decisions/0004-theme-control-shown-but-disabled.md`)

## Context

The Theme control (System / Light / Dark) in Preferences → General was
**shown but disabled** by DR-4 (2026-07-09), with a muted helper label
reading "Not applied yet — planned for a future release." DR-4 was explicit
that this was a *temporary* honesty measure: `general.theme` round-tripped in
`settings.toml` but **nothing in the codebase read it to apply a palette or
colour scheme**, so a working-looking combo would have been a no-op that
eroded trust. DR-4's arbiter verdict named the disabled state as the only
option no persona objected to *given that live theming was out of scope for
that release*.

This record closes that gap: it makes the Theme control **apply live** and
**enables** it. Landing this is what DR-4 anticipated ("planned for a future
release"), so DR-4 is superseded rather than contradicted.

**What shipped before this branch, so this record is not misread as
describing the present:**

- `Settings::theme()` was never consumed anywhere — no `setColorScheme` /
  palette code existed in `src/`.
- `src/ui/PreferencesDialog.cpp` built `m_themeCombo` and immediately
  `setEnabled(false)` it, with a disabled tooltip and a visible
  `themeHelpLabel` ("Not applied yet …").
- `restoreEditableDefaults()` deliberately skipped the Theme control because
  it was disabled.

**What this branch changes (user-visible):**

- The Theme combo is **enabled**; the "not applied yet" helper label and
  disabled tooltip are removed.
- Choosing Light or Dark re-themes the running app **without a restart**;
  System hands the appearance back to the OS and follows it live. Applied at
  startup from the persisted value and live on Preferences OK, via
  `Application::applyTheme` → `colorSchemeFor` → `QStyleHints::setColorScheme`
  (`src/settings/Settings.cpp:colorSchemeFor`, `src/app/Application.cpp`).
- Restore Defaults now resets Theme to System like every other editable
  control.

## Options

- **A. Wire it live and enable it (what ships).** Apply the persisted theme
  at startup and re-apply on OK; enable the combo; drop the helper label.
  System follows the OS live.
- **B. Keep it disabled (status quo, DR-4).** Leave the combo greyed with the
  "not applied yet" note.
- **C. Enable the combo but apply only on next launch (restart-required).**
  Persist on OK, read once at startup; surface a "requires restart" hint via
  the §15 volatility registry.

## Personas debate

- **Office non-technical user:** Picks "Dark" expecting the window to go dark
  immediately. Under A it does. Under C it appears to do nothing until a
  relaunch they may not connect to the setting — the exact "is it broken?"
  confusion DR-4 worried about, merely moved from disabled to restart-lag.
  Favours A.
- **Older careful user:** DR-4's decisive persona — feared a live-but-inert
  control causing self-blame. A removes the inertness (the control now does
  what it says), so the fear no longer applies; an enabled control that
  visibly works is calmer than a greyed one with an apology. Favours A.
- **Power migrator:** Hand-sets `theme = "dark"` in `settings.toml` and
  expects the app to honour it on launch. Under A the startup apply path
  reads it and the window opens dark. Under B their hand-set value is stored
  but ignored at runtime. Favours A; B is the status quo they were working
  around.
- **Occasional user:** DR-4's frugality objector ("full theming is a lot of
  work for a surface-existing-settings release"). That objection was scoped
  to *that* release; this is the follow-up item that funds the work. No
  standing objection to A now that the work is being done.

## Admissible objections

- **Office user + older careful user, Option C — "pick Dark" step:** an
  enabled control whose effect is deferred to the next launch reproduces the
  "did nothing / is it broken?" failure DR-4 set out to avoid, just relocated.
  Decisive against C for the primary flow.
- **Power migrator, Option B — "launch with a hand-set theme" step:** B
  stores `theme` but never applies it, so a hand-edited `theme = "dark"` is
  silently inert at runtime. Decisive against continuing B.
- **Every persona, live-switch icon legibility — "flip to Dark with a
  document open" step:** `themedActionIcon` bakes fixed-colour pixmaps at
  build time, so a naive palette swap would leave toolbar/menu icons tinted
  for the old scheme (e.g. near-black glyphs on a dark toolbar). Decisive for
  re-tinting icons on the apply path (`ThemedIconBinder::refresh`), not just
  swapping the palette.

### Rejected as naked preference

- "Add a big Light/Dark toggle to the toolbar instead of leaving it in
  Preferences." — rejected: states no concrete user/step/failure; the control
  already has a home in Preferences → General per DESIGN §6.13, and a
  toolbar toggle is a separate scope with its own IA argument.

## Checkable threshold this record establishes

Independently checkable:

- **Enabled + honest.** The Theme combo is `isEnabled()` and there is no
  `themeHelpLabel` in the dialog. (`tests/uat/test_uat_preferences.cpp` →
  `uat_pref_010`; `tests/test_preferences.cpp` → `themeControlEnabled`.)
- **Maps to the right scheme.** `colorSchemeFor(System|Light|Dark)` ==
  `Qt::ColorScheme::{Unknown, Light, Dark}`. (`tests/test_settings.cpp` →
  `colorSchemeMapping`.)
- **Applies without a restart.** Changing the combo and accepting drives the
  `settingsApplied` re-apply path so the new theme (and its mapped scheme)
  takes effect with no reconstruction. (`tests/test_preferences.cpp` →
  `themeAppliesLiveThroughSignal`.)
- **System follows the OS live.** `Theme::System` maps to
  `Qt::ColorScheme::Unknown` (Qt tracks the OS) and
  `QStyleHints::colorSchemeChanged` is connected so icons re-tint on an OS
  flip. (`src/app/Application.cpp`.)

## Arbiter verdict + rationale

**Accepted 2026-07-20 — Option A.** DR-4 disabled the control **solely**
because live theming was unwired and out of scope for a surface-existing-
settings release; it explicitly flagged the live wiring as planned. This
record does that planned work, so every persona position DR-4 recorded now
resolves toward an enabled, working control: the non-technical and
older-careful users get immediate, honest feedback; the power migrator's
hand-set key is finally honoured at runtime; the occasional user's frugality
objection was release-scoped and no longer stands. Option C is rejected
because a restart-deferred effect reproduces the very "is it broken?" failure
DR-4 avoided. The one new admissible objection — stale baked icons on a live
switch — is handled by re-tinting through `ThemedIconBinder::refresh` on the
apply path rather than relying on palette propagation alone.

Implementing seams: `Settings::colorSchemeFor` (enum → `Qt::ColorScheme`),
`Application::applyTheme` (startup + live, plus the `colorSchemeChanged`
connection for System), `PreferencesDialog` (enabled combo, helper label
removed), and `ThemedIconBinder` (`src/ui/IconHelper.{h,cpp}`) for the icon
re-tint. The control lives in Preferences → General on every platform (Qt
`QStyleHints::setColorScheme` is cross-platform), consistent with G4.

## Consequences

- **Positive.** The Theme control does what it says, live and without a
  restart; a hand-set `theme` is honoured on launch; System tracks the OS.
- **Costs / follow-ups.** `QStyleHints::setColorScheme` recolours the Qt
  palette (widgets, menus, standard controls); any surface that hard-codes
  colours instead of reading the palette would not follow the theme — none
  are known in-tree today, and the themed-icon path is explicitly handled.
  A dedicated high-contrast/accessibility theme is out of scope (tracked
  under the G8 accessibility milestone), not a reopening of this record.

## Evidence required to reopen

A measured case where live theming costs a real user a concrete step — e.g.
an owner pass showing a surface that fails to re-theme (hard-coded colours) or
an icon that stays illegible after a switch — plus owner sign-off. Adding a
future high-contrast theme extends this decision; it does not reopen it.
