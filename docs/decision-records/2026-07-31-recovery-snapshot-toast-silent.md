# "Recovery snapshot saved" status-bar toast removed; dirty marker + Feedback Report stay as the unsaved-work signal

- **Status:** accepted <!-- proposed | accepted | superseded-by <YYYY-MM-DD-slug> -->
- **Arbiter:** the agent role named for this record (session
  `claude/ui-deference-polish`); the owner (programmerq) is the
  escalation-only override.
- **Date proposed:** 2026-07-31
- **Date accepted / superseded:** 2026-07-31 (owner directive, relayed via
  the session brief — see Context)

## Context

Owner directive (verbatim, relayed via the coordinator brief for this PR):

> "'Recovery Snapshot Saved' is unwanted verbosity that means nothing to
> an end user. Let's avoid that."

**What ships today on `main` (before this record):**
`MainWindow::autoSaveDirtyDocs()` (`src/ui/MainWindow.cpp`) writes a
recovery sidecar for every dirty, path-having, non-untitled open document
on each auto-save tick (never the backing file — see DR
2026-07-19-autosave-recovery-sidecar), and if at least one snapshot was
written, calls `flashSuccess(tr("Recovery snapshot saved."))`, which shows
a `✓ Recovery snapshot saved.` message in the status bar for 6 seconds.

This is a background, timer-driven operation the user never asked to run
and cannot act on — the textbook case
[`docs/ux-guidelines.md`](../ux-guidelines.md) names as the anti-pattern
motivating gate G10 (deference): permanent-surface chrome narrating
routine internals back to a user who didn't request an update. PHILOSOPHY's
never-worry-save model (`../../PHILOSOPHY.md`) already establishes that
saving — recovery snapshots included — is supposed to be silent by design;
a toast announcing that the silent thing happened is the app congratulating
itself for doing its job.

**Verified before removing, not assumed:** does the user lose a signal
they actually need? No — two independent, already-shipped signals report
unsaved work without this toast:
1. The title-bar "•" dirty marker, built from `IDocument::isDirty()` in
   `MainWindow::updateTitleForDocument` and guarded against regressions by
   `tests/test_dirty_marker_zoom.cpp`.
2. The Feedback Report's per-document `*(unsaved changes)*` line
   (`src/diagnostics/FeedbackReport.cpp`, reading `doc->isDirty()`
   directly).

Neither depends on the toast; both are unaffected by removing it.

## Options

- **A. Remove the toast entirely, no replacement.** What ships. The dirty
  marker and Feedback Report remain as the (pre-existing, unaffected)
  unsaved-work signal.
- **B. Downgrade to a quieter affordance** — e.g. a small icon glyph on the
  dirty marker itself, or a one-time first-run explainer. Considered and
  rejected: the dirty marker ALREADY exists and already communicates
  "unsaved work exists"; a snapshot succeeding changes nothing the user
  needs to additionally know (the file is protected either way — before
  the snapshot by nothing, after it by the sidecar — and the user can't
  tell the difference by looking, nor would they want to).
- **C. Keep the toast, silence only autoSave's OWN low-noise ticks (e.g.
  debounce so it fires at most once per session).** Rejected: the toast
  itself is the problem (narrating routine background work), not its
  frequency — this doesn't address the reported complaint.

## Personas debate

- **Office non-technical user:** Has no mental model of "recovery
  snapshot" as a concept; the toast is unexplained jargon interrupting
  their peripheral vision every ~30s while auto-save is on. Favours A.
- **Older careful user:** Wants confidence their work is safe — but gets
  that from the dirty dot (which they already watch, per the existing
  close-prompt / Feedback-Report affordances) and from never-worry-save's
  actual guarantee, not from a transient toast they'd have to notice and
  parse in the moment it appears. A toast that means "something happened"
  without saying whether it matters is closer to noise than reassurance.
  Favours A.
- **Power migrator:** Expects background saves to be invisible, the way
  Google Docs / Notion / Preview.app's autosave is — no app in this class
  narrates "background save complete." Favours A.
- **Occasional user:** Would be MORE confused by a toast referencing
  "recovery" (implying something went wrong) than by its absence. Favours
  A.

## Admissible objections

- None raised that name a concrete user/step/failure caused specifically
  by REMOVING the toast (as opposed to removing the underlying protection,
  which this record does not touch — the sidecar write itself is
  untouched). The two pre-existing signals (dirty dot, Feedback Report)
  were checked and are unaffected; see Context and the Checkable threshold
  below.

### Rejected as naked preference

- "Some visible confirmation feels reassuring." — rejected: states no
  concrete user/step/failure; "reassuring" without an action to take is
  exactly the narration `docs/ux-guidelines.md`'s "Dialogs are for
  decisions, not narration" section rules out, generalised to toasts.

## Checkable threshold this record establishes

- **No status-bar message on a successful auto-save snapshot.**
  `mw->statusBar()->currentMessage().isEmpty()` immediately after
  `autoSaveDirtyDocs()` runs and writes a sidecar. Regression-guarded by
  `uat_fnd_032_recoverySnapshotNeverFlashesStatusBarMessage`
  (`tests/uat/test_uat_foundations.cpp`).
- **The sidecar is still written.** `RecoveryStore::pendingRecovery()`
  resolves for the backing path after the tick — the actual protection is
  untouched, only the announcement is gone. (Same test; also the
  pre-existing `uat_fnd_030_autoSaveWritesRecoverySidecarNotBackingFile`.)
- **The dirty marker survives.** The title-bar "•" is present both before
  and after the auto-save tick. (Same test.)

## Arbiter verdict + rationale

**Option A is adopted.** The owner's directive is dispositive; the
verification step (checking for a genuine gap before deleting, per the
brief) found none — both pre-existing signals the user actually needs are
independent of this toast and remain exactly as they were. Options B and C
were considered and rejected because the toast's frequency and phrasing
were never the problem; its existence as narration of routine, user-
invisible-by-design background work is.

Implementing seam: `MainWindow::autoSaveDirtyDocs()`
(`src/ui/MainWindow.cpp`) — the `flashSuccess(...)` call is replaced with a
direct `uxrecord::recordEvent(...)` call carrying the same event
name/payload the toast's `flashSuccess()` would have logged, so the
local, never-shown, never-networked session-recording trail (used only
during an owner-run HITL capture) still records that a snapshot happened,
for review-pass debugging — per PHILOSOPHY's networking rule this is
local-only instrumentation, not telemetry, and per the brief's "keep any
logging that exists for debugging; this is about the user-facing surface
only."

## Evidence required to reopen

A measured case where a user genuinely needed the toast's specific timing
information (not just "is this doc dirty," which the existing marker
answers) — e.g. HITL evidence that someone paused work waiting to see
"did my recovery snapshot just happen" and had no way to tell — plus owner
sign-off.
