# Background removal: subtle menu-entry status glyph, not a progress widget

- **Status:** accepted <!-- proposed | accepted | superseded-by <YYYY-MM-DD-slug> -->
- **Arbiter:** the agent role named for this record; the owner (programmerq) is the escalation-only override.
- **Date proposed:** 2026-07-21
- **Date accepted / superseded:** 2026-07-21 (owner directive on PR #104)

## Context

PR #104 first routed the U²-Net background-removal op through the shared
status-bar `MlProgressWidget` (indeterminate spinner + elapsed "· Ns" text +
a working ✕ / ⌘. cancel), the same affordance OCR uses, per the then-accepted
[ADR 0002 §1/§2](0002-ml-background-removal-progress-cancel.md). The owner
**rejected** that approach and flipped the PR to draft, directing (verbatim):

> "Instead of displaying a progress bar, this should be calculated
> asynchronously. If I open the menu that offers the option to remove the
> background, the menu entry can reflect the status. If it's calculating, a
> glyph can indicate that. If it hasn't been triggered but can be triggered, it
> can offer the option. If it can't be triggered or failed, the icon glyph can
> reflect each of those. … A very subtle UI hint is much better than long-form
> text and progress bars. The document should always be the main focus."

What ships **today on `main`** (before this change): background removal runs
async through `MlScheduler` at `UserAction` priority; the only feedback is the
ambient `m_mlIndicator` status-bar dot (CONVENTIONS §12). The rejected PR #104
v1 added the progress bar on top of that. This record settles how the op's
status is surfaced going forward, and supersedes ADR 0002 §1/§2 **for
background removal** (ADR 0002's no-substitution §5 and its OCR progress
treatment stand unchanged — OCR keeps `MlProgressWidget`).

This record does **not** cover the owner's separately-floated opportunistic
first-pass (cheaply decide candidacy and precalculate the cutout in the
background). That is out of scope here and tracked as a fast-follow; the
existing `BackgroundCandidateScorer` "sparkle" recommendation badge is left
exactly as-is and is orthogonal to the op-status glyph.

## Options

- **A — Progress widget (rejected v1).** Drive `MlProgressWidget` with an
  indeterminate spinner + elapsed text + cancel button, mirroring OCR.
- **B — Menu-entry status glyph (this record).** The op is surfaced only as a
  subtle glyph on the `Tools > Remove Background` entry: a busy glyph while
  calculating, an alert glyph on transient failure, a muted "can't" glyph when
  disabled, and the normal actionable entry (with the existing recommendation
  badge) otherwise. Cancellation is the re-invoke gesture on the same entry.
  The ambient `m_mlIndicator` dot stays as the peripheral "ML is working"
  signal.
- **C — No per-op affordance at all.** Rely solely on the ambient dot. Rejected:
  the owner explicitly wants the *menu entry* to reflect status, and a bare dot
  gives no cancel path and no failed/unavailable signal on the control itself.

## Personas debate

- **Office non-technical user:** wants to click and keep working on the
  document; a bar or modal that grabs the bottom of the window is noise. A
  quiet glyph on the very control they just used is legible without stealing
  focus. (Favours B.)
- **Older careful user:** needs a positive signal that something is happening
  and that a cancel didn't corrupt the image. The busy glyph + "choose again to
  cancel" tooltip, and the byte-for-byte-unchanged guarantee on cancel, cover
  this without a wall of text. (Favours B, provided cancel is discoverable.)
- **Power migrator:** expects Preview/Acrobat-style subtlety — no long-form
  progress narration for a quick local op. (Favours B.)
- **Occasional user:** may forget what the glyph means; the tooltip on each
  state (calculating/failed/unavailable) carries the words, so the glyph is a
  hint, not the sole channel. (Neutral-to-favours B.)

## Admissible objections

- **Cancel discoverability — older/occasional user, mid-op, "how do I stop
  this?":** with the progress widget gone there is no ✕ button. Failure mode:
  the user can't find a way to abort. **Resolution:** re-invoking the same menu
  entry cancels, and the calculating-state tooltip says so ("Removing
  background… (choose again to cancel)"). This is the least-surprising single
  affordance given the owner's "subtlety, not a second surface" directive.
- **Unavailable-glyph noise — any user opening the Tools menu on a non-image
  document:** a bespoke "can't" glyph on a disabled row is a new visual
  convention not used by other disabled entries. Failure mode: reads as broken
  / inconsistent. **Resolution:** the glyph is scoped to *this one entry* per
  the owner's explicit request, is muted (a prohibition mark), appears only
  inside the menu when opened (never on the document or toolbar), and sits
  alongside the established disabled + explain-why tooltip (G3). It does not
  establish an app-wide convention.

### Rejected as naked preference

- "A progress bar is more reassuring." — rejected: states no concrete user,
  step, or failure the glyph model causes; the owner's directive and the
  personas above weigh the other way for a sub-operation this size.

## Checkable threshold this record would establish

The `Tools > Remove Background` menu entry reflects its op status entirely
through the QAction's icon/enabled/tooltip, with **no** `MlProgressWidget`,
progress bar, spinner, or modal for background removal, verifiable as:

1. **Calculating:** on trigger, `action->icon()` is the `status-busy` glyph,
   `action->isEnabled()` is true, and the tooltip names the cancel gesture; the
   `MlProgressWidget` label never contains "Removing background".
2. **Cancel:** re-invoking the entry cancels; `rawImageBytes(after) ==
   rawImageBytes(before)`, `!isDirty()`, `!canUndo()`; the busy glyph clears.
3. **Failed:** an injected null result leaves the `status-failed` glyph + retry
   tooltip, entry still enabled, document untouched.
4. **Unavailable:** a disabled entry (non-image / Never-Download) carries the
   `status-unavailable` glyph and keeps its explain-why tooltip.

Covered by `uat_bgr_070/080/090` in
`tests/uat/test_uat_background_removal.cpp`, with G2 captures of each state.

## Arbiter verdict + rationale

**Option B is adopted; A is superseded for background removal.** The owner's
directive is dispositive and the admissible objections (cancel discoverability,
unavailable-glyph noise) are both answered by concrete design choices rather
than reopening A. CONVENTIONS §12's statement that the ambient dot is "the
only affordance the user sees for background ML" is refined by this record: for
background removal the menu-entry glyph is added as a second, deliberately
subtle surface; the dot remains. §12 is annotated to point here.

## Evidence required to reopen

A concrete, checkable problem the glyph model causes that A would not — e.g.
usability evidence that real users cannot find the cancel gesture, or that the
op routinely runs long enough (≫10s) that an indeterminate glyph is
insufficient and a percent-done bar is warranted — plus owner sign-off.
