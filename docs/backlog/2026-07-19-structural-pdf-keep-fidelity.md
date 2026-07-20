# Structural-PDF keep-fidelity for ⌥⌘Q (Quit and Keep Windows)

- **Filed:** 2026-07-19
- **Area:** app / quit-and-keep-windows, PDF
- **Related:** docs/decision-records/2026-07-16-quit-and-keep-windows.md,
  docs/decision-records/0004-never-worry-save-invariant.md

## Problem

The kept-windows (⌥⌘Q) capture now persists two kinds of unsaved PDF
dirtiness with **no prompt** and full fidelity:

- **Annotation-only dirty PDFs** are captured as their on-disk path plus a
  JSON payload of the unsaved annotations, and on restore reopen from disk
  with the annotations re-applied as **individually editable** objects,
  still dirty (`PdfDocument::restoreAnnotationsFromDraft`).

But a PDF whose dirtiness includes **structural** edits — rotate / delete /
move / insert / crop, applied through the `PdfCommand` stacks and reflected
in `PdfDocument::hasStructuralEdits()` (`m_dirty`) — cannot be reconstructed
from the annotation JSON. This pass does **not** implement a full-document
draft blob for those, so to honour the ADR-0004 no-silent-loss floor a
structurally-edited PDF falls back to the ⌘Q-style per-doc
Save/Discard/Cancel **prompt** even under ⌥⌘Q.

This is the one **residual** where ⌥⌘Q still prompts. It is narrow (only
PDFs with structural page-graph edits) and it never loses data — but it
diverges from the owner's "⌥⌘Q must NEVER prompt" goal for that case.

## Proposed fix

Give structural-edited PDFs the same no-prompt keep as everything else by
writing a **full-document draft blob**:

1. **Capture:** serialize the current edited qpdf document (structural edits
   + pending annotations written as real `/Annot`) to a blob in the draft
   store, associated with the original on-disk path. Reuse the existing PDF
   save machinery (`saveBeginQpdfPhase` / `PdfEditor::save`) but WITHOUT
   mutating the live document's `m_path` / `m_doc`.
2. **Restore:** materialize the blob, open the PDF from the blob bytes so the
   `QPdfDocument` and the qpdf `m_editor` are both consistent with the edited
   content, then re-associate the save target to the **original path** and
   mark the document dirty. The tricky part is decoupling the editor's
   *source* (the blob) from the save *target* (the original path) so a
   subsequent Save writes back to the original file without a stale
   editor re-parsing the un-edited original — that decoupling is the reason
   this was deferred rather than rushed.

Note: a full-blob round-trip may flatten signature / redaction annotations
for that one document (they burn into page content on save), so keep
preferring the annotation-JSON path whenever structural edits are absent —
that path preserves annotation editability in the common case.

## Acceptance

- A structurally-edited PDF kept via ⌥⌘Q restores with its structural edits
  intact, re-associated to its original path, marked dirty, and **no prompt**.
- Saving the restored doc writes to the original path.
- The annotation-only path stays the default when no structural edits exist.
