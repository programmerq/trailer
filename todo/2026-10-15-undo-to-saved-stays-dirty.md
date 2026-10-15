# Undoing back to the saved state leaves the document dirty, and the window is never marked modified

Priority: P1. Tags: `undo`, `save`.

- Done when: edit then undo to the saved state, for a PDF rotate, a PDF mark and an image
  rotate: no dirty marker, `isWindowModified()` false, and close doesn't prompt; Redo makes it
  dirty again. After any edit `isWindowModified()` is true, so macOS shows the close-button dot
  and "Edited". A document restored from crash recovery shows as edited.
- Test: `test_known_bugs_window` (this entry). Not checked: Redo making the document dirty again;
  the close prompt; a document restored from crash recovery.
- Src: `PdfAdapter.cpp:477-480`, `:2209`; `ImageAdapter.cpp:1133`; the
  title uses a "• " prefix and `setWindowModified` is never called. Undoing an image mark does
  clear the dirty state; undoing a PDF mark doesn't.
