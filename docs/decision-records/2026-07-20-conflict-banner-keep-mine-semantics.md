# 2026-07-20 — Conflict banner: Keep-mine defers the write to Save; deleted file is unsaved-for-close

- **Status:** accepted <!-- proposed | accepted | superseded-by <YYYY-MM-DD-slug> -->
- **Arbiter:** the agent role named for this record; the owner (programmerq) is the escalation-only override.
- **Date proposed:** 2026-07-20
- **Date accepted / superseded:** 2026-07-20 (accepted)
- **Builds on / extends:**
  [`2026-07-19-external-file-change-handling.md`](2026-07-19-external-file-change-handling.md)
  and [`0004-never-worry-save-invariant.md`](0004-never-worry-save-invariant.md).
  The 2026-07-19 record shipped the watch/reload/conflict-guard machinery and
  defined `Keep mine` as an immediate one-shot force-save. This record refines
  the **Keep-mine write semantics** and the **conflict-banner presentation**,
  and closes an adjacent silent-loss gap (a deleted backing file on a clean
  doc) that neither prior record covered. The force-save mechanism
  (`setForceSaveOverExternalChange`) is unchanged and still exercised at the
  adapter level — only the banner's Keep-mine handler stops using it.

## Context

Two HITL/audit findings against the file-change feature merged in PR #89:

- **CF-6 (conflict-banner taste).** The banner's four buttons (Reload / Keep
  mine / Compare / Dismiss) were equal-weight; **"Keep mine" overwrote the
  on-disk copy immediately** on click (`MainWindow.cpp`) with nothing in the
  label saying so; and there was no recommended default. This brushes against
  three owner taste rules: labels should state consequences, a destructive
  write should not be a silent side effect of a banner button, and a
  multi-choice surface should visually recommend the safe default.
- **CF-7 (deleted-file silent loss).** A **clean** document whose backing file
  was **deleted** on disk kept its buffer (the only remaining copy) and showed
  the deleted banner, but was **not** marked as having unsaved work. Dismissing
  the banner and closing the doc/window therefore triggered **no**
  unsaved-changes prompt, and the buffer was dropped silently — the exact
  no-silent-loss floor ADR 0004 established, for a file that vanished rather
  than for the user's own edits. Confirmed by a headless reproduction: with the
  close response forced to Cancel, the clean-but-deleted doc still closed
  (`documentCount()` fell to 0) because the tab-close veto was gated on
  `isDirty()` alone (`MainWindow.cpp`, the `documentCloseRequested` lambda).

## Decision

### CF-6 — Keep-mine defers the write; consequence labels; weighted default

1. **Keep mine does not write on click.** Clicking Keep mine resolves the
   conflict **without touching disk**: it keeps the (already-dirty) buffer,
   **refreshes the load-time baseline** to the current on-disk identity
   (`IDocument::captureFileBaseline()`) so the save-time guard now classifies
   `NoChange`, and dismisses the banner. The file is overwritten only by the
   user's **next explicit Save**, which then succeeds cleanly with no
   re-prompt. **Rationale:** the overwrite happens through a visible,
   user-initiated Save action rather than as a side effect of a banner button —
   a better fit for never-silently-write. This is the recommended semantics
   from the task and the one implemented (the alternative — keep immediate-write
   + an "(overwrite file)" label — was considered and rejected because it makes
   a destructive disk write the direct result of a single banner click, which
   the no-silent-write taste rule disfavours).
2. **Keep mine vs Dismiss are clearly distinct.** Keep mine clears the conflict
   (baseline refresh) so a following Save overwrites cleanly; **Dismiss** only
   hides the banner and leaves the baseline — and thus the save-time guard —
   **armed**, so the next Save re-detects the conflict and re-raises the banner
   (no clobber).
3. **Labels state consequences.** Reload keeps its `Reload (discard my edits)`.
   Keep mine becomes **`Keep mine (Save overwrites the file)`** — it names Save
   as the overwrite path and does **not** claim an on-click write. The Compare
   placeholder tooltip is updated to match ("…or Keep mine to keep your version
   (Save then overwrites the file)").
4. **Visually-weighted default.** **Keep mine is the primary/default button** —
   `setDefault(true)` + `setAutoDefault(true)` plus an accent fill so the
   weighting is visible in the banner. **Justification:** Keep mine is the
   non-destructive-until-save choice — it preserves the user's active work and
   writes nothing until an explicit Save — so it is the safe default to
   recommend. Reload (which discards the user's edits) is a normal secondary
   button; **Dismiss** is flat/passive (`setFlat(true)`) since it decides
   nothing and merely hides the banner.

### CF-7 — a deleted backing file counts as unsaved work for close

A document whose backing file has been **deleted** on disk is treated as having
unsaved work for close purposes, so the never-worry-save prompt fires. New
predicate `IDocument::hasUnsavedWork()` = `isDirty() || externalChangeState() ==
Deleted`. The tab-close veto (`documentCloseRequested`), the window
`closeEvent` dirty walk, and the `"•"` title/tab marker are all gated on
`hasUnsavedWork()` instead of `isDirty()`. The title is refreshed when a
deletion is detected (`onExternalFileDeleted`) so the marker appears
immediately. Choosing **Save** at the prompt recreates the file from the buffer
(the deleted state does not block a save); **Discard** drops it as the user
asked.

## Checkable thresholds this record establishes (G1)

All observable pass/fail, proven headlessly by the tests under Evidence:

1. **Keep mine writes nothing until an explicit Save.** After an external
   overwrite of a dirty doc surfaces the banner, clicking Keep mine changes
   **zero on-disk bytes** (the file is byte-for-byte the external copy), the
   buffer stays dirty, and `externalChangeState()` becomes `NoChange`. A
   following explicit Save then overwrites the file (bytes change) **without**
   re-raising the banner. (`uat_ext_007_keepMineDefersWriteToExplicitSave`.)
2. **Dismiss leaves the guard armed.** After Dismiss, `externalChangeState()`
   is still `DirtyConflict`; a following Save re-raises the conflict banner and
   the on-disk bytes are unchanged (no clobber).
   (`uat_ext_008_dismissLeavesGuardArmed`.)
3. **Consequence labels + weighted default.** The conflict banner's Reload label
   contains "discard"; the Keep-mine label contains "Keep mine", "Save", and
   "overwrite"; Keep mine is the default button and Dismiss is flat.
   (`test_file_change_banner`: `conflictLabelsStateConsequences`,
   `conflictKeepMineIsWeightedDefault`.)
4. **A deleted-backing-file clean doc is unsaved-for-close.** A clean doc whose
   file is deleted shows the `"•"` marker and `hasUnsavedWork()` is true; the
   tab-close request runs the Save/Discard/Cancel prompt (forced Cancel vetoes
   the close and keeps the doc) rather than closing silently.
   (`uat_ext_006_deletedCleanDocIsUnsavedForClose`.)

## Constants / call-sites (G6)

- **`IDocument::hasUnsavedWork()`** at
  [`src/document/IDocument.h`](../../src/document/IDocument.h) — the
  close-prompt predicate (`isDirty()` OR the backing file is `Deleted`).
- **Keep-mine handler** (baseline refresh, no write) at
  [`src/ui/MainWindow.cpp`](../../src/ui/MainWindow.cpp), the
  `FileChangeBanner::keepMineRequested` connection.
- **Keep-mine label + primary/default styling + flat Dismiss** at
  [`src/ui/FileChangeBanner.cpp`](../../src/ui/FileChangeBanner.cpp). The
  accent colour `#2d6cdf` is a plain UI accent (not a tuned magic threshold);
  it reads on the amber banner in both themes.

## Evidence

- Unit: `tests/test_file_change_banner.cpp` (new-label + weighted-default +
  flat-Dismiss assertions, alongside the existing mode/G3-Compare coverage).
- UAT: `tests/uat/test_uat_external_change.cpp` — `UAT-EXT-006` (CF-7
  deleted-clean-doc unsaved-for-close), `UAT-EXT-007` (CF-6 Keep-mine defers
  write to Save), `UAT-EXT-008` (CF-6 Dismiss leaves guard armed). Labelled
  `uat`. Emits the G2 before/after banner pair and the deleted-marker capture
  under `docs/uat/images/`.

## Evidence required to reopen

A path where Keep mine writes to disk without an explicit Save; a path where a
deleted-backing-file doc closes without a prompt; a Dismiss that disarms the
guard; or a usability finding that the deferred-write Keep mine surprises a
concrete user at a concrete step, plus owner sign-off.
