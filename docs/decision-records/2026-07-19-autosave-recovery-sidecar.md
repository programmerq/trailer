# Auto-save persists to a recovery sidecar, never the backing file

- **Status:** accepted
- **Arbiter:** the agent role named for this record; the owner (programmerq) is the escalation-only override.
- **Date proposed:** 2026-07-19
- **Date accepted / superseded:** 2026-07-19 (accepted)

> This record **amends and extends [ADR 0004 — never-worry-save](0004-never-worry-save-invariant.md)**. ADR 0004 closed the *read-side* gap (a Close of a dirty document must not silently drop unsaved edits). This record closes the *write-side* twin: auto-save must not silently **write** the user's file. ADR 0004 stays accepted; this record narrows *how* continuous persistence is implemented so the never-worry promise holds in both directions.

## Context

A P0 data-integrity bug was reported by the owner dogfooding on a real Mac:
freehand annotation on a PDF, then **"I closed the window and chose 'don't
save', but it absolutely changed the file. We don't want to lose data, but we
don't want to overwrite data either."**

Root cause: the 30 s auto-save timer (`kAutoSaveIntervalMs`,
`src/ui/MainWindow.cpp:686`) called `MainWindow::autoSaveDirtyDocs()`, whose
tick invoked `doc->save()` — the same in-place write path as an explicit Save
(`PdfDocument::save` → `saveBeginQpdfPhase`, `src/document/PdfAdapter.cpp`,
`targetPath == m_path`). So an in-progress markup session longer than one 30 s
tick silently rewrote the user's source PDF. The dirty-close **Discard** branch
(`confirmCloseDirtyDoc`) kept no pre-session copy, so choosing "Don't Save"
could not restore the original bytes. ADR 0004's floor ("no silent data loss on
close") was met, but its unstated write-side twin — *the source file is never
written without an explicit Save* — was not.

**What shipped before this record:** auto-save wrote the backing file in place
every 30 s for any dirty titled document, and Discard had no way to undo those
writes.

## Options

- **A. Backup-and-restore on Discard.** Keep auto-save writing the backing
  file, but snapshot the original bytes to a sidecar on first mutation and
  restore them if the user Discards. Preserves "continuous persistence into the
  real file" but means the source *is* transiently written behind the user's
  back, and a crash mid-write or a missed restore still leaves a mutated file.
- **B. Auto-save to a recovery sidecar; the backing file is written only by
  explicit Save/Save-As.** Auto-save persists to a snapshot in app-data, never
  the source. Discard drops in-memory state and the sidecar. Reopen silently
  restores a newer sidecar as a *dirty* document so a crash loses nothing, with
  the source untouched until the user Saves.

## Personas debate

- **Office non-technical user:** Wants work to survive a crash without thinking
  about saving. Indifferent to *where* the recovery copy lives as long as work
  is not lost and files aren't corrupted. Served by B (crash-safe) without the
  corruption risk A carries.
- **Older careful user:** "I decide when this file is written." This is the
  decisive lens: under A the file is written behind their back between saves;
  under B the on-disk file changes only when they press Save, and Discard truly
  means discard. B is the only option that honours "control over when disk is
  touched."
- **Power migrator:** From Preview expects autosave-feel; from Acrobat expects
  manual Save. B gives both: the app never loses work (recovery), and ⌘S is the
  only thing that writes the document — matching Acrobat's mental model and not
  violating Preview's (Preview also never corrupts the original on Discard).
- **Occasional user:** Won't reason about sidecars; needs the default to be
  safe. B's silent restore-to-dirty is invisible until they look, and never
  surprises them with a changed file.

## Admissible objections

- **Older careful / explicit-save user, Option A:** auto-save writing the
  source between saves takes away the exact control they rely on — they can no
  longer trust that an unsaved file on disk is unchanged. Concrete failure at "I
  opened, marked up, chose Don't Save, and my file was different." This is the
  reported P0. **Answered by B:** no path except explicit Save/Save-As writes
  the backing file.
- **Any user, crash mid-session:** if auto-save stops touching the file, a crash
  before an explicit Save must not lose the work. **Answered by B:** the tick
  writes a recovery sidecar; reopen restores it as a dirty document, source
  untouched.
- **External-change watcher (interplay with the in-flight external-file
  monitor):** if sidecars lived next to the user's file, a file watcher would
  fire on them and the user's directory would be littered. **Answered by B:**
  sidecars live under `QStandardPaths::AppDataLocation/autosave/`, keyed by a
  hash of the backing path — never in the user's directory.

### Rejected as naked preference

- "Real editors autosave into the file." — rejected: states a taste, names no
  user-step failure. Its admissible form (crash-safety) is answered by the
  sidecar + reopen-recovery, which loses no work.

## Checkable threshold this record establishes

**Dual invariant, binding all modes:**

1. **Never silently lose in-memory work.** After an auto-save tick, the work is
   recoverable: reopening the backing file restores the in-progress edit as a
   *dirty* document.
2. **Never silently write the backing file.** No code path except an explicit
   Save/Save-As writes the user's file. An auto-save tick leaves the backing
   file **byte-identical** and leaves the document **dirty**. An explicit
   **Discard** leaves the on-disk file byte-identical.

Concretely: auto-save calls `IDocument::writeRecoverySnapshot(sidecar)` (a new
method distinct from `save()`), never `save()`
(`src/ui/MainWindow.cpp` `autoSaveDirtyDocs()`, the write-side guard at
`src/ui/MainWindow.cpp:728`, `doc->writeRecoverySnapshot(sidecar)`; the tick
interval is `kAutoSaveIntervalMs`, `src/ui/MainWindow.cpp:682`). Sidecars are
managed by `RecoveryStore` (`src/document/RecoveryStore.{h,cpp}`) under
app-data. Discard and successful Save both clear the sidecar
(`confirmCloseDirtyDoc`, `saveDocumentAsync`). Reopen restores a newer sidecar
over an unchanged source (`Application::openFiles` recovery hook +
`RecoveryStore::pendingRecovery`).

Proven by the automated tests, all green:

- `tests/test_discard_file_integrity.cpp` —
  `discardAfterAutoSaveLeavesSourceFileByteIdentical` (invariant 2),
  `explicitSaveWritesBackingFile` (Save still writes),
  `reopenRecoveryRestoresAnnotationDirtyButSourcePristine` (invariant 1 + 2).
- `tests/test_recovery_store.cpp` — sidecar-path derivation, record/lookup/clear,
  `pendingRecovery` requires a newer sidecar over an **unchanged** source and
  rejects an externally-changed source, index persistence.
- `tests/uat/test_uat_foundations.cpp` —
  `uat_fnd_030_autoSaveWritesRecoverySidecarNotBackingFile` (backing file
  byte-identical after a tick; doc stays dirty; sidecar recoverable).

## Arbiter verdict + rationale

**Accepted (Option B).** The owner stated the invariant directly — *"no path
writes the backing file except explicit Save/Save-As, and Discard discards only
the in-memory state"* — which resolves the record at arbiter level.

Option A is inadmissible because it still writes the source behind the user's
back and leaves a corruption window on crash or missed restore; it does not
satisfy the older-careful/explicit-save lens whose objection is the reported
P0. Option B satisfies both halves of the dual invariant: crash-safety via the
recovery sidecar (no silent loss) and an untouched backing file until explicit
Save (no silent write). The auto-save-only-minimalist's taste ("just write the
file") carries no weight — its real stake, not losing work, is fully served by
recovery.

This narrows ADR 0004's implementation without weakening it: continuous
persistence remains the default (now into a sidecar), the explicit-Save opt-out
remains, and the dirty-close Save/Discard/Cancel prompt remains the close-time
floor.

## Evidence required to reopen

A documented data-loss path under this model (e.g. a crash sequence where the
sidecar fails to restore in-progress work), or a usability finding that
silent-restore-to-dirty confuses the office or occasional user at a concrete
step, plus owner sign-off.
