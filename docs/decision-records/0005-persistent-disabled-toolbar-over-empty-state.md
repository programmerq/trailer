# 0005 — Persistent (fully-disabled) document toolbar over the Win/Linux empty state

- **Status:** accepted
- **Arbiter:** the agent role named for this record; the owner (programmerq) is the escalation-only override.
- **Date proposed:** 2026-07-10
- **Date accepted / superseded:** 2026-07-10

## Context

DESIGN §2.4.2 describes the Windows/Linux empty window as carrying **Open**
and **Recent** affordances and a **centered drop-target** — "an unambiguous
*Open a file* prompt with a visible drag zone and a file-picker button.
**Nothing more.**" (DESIGN.md:188-191).

The implementation does not strip the window down to only those affordances.
The persistent empty-state window is a full `MainWindow` that keeps its main
toolbar, menu bar, and dockable Markup/Form toolbars in place; the central
area swaps to the `EmptyStateWidget` welcome surface
(`MainWindow::updateEmptyState()`, `src/ui/MainWindow.cpp`). Every
document-dependent control is disabled while `documentCount() == 0` — rotate,
save, zoom, find, page nav, and (as of this change) the Markup/Form toolbar
**toggle** actions and their View-menu entries / `Ctrl+Shift+A` shortcut. The
document-only toolbars are also hidden over the empty state so they cannot
present tools that would act on a now-closed document.

This record exists because a literal reading of "Nothing more." and the
implementation are in tension: the DESIGN sentence, read strictly, forbids the
surrounding chrome, while the shipped app keeps a standard, fully-disabled
application frame. The G3 finding that motivated this was narrower — the two
toolbar *toggle* actions were the only document-dependent controls **not**
gated, so over the empty state a user could still re-summon a toolbar whose
tools no-op (a "lying control"). That gap is now closed (FIX 1); this record
settles the remaining design question of whether the persistent chrome should
exist at all.

**What ships today (so this record isn't misread as describing a target):** the
persistent empty-state window keeps the full application frame with every
document control honestly disabled, and now gates the toolbar toggles too.

## Options

- **A. Literal "Nothing more."** Over the empty state, tear the window down to
  only the Open/Recent affordances and the centered drop-target — hide or
  remove the main toolbar and the document menus, leaving a minimal welcome
  chrome. The full frame is rebuilt when a document opens.
- **B. Persistent platform-native frame, all controls honestly disabled
  (chosen).** Keep the standard application window intact over the empty state;
  disable every control that needs a document (including the toolbar toggles,
  per FIX 1) and hide the document-only toolbars. "Nothing more." is read as a
  statement about the *central surface* (the empty state shows the welcome
  prompt and nothing more), not a demand to dismantle the window frame.

## Personas debate

- **Office non-technical user:** Expects a normal-looking app window between
  documents; a window that loses its toolbar/menus when the last file closes
  reads as a mode change or a partial crash. Favours B, provided disabled
  controls are visibly greyed (they are).
- **Older careful user:** Wants the frame to stay put and predictable — the same
  menus in the same place whether or not a document is open. A control that is
  present but greyed is honest and unsurprising; a control that vanishes and
  reappears is not. Favours B. Under A, the disappearing chrome is the exact
  "the app rearranged itself on me" failure this lens fears.
- **Power migrator (ex-Preview/Acrobat):** Both Preview and Acrobat keep a
  standard window frame with disabled controls between documents; a stripped
  welcome-only chrome would read as non-native. Favours B; no objection to the
  greyed controls as long as none of them lie (FIX 1 removes the last liar).
- **Occasional user:** Sees the empty state rarely; needs it self-explanatory.
  The centered "Open a file" prompt serves this directly under either option —
  this lens has no stake in whether the surrounding frame persists, only that
  the central surface is clear. Neutral.

## Admissible objections

- **Older careful / office user, Option A:** removing the toolbar and menus over
  the empty state changes the window's shape when the last document closes; the
  concrete failure is "the app looks broken / rearranged when I close my last
  file." This is the decisive argument for B.
- **Any user, pre-FIX-1 Option B:** if the frame persists but a control still
  *acts* over the empty state (the un-gated Markup/Form toolbar toggles), that
  control lies — the concrete failure is "I toggled a toolbar on and its tools
  do nothing." Option B is only admissible **with** every document-dependent
  control disabled, which FIX 1 now guarantees.

### Rejected as naked preference

- "DESIGN says *Nothing more.* so the toolbar must go." — rejected as a naked
  reading: it asserts the letter of one sentence but names no user, step, or
  failure that the persistent, fully-disabled frame causes. The admissible
  version (disappearing chrome surprises the older-careful/office user) points
  the other way.

## Checkable threshold this record would establish

Over the Win/Linux empty state (`documentCount() == 0`), **every** control that
operates on a document is disabled — including the Markup and Form toolbar
toggle actions, their View-menu entries, and the `Ctrl+Shift+A` shortcut — and
the document-only toolbars are hidden. No control on the empty-state window is
both enabled and a no-op. Reopening a document re-enables the gated controls
and restores the user's prior toolbar visibility (no force-show). This is
proven by the UAT case `uat_empty_005_markupToolbarHiddenOverEmptyState`
(`tests/uat/test_uat_empty_state.cpp`), which asserts the toggles are disabled
over the empty state and re-enabled once a document is open.

## Arbiter verdict + rationale

Adopt **Option B**. A persistent, fully-disabled, platform-native document
toolbar and frame over the empty state is the intended Win/Linux shape. It is
what Preview, Acrobat, and standard document apps do; it satisfies the
older-careful and office personas' concrete "don't rearrange the window on me"
objection, which Option A fails. DESIGN §2.4.2's "Nothing more." is read as
governing the **central surface** — the empty state shows the welcome prompt
and nothing more — not as a demand to dismantle the application frame. This
verdict supersedes the literal reading of "Nothing more." The only admissible
objection against B (a control that lies over the empty state) is closed by
FIX 1's gating of the toolbar toggles, so every remaining control is honestly
disabled per G3. This is a design reconciliation within the owner-decided
empty-state model (see `docs/decisions/empty-state-window-model.md`), not a
change to that model, so it sits at arbiter level; the owner override is
escalation-only.

## Evidence required to reopen

A documented user-flow failure caused specifically by the persistent frame
(e.g. a usability finding that the greyed chrome misleads a named persona at a
concrete step), or a discovered empty-state control that is enabled yet no-ops
despite FIX 1, plus owner sign-off.
