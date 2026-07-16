# 0004 — Never-worry saving: hard invariant, or default with opt-out?

- **Status:** accepted
- **Arbiter:** the agent role named for this record; the owner (programmerq) is the escalation-only override.
- **Date proposed:** 2026-07-09
- **Date accepted / superseded:** 2026-07-12 (accepted)

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
default-with-opt-out. Auto-save is the default and the explicit-Save opt-out is
retained: continuous save is debounced at `kAutoSaveIntervalMs = 30 * 1000` (30s) at
`src/ui/MainWindow.cpp:431`, and a persona who prefers to save by hand can still
turn continuous save off and press Save, with the UI presenting and honouring
that option. What **changed with this record** is the floor beneath both modes.
The "no silent data loss" gap that UAT-FND-014 used to document — on
Close-with-unsaved the window closed immediately with no prompt and unsaved edits
were **silently discarded** — is now **closed** by the fix this record adjudicates:
a Close of a dirty document raises a **Save / Discard / Cancel** prompt before any
window or tab closes (`docs/uat/01-foundations.md:155-165`, UAT-FND-014, now
describing the prompt as the expected behaviour). So the floor holds today; this
record ratifies it as a hard invariant rather than describing an open gap.

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

The two contrasting lenses this record turns on:

- **Auto-save-only minimalist ("no dialogs"):** Wants Trailer to just persist and
  never interrupt — no disable toggle, no save prompts, no modal on close. Reads
  every dialog as a failure of the never-worry promise; would prefer Option A with
  the close prompt suppressed. Its concrete stake is *fewer interruptions*, not a
  data-loss claim.
- **Explicit-save controller ("the control must exist and be honoured"):** Wants
  to decide when a document is written and to be able to close without an
  auto-write happening behind their back. Its concrete stake is *control over
  when disk is touched*; it needs the opt-out to survive and needs Close to ask
  rather than silently write or silently drop. This is the older-careful lens made
  explicit, and it is the reason a pure Option A (toggle removed, no prompt) is
  inadmissible.

## Admissible objections

- **Older careful / explicit-save user, Option A:** removing the disable toggle
  takes away a control this user relies on to know their document's save state;
  concrete failure at "I want to decide when this is written to disk." This is the
  strongest argument against a *pure* Option A. **Answered:** the opt-out is
  retained — auto-save is the default but the explicit-Save mode stays, and the UI
  presents and honours it, so this control is not removed.
- **Any user, opt-out mode with no close-time affordance:** if continuous save is
  off and there is *no* prompt or unsaved-state affordance, the user edits,
  closes, and silently loses work — a data-loss failure. **Answered:** the
  just-landed dirty-close **Save / Discard / Cancel** prompt makes explicit-save
  mode honour the floor. On Close of a dirty document, `DocumentView` emits the
  `documentCloseRequested(IDocument*, bool *veto)` veto signal
  (`src/ui/DocumentView.h:40`, emitted `src/ui/DocumentView.cpp:69`), which
  `MainWindow` handles by running `confirmCloseDirtyDoc()`
  (`src/ui/MainWindow.cpp:170-173`, helper at `:3465`) and vetoing the close
  unless the user Saves or Discards. No code path can now close a dirty document
  and silently lose the edits, in either mode.
- **Auto-save-only minimalist, the close prompt itself:** objects that the
  Save/Discard/Cancel dialog is an interruption the never-worry promise should
  avoid. **Answered but not decisive:** the prompt fires only when a document is
  dirty *and* being closed — never during normal editing, and never for a clean
  document (`uat_fnd_014_closeCleanTabNeverPrompts`). In auto-save mode the doc is
  rarely dirty at close, so the interruption is bounded; and preventing silent
  loss is the non-negotiable floor, which outranks a preference for zero dialogs.
  This objection names a real preference but no data-loss step, so it does not
  overturn the invariant.

### Rejected as naked preference

- "Real apps make you press Save." / "Real apps just save." — both rejected:
  each asserts a taste, names no user-step-failure. The admissible versions are
  the two objections above.

## Checkable threshold this record establishes

**Committed floor (binds both modes):** *no silent data loss, ever.* No code path
may close, discard, or replace a document with unsaved edits without the user
having explicitly chosen to lose them. Concretely: closing a **dirty** document
raises a Save / Discard / Cancel prompt and the close is vetoed unless the user
Saves (writes the file; untitled documents route through Save-As) or Discards;
Cancel leaves the document open with its dirty edits intact and nothing written;
closing a **clean** document never prompts. Auto-save being the default vs. the
explicit-Save opt-out being chosen does not change this floor — it holds in both.

This is proven by the six automated UAT-FND-014 cases in
`tests/uat/test_uat_foundations.cpp`, all green:

- `uat_fnd_014_closeDirtyTabCancelKeepsDocAndEdits` — Cancel restores / keeps the
  dirty document and edits;
- `uat_fnd_014_closeDirtyTabDiscardDropsDoc` — Discard drops the doc without
  writing;
- `uat_fnd_014_closeDirtyTabSaveTitledWritesFile` — Save on a titled doc writes
  and closes;
- `uat_fnd_014_closeDirtyTabSaveUntitledRoutesThroughSaveAs` — Save on an untitled
  doc routes through Save-As;
- `uat_fnd_014_closeDirtyNonLastTabCancelThenDiscard` — a non-last (middle) tab
  honours the per-tab veto;
- `uat_fnd_014_closeCleanTabNeverPrompts` — a clean document closes with no prompt.

The historical option split (A: remove the toggle; B: keep it with affordances)
is subsumed: the floor is adopted as a hard invariant that binds every mode, and
the opt-out is retained as an in-mode choice above that floor.

## Arbiter verdict + rationale

**Accepted.** The owner is on record — a decision table presented with veto, **not
vetoed** — and that input resolves this record without a stalemate, so it reaches
`accepted` at arbiter level rather than stalling for escalation.

The call is a synthesis of the two options rather than a pick between them, and it
is deciding-rationale from the owner:

1. **Invariant:** *no silent data loss, ever* is the non-negotiable floor. This is
   Option A's guarantee, but promoted to bind **both** modes rather than being
   bought by removing the toggle.
2. **Mode:** auto-save stays the **default**, and the explicit-Save **opt-out is
   retained** — a persona who wants to save by hand still can, and the UI presents
   and honours that option. This is Option B's structure.
3. **Reconciliation of 0004's open opt-out question:** the opt-out stays, *but even
   in explicit-save mode the no-silent-loss invariant holds.* The just-implemented
   dirty-close **Save / Discard / Cancel** prompt is exactly what makes
   explicit-save mode honour the floor.

Which admissible objections drove it: the explicit-save controller's objection to a
*pure* Option A (don't take away the control over when disk is written) is honoured
by retaining the opt-out; the any-user silent-loss objection against an un-guarded
opt-out is closed by the dirty-close prompt. The auto-save-only minimalist's
objection to the prompt itself is admissible as a preference but does not overturn
the floor — it names no data-loss step, and the prompt is bounded to dirty-close
only, so the invariant outranks it. The naked-preference pair ("real apps make you
press Save" / "real apps just save") carries no weight, as recorded.

**Closing evidence** that the documented data-loss gap is shut: on Close of a dirty
document `DocumentView` emits `documentCloseRequested(IDocument*, bool *veto)`
(`src/ui/DocumentView.h:40`, `src/ui/DocumentView.cpp:69`), which `MainWindow`
answers by running `confirmCloseDirtyDoc()` (`src/ui/MainWindow.cpp:170-173`, helper
at `:3465`) and vetoing the close unless the user Saves or Discards. The six
automated UAT-FND-014 cases in `tests/uat/test_uat_foundations.cpp` (Cancel-restores,
Discard, Save-titled, Save-untitled-via-Save-As, non-last-tab, clean-doc-no-prompt)
are all green, so UAT-FND-014 is now covered by an automated test rather than
standing open as the earlier silent-discard gap. The owner retains the
escalation-only veto and sign-off on this model.

## Evidence required to reopen

Once accepted: a documented data-loss path under the accepted model, or a
usability finding that the chosen model fails the older-careful or office user
at a concrete step, plus owner sign-off.
