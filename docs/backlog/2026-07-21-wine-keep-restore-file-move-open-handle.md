---
id: 2026-07-21-wine-keep-restore-file-move-open-handle
title: Confirm the two "file moved/deleted between ⌥⌘Q and restore" keep round-trips on native Windows (Wine unit tests can't delete the open file)
priority: unranked
status: open
source: discovered landing structural-pdf-keep-fidelity (PR #110); Windows/Wine CI job
created: 2026-07-21
---

## Threshold

On a **native Windows** build (the MSVC job in `.github/workflows/ci.yml`,
currently `if: false`), the two `test_quit_and_keep_windows` cases that simulate
the user moving/deleting the kept file between ⌥⌘Q and relaunch —
`restoreInsertedPagesPdfSurvivesWithoutSource` and
`restoreStructuralPdfMovedOriginalReturnsUntitledDirty` — either (a) pass with
all assertions once the pre-quit document is released before the file is
removed, or (b) are shown to be a genuine Wine-only artifact. Close this item
once one of:

1. The two cases run on a real Windows host (native MSVC job enabled for one
   run, or run locally) with the `runningUnderWine()` guard removed, and pass;
   **or**
2. The tests are restructured to release the pre-quit document (close its
   window, mirroring the process exit that precedes a real file move) **before**
   `QFile::remove(...)`, and that restructured form passes on Linux **and**
   native Windows (leaving, at most, a narrower Wine-only guard on the
   backing-file case if the annotation-sweep worker handle still lingers under
   Wine per the #90 mechanism).

## Context

Both cases delete the kept file with `QFile::remove()` while the ORIGINAL
`PdfDocument` is still alive: these tests stub `performQuit` as a no-op, so the
pre-quit window/document is never torn down. On the Windows/Wine file model a
file held open by a live handle cannot be deleted, so `QFile::remove()` returns
false and the case fails; POSIX unlink-of-an-open-file succeeds, so the full
assertion set runs on Linux. The failing asserts were pinpointed on the Wine CI
lane (run 29795387004): `'QFile::remove(source)' returned FALSE` and
`'QFile::remove(path)' returned FALSE`.

Two distinct handle owners are involved:

- **Insert-source case** (`QFile::remove(source)`): the insert source is opened
  on the **main** thread by `InsertPagesCommand` (kept alive in the undo stack
  for lazy foreign-object copy). Releasing the document (closing the window)
  should free it on both native Windows and Wine — this case is expected to
  become cross-platform-green under restructure option (2).
- **Backing-file case** (`QFile::remove(path)`): the backing file is opened by
  the annotation-sweep **worker** thread and adopted as `m_editor`
  (`adoptBackgroundLoadResult`, `src/document/PdfAdapter.cpp`). Per
  `docs/backlog/2026-07-19-wine-cross-thread-editor-save.md`, Wine does not
  release that cross-thread handle even when the editor is destroyed on the main
  thread, so this case may remain a genuine Wine-only artifact even after
  releasing the document; native Windows (process-global, non-thread-affine
  handles) is expected to release it.

The real ⌥⌘Q keep flow releases the document when the process exits, before the
user moves/deletes the file, so the shipping path is unaffected — this is a
harness fidelity gap, not a product defect. Guarded with `runningUnderWine()`
QSKIP (`kWineOpenFileDeleteSkip` in `tests/test_quit_and_keep_windows.cpp`);
Linux asserts both cases in full. This item exists to verify the conclusion on
native Windows, since the native Windows CI job is disabled and Wine is our only
automated Windows signal.
