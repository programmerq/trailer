# Page operations cost O(document) on the GUI thread

Priority: P1. Tags: `open`.

- Done when: rotate, delete and undo make O(1) engine calls on the GUI thread at N = 20,000
  pages (counted at the engine boundary), and the first edit doesn't block on a whole-file
  parse.
- Src: One rotate takes 150 ms at 20,000 pages and 1,048 ms at 100,000; the first edit adds
  a lazy qpdf parse (776 ms / 3,249 ms). `PdfAdapter.cpp:2100-2147` serializes the whole
  document, closes, reloads and clears all OCR. A stable page model (source page, rotation,
  crop; written out only at Save) fixes this together with [⌘Z of a page operation after Save does nothing, marks the document edited, and can make it unsaveable](2026-10-15-page-op-undo-after-save.md), [Save, page operations and Undo move the reading position](2026-10-15-save-moves-reading-position.md) and [Any page operation discards OCR for every page](2026-10-15-page-op-discards-ocr.md).
