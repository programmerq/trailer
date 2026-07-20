---
id: 2026-07-19-writeannotations-duplicates-on-successive-saves
title: Successive saves duplicate markup annotations (writeAnnotations appends without a clear)
priority: unranked
status: open
source: discovered while implementing the auto-save recovery-sidecar P0 fix (PR claude/discard-file-integrity)
created: 2026-07-19
---

## Threshold

Saving a PDF document N times with markup annotations must leave exactly the
in-memory store's annotation set in the file each time — never a growing
duplicate set. Concretely: open a PDF, add one Ink stroke, Save (file has 1),
add a second stroke, Save again → the file has **2** annotations, not 3. A
regression test drives `PdfDocument` add→save→add→save and asserts the reloaded
`PdfEditor::readAnnotations().size()` equals the number of distinct strokes.
Close this item in the PR that fixes it, citing this id.

## Context / Body

`PdfEditor::writeAnnotations()` (`src/document/PdfEditor.cpp`) **appends** to
each page's `/Annots` array and does not clear pre-existing managed markup
annotations first. `PdfDocument::saveBeginQpdfPhase()`
(`src/document/PdfAdapter.cpp`) calls `writeAnnotations(store)` against
`m_editor`, and `saveCommitOnUi()` then reloads `m_editor` from the just-written
file and repopulates the store from `readAnnotations()`. So on the *second*
save of an annotated document, the editor already holds the annotations from
the first save and `writeAnnotations` appends the store's set again — the file
ends up with duplicates.

**Empirically confirmed** during the recovery-sidecar work: open blank → add 1
→ save (1 in file) → add 1 → save → **3** in file (expected 2).

This is a **pre-existing** latent bug, independent of the auto-save
recovery-sidecar P0. That P0 fix side-stepped it in its own code paths:
`PdfDocument::writeRecoverySnapshot()` and `recoverFrom()` both call the new
`PdfEditor::clearManagedAnnotations()` before/around writing so the store is the
single source of truth and the recovery snapshot / recovered-then-saved file are
not duplicated. The general fix is to make the normal save path do the same:
call `clearManagedAnnotations()` in `saveBeginQpdfPhase()` immediately before
`writeAnnotations()`, so the store is always authoritative on every save. That
edit was intentionally **not** made in the P0 PR to keep its diff scoped and to
avoid colliding with PR #89's in-flight `PdfDocument::save` refactor
(`claude/external-file-change-handling`); it should be applied once #89's save
changes have landed (or coordinated with them). The `clearManagedAnnotations()`
helper already exists and is tested, so the fix is a one-line addition plus a
regression test.
