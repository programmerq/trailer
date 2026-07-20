---
id: 2026-07-19-wine-cross-thread-editor-save
title: Confirm on native Windows that saving a background-worker-adopted qpdf editor works (Wine unit tests can't)
priority: unranked
status: open
source: discovered while landing the auto-save recovery-sidecar P0 (PR claude/discard-file-integrity); Windows/Wine CI job
created: 2026-07-19
---

## Threshold

On a **native Windows** build (the MSVC job in `.github/workflows/ci.yml`,
currently `if: false`), a document opened, whose background annotation sweep has
adopted its worker-thread-parsed qpdf editor, then annotated and **same-file
Saved**, writes the backing file successfully (no `save()` failure). Confirm by
either re-enabling the native Windows unit-test job for one run, or running
`test_discard_file_integrity` without the `loadEditorSyncForTesting()` pins on a
real Windows host and observing all cases pass. Close this item once that is
verified (and, if it does NOT pass on native Windows, escalate — that would be a
real product bug in the open→annotate→save flow on Windows).

## Context / Body

`PdfDocument`'s deferred annotation load (DR 0006) parses a fresh qpdf editor on
a QtConcurrent **worker thread** and adopts it as the GUI editor
(`adoptBackgroundLoadResult`, `src/document/PdfAdapter.cpp` — adopts only when
`!m_editorLoaded`). When the user then Saves same-file, `saveCommitOnUi` closes
that editor (`m_editor.reset()`) and `QFile::remove()`s the backing file before
renaming the staged temp over it.

Under the **Wine** cross-build CI job this `save()` returns **false**: Wine's
cross-thread file-handle semantics do not release the worker-opened qpdf file
handle cleanly when the editor is destroyed on the main thread, so the
`QFile::remove()` of the backing file fails. This was pinpointed with unbuffered
stderr checkpoints in `test_discard_file_integrity` (the failing cases stopped
exactly at `doc.save()`), and it reproduces **only** with a worker-adopted
editor — the recovered-document cases, whose editor is loaded on the main thread
in `recoverFrom()`, Save fine under Wine.

The precise mechanism (pinned with unbuffered stderr checkpoints across several
Wine runs): `annotations()` kicks the background sweep (DR 0006), whose worker
thread opens the backing file to parse a to-be-adopted editor
(`startBackgroundLoad`). That editor's qpdf `FILE` handle, opened on the worker
thread, is not released by Wine when the editor is destroyed on the main thread
(whether adopted as `m_editor` or discarded). `save()`'s same-file path then
`QFile::remove()`s the backing file and it fails, so `save()` returns false.
(An earlier attempt to pin the editor to the main thread with a
`loadEditorSyncForTesting` seam did NOT help — the worker still opens the
backing file regardless of which editor is adopted — and was removed.)

On **real Windows** this is expected to work: qpdf uses C stdio `FILE*` /
process-global Win32 handles, which are not thread-affine, so opening on the
worker thread and closing on the main thread during the rename is valid. That is
why this is treated as a **Wine-environment artifact, not a product defect**,
and the P0 recovery-sidecar work does not touch the shipping save path. The two
affected unit-test cases (`explicitSaveWritesBackingFile`,
`recoveryOfPreviouslyAnnotatedPdfDoesNotDuplicateViaBackgroundSweep`) are
`QSKIP`-ped **under Wine only** (`runningUnderWine()` via
`ntdll!wine_get_version`); they run with all assertions intact on Linux (and
would on native Windows). The Discard/byte-integrity (case 1) and recovery
(cases 3/5) coverage still runs on Wine. No assertion was weakened or removed on
any platform.

This item exists to *verify* that Wine-only conclusion on native Windows, since
the native Windows CI job is disabled and Wine is our only automated Windows
signal. If it turns out native Windows also fails, the real fix belongs in the
product (e.g. re-parse the editor on the GUI thread before a same-file save, or
do not adopt the worker editor for the save path) and this becomes a P0 of its
own.
