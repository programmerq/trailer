# Saving a PDF wipes its markup undo history, so ⌘Z then undoes the wrong thing

Priority: P0. Tags: `undo`, `data-loss`.

- Done when: add a mark, rotate a page, Save, then ⌘Z twice: the rotate is reverted, then the
  mark, and the document is marked edited; ⌘⇧Z restores both. The same holds after Save As.
- Test: `test_known_bugs_pdf_save` (this entry).
- Src: `PdfAdapter.cpp:2725` calls `clearHistory()` for annotations but keeps page-op
  history, so ⌘Z after Save skips the newest mark and undoes an older rotate.
