# Markup toolbar tool actions: disable-with-tooltip, not hide

- **Status:** accepted
- **Arbiter:** the agent role named for this decision; the owner (programmerq) is the escalation-only override.
- **Date proposed:** 2026-08-01
- **Date accepted / superseded:** 2026-08-01

## Context

`MarkupToolbar::setToolVisible()` (as it shipped from the `TODO.md`
"Contextual tool availability" item, 2026-05-ish) hid the text-aware trio
(Underline / Highlight / StrikeOut) via `QAction::setVisible(false)` on
documents without a text layer, and hid Instant Alpha / Smart Lasso the
same way on non-image documents or when the SAM model-download policy
blocked them. The in-code rationale at the SAM call site read: *"PHILOSOPHY:
a tool the user cannot act on is hidden, not greyed — the markup toolbar
shouldn't carry buttons that just pop up 'actually no' tooltips."* When a
group's every tool was hidden, the preceding separator was hidden too, so
the toolbar never showed two adjacent dividers around an empty region.

`docs/audit-2026-07-31-g10-deference.md` (SC-CRIT-2) found the cost of that
design: `QToolBar` lays out its actions in one row, so hiding an action
collapses its slot and shifts every action after it. Redact, the SAM
separator, Instant Alpha/Smart Lasso, and the trailing Stroke / Fill /
Width / Dash controls all move — for example, switching from an OCR'd PDF
tab to a plain-image tab (or vice versa) visibly shifts the colour swatches
and width spinner the user's eye and mouse were tracking. This is gate
**G10** (`AGENTS.md`), which did not exist when the original hide-based
design was written and postdates it.

This record decides, for these five tool actions specifically, whether to
keep hiding (accepting the G10 violation) or switch to disabling with a
tooltip (reversing the "hidden, not greyed" call and re-opening the
question G3 already answers for every other gated control in the app).

## Options

- **A. Disable with tooltip (G3-consistent).** Keep every tool action
  always present in the toolbar; `setEnabled(false)` plus a tooltip stating
  why (and where to go, when there is a next step) when the current
  document can't use it. The action never disappears, so nothing after it
  ever reflows.
- **B. Reserve each action's slot while still hiding it.** Keep
  `setVisible(false)` but wrap each hideable action's button in a
  fixed-width container that stays in the layout, blanked rather than
  removed, mirroring the status-bar fix this same audit pass makes for
  SC-CRIT-1. `QToolBar`/`QAction` has no native "reserve the button's space
  but paint nothing" mode — achieving this would mean abandoning
  `QAction`-driven buttons for these five tools and hand-rolling custom
  `QWidget` slots with manual icon/hit-testing logic, which the rest of the
  toolbar does not do.
- **C. Do nothing — accept the reflow.** Leave `setToolVisible()` as
  shipped. Fails G10 outright; not seriously considered, listed for
  completeness per the record template.

## Personas debate

- **Office non-technical user:** Reaches for Redact/Stroke/Fill by muscle
  memory after marking up a few pages; a control sliding sideways under
  the cursor when switching tabs reads as the toolbar rearranging itself
  for no reason they can see. Favours **A** — nothing moves, ever. A
  dimmed Highlight button on a scanned image they haven't OCR'd yet is a
  quieter, more explicable surprise than furniture moving under their
  hand.
- **Older careful user:** Wants a tool in the same place every time,
  document open or not. Strongly favours **A** for the same "it moved on
  me" reason ADR 0007 already settled for toolbar rows. A dimmed icon with
  a tooltip is a familiar, well-understood Windows/Mac convention this
  persona already knows from every other app.
- **Power migrator (ex-Acrobat/Preview):** Both reference apps show
  format-inapplicable tools as dimmed/disabled rather than removing them
  from the strip — a reflowing toolbar row reads as non-native. Favours
  **A**.
- **Occasional user:** Rarely notices a disabled icon either way, but
  would notice a control jumping sideways right as they're about to click
  something else. Mildly favours **A**; indifferent to B's added
  implementation complexity since it produces the same visible outcome as
  A but costs far more code to get there.

## Admissible objections

- **Office/older-careful user, Option C:** switching from an OCR'd PDF tab
  to a plain-image tab (or back) shifts Redact/Stroke/Fill/Width/Dash out
  from under a returning user's cursor — the concrete G10 failure this
  record exists to fix. Decisive against C.
- **Any user, Option B's real cost:** `QToolBar` does not offer a
  "reserved but blank" action slot — the only way to get one is to replace
  five `QAction`s with hand-rolled `QWidget`s carrying their own icon
  painting, hit-testing, and checked/exclusive-group state (duplicating
  what `QActionGroup` already gives Options A/C for free). That is real,
  ongoing maintenance surface for a benefit — "the icon looks entirely
  absent rather than dimmed" — that no persona above named as something
  they need; every persona's admissible complaint is about *position*, not
  about whether an inapplicable tool's icon is dimmed or invisible.
  Admissible against B's cost/benefit, not against B's outcome (which
  would also pass G10).

### Rejected as naked preference

- "A dimmed row of icons looks cluttered." — rejected: no persona above
  raised this as a concrete step-level failure; the original design's own
  rationale ("just pop up 'actually no' tooltips") describes G3's rule
  correctly but predates G10 and did not weigh the reflow cost against it.
  The admissible version of this concern — Option B — is addressed above
  on cost/benefit grounds, not dismissed as preference.

## Checkable threshold this record would establish

`MarkupToolbar`'s tool actions (Underline, Highlight, StrikeOut, Instant
Alpha, Smart Lasso) never change `isVisible()`; unavailability is
expressed by `isEnabled() == false` plus a non-empty `toolTip()`. Every
other action in the toolbar (Redact, Stroke, Fill, Width, Dash, and the
shape tools) has a `pos()` that is bit-identical across any combination of
the five gated actions' enabled/disabled toggles. Proven by
`tests/test_markup_toolbar.cpp`'s
`toolPositionsNeverMoveAcrossEnableDisableToggles` (unit, isolated
`MarkupToolbar`) and
`tests/uat/test_uat_search_and_markup.cpp`'s
`uat_xct_079_markupToolbarActionsStayPutAcrossDocumentTypeSwitch`
(integrated, via `MainWindow`, switching between a text-layer PDF and a
plain image tab — the audit's named repro).

## Arbiter verdict + rationale

**Accepted: Option A** (disable-with-tooltip for all five tool actions,
superseding the prior hide-based design for these actions specifically).

G10 is an accepted, binding gate; the original hide-based design predates
it and never weighed the reflow cost against the "dimmed icon" cost it was
avoiding. Every admissible objection above is about a *control moving*,
which only Option A (and, at disproportionate cost, Option B) fixes;
Option C is rejected outright. Between A and B, no persona names a reason
to prefer B's outcome over A's, while B carries a real, ongoing
implementation cost (replacing `QAction`-driven buttons with hand-rolled
widgets for five tools) that nothing above justifies. A is also the
*more* G3-consistent shape: the Tools-menu entries for Instant Alpha and
Smart Lasso (`MainWindow::m_instantAlphaAction` /
`m_smartLassoAction`, gated via `applyMlPolicy()`) already disable with an
identical tooltip for the exact same two features — Option A brings the
markup-toolbar buttons into agreement with the menu entries that already
exist for the same capability, rather than leaving two surfaces for one
feature disagreeing on whether "unavailable" means invisible or dimmed.

This narrows, rather than reopens, the original "hidden, not greyed"
principle: it still applies to chrome that has *nowhere to go* at all (an
absent menu item, a feature with no UI surface yet) — see PHILOSOPHY's own
carve-out, "an absent menu item can't carry a tooltip, and shouldn't have
to." It does not apply to a control that already exists in a shared,
position-sensitive toolbar row, where G10 now governs.

## Evidence required to reopen

A reproducible case where a dimmed-but-present tool button in this
specific toolbar row confuses or blocks a real user at a real step —
naming the user, the step, and the failure — together with owner sign-off,
or a superseding decision record. "A row of five icons looks busier when
one or two are dimmed" is already rejected above as a naked preference and
would not on its own meet this bar.
