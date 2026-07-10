# 0004 — Never-worry saving: hard invariant, or default with opt-out?

- **Status:** proposed
- **Arbiter:** the agent role named for this record; the owner (programmerq) is the escalation-only override.
- **Date proposed:** 2026-07-09
- **Date accepted / superseded:** —

## Context

PHILOSOPHY → *Never worry about saving* states the model as **continuous
persistence + an always-honoured explicit Save**: the user should neither lose
work to a forgotten save nor be nagged into one. DESIGN §6.10.1 describes the
auto-save behaviour and adds that **auto-save can be disabled entirely**, in
which case Trailer behaves like a traditional "press Save" app — called there
"the right default for a chunk of users." The older-careful persona (DESIGN
§2.5.2) explicitly "prefers explicit Save over auto-save."

These are in tension. If auto-save can be turned fully off, then for that user
there is no continuous persistence, and the never-worry guarantee does not hold.
The question this record exists to settle — and does **not** pre-decide — is
whether never-worry-save is a hard invariant or a default with an opt-out.

**What ships today (so this record isn't misread as describing the target):**
default-with-opt-out. The disable toggle exists, and continuous save is
debounced at `kAutoSaveIntervalMs = 30000` (30s) at
`src/ui/MainWindow.cpp:392`. The "no silent data loss" floor does **not** hold
today: `docs/uat/01-foundations.md:160-161` (UAT-FND-014, unchanged) documents
that on Close-with-unsaved the window closes immediately with no prompt and
unsaved edits are **silently discarded** — the concrete live gap this record's
threshold must close, so the record is not read as if the floor already holds.

External grounding: Apple's file-management guidance is autosave-by-default —
"avoid making people take an explicit action to save" — while still defining
clear *unsaved-state* affordances (close-button dot, Window-menu dot, save
dialog on close) for when autosave is off
(https://developer.apple.com/design/human-interface-guidelines/file-management).
That is a template for the opt-out option, not a ruling for Trailer.

## Options

- **A. Hard invariant.** Continuous persistence is always on; there is no
  disable toggle. Explicit Save is still honoured. The DESIGN §6.10.1 disable
  toggle would be removed. Never-worry-save becomes an enforceable gate with a
  number (the 30s debounce, or a chosen value).
- **B. Default with opt-out.** Continuous persistence is the default; the
  older-careful user may turn it off, at which point Trailer shows explicit
  unsaved-state affordances (dot on the window/close control, save dialog on
  close/quit) so that user is never surprised. Matches today's behaviour and the
  Apple autosave-off template.

## Personas debate

- **Office non-technical user:** Wants work to survive without thinking about
  it. Favours continuous save on by default; would rarely seek the toggle.
- **Older careful user:** "I need to know when it saves." Explicitly prefers
  pressing Save and seeing an unsaved indicator. This lens is the entire reason
  Option B exists; under Option A they lose the control they want.
- **Power migrator:** From Preview expects autosave; from Acrobat expects manual
  Save. Comfortable either way as long as ⌘S is honoured and means something.
- **Occasional user:** Won't remember the setting; needs whatever the default is
  to be safe, and needs the unsaved state (if any) to be visible.

## Admissible objections

- **Older careful user, Option A:** removing the disable toggle takes away a
  control this user relies on to know their document's save state; concrete
  failure at "I want to decide when this is written to disk." This is the
  strongest argument for Option B.
- **Any user, Option B with no unsaved indicator:** if auto-save is off and
  there is *no* visible unsaved-state affordance, the user edits, closes, and
  silently loses work — a data-loss failure. Option B is only admissible *with*
  the unsaved-state affordances specified.

### Rejected as naked preference

- "Real apps make you press Save." / "Real apps just save." — both rejected:
  each asserts a taste, names no user-step-failure. The admissible versions are
  the two objections above.

## Checkable threshold this record would establish

- **If Option A:** no code path can leave a document with unpersisted edits
  older than the chosen debounce (start from `kAutoSaveIntervalMs = 30000`); the
  disable toggle is gone; a UAT case proves edits survive a simulated crash /
  kill after the debounce window.
- **If Option B:** with auto-save off, every unsaved document shows the
  specified unsaved-state affordance and prompts to save on close/quit; a UAT
  case proves no silent-loss path exists in the off state.

Either way the outcome is a testable "no silent data loss" guarantee; the
options differ only in whether the user may choose manual mode.

## Arbiter verdict + rationale

<Open — status is proposed. Needs owner: this changes a stated design invariant
either way (remove the toggle, or formally bless it as the opt-out), so it is
escalation-level per PHILOSOPHY → the owner is escalation-only.>

## Evidence required to reopen

Once accepted: a documented data-loss path under the accepted model, or a
usability finding that the chosen model fails the older-careful or office user
at a concrete step, plus owner sign-off.
