# ⌘Z of a page operation after Save does nothing, marks the document edited, and can make it unsaveable

Priority: P0. Tags: `undo`, `data-loss`.

- Done when: for delete, move, rotate, insert and crop, the sequence op → Save → ⌘Z restores
  the pages exactly as before the op, and ⌘⇧Z re-applies it. `undo()` returns false whenever
  nothing was reverted. Save never fails after an undo. A symmetry test over every PdfCommand,
  run across a Save, proves this.
- Test: `test_known_bugs_pdf_save` (this entry; delete and move). Not checked: rotate, insert and
  crop, which undo correctly across a Save today; the symmetry test over every PdfCommand.
- Src: After Delete → Save → ⌘Z, `undo()` returns true, nothing changes, qpdf
  warns "operation for dictionary attempted on object of type destroyed", and Save, Save As and
  the recovery snapshot all fail from then on. After Move → Save → ⌘Z, ⌘⇧Z moves the page
  again (a third order) and Save As writes it. Rotate, insert and crop undo correctly across a
  Save today; delete and move don't.
  - `PdfAdapter.cpp:2206` ignores `revert()`'s result.
  - The commands hold objects of the editor that `saveCommitOnUi` replaces
    (`PdfCommands.h:66-78`; `PdfAdapter.cpp:2634-2735`).
