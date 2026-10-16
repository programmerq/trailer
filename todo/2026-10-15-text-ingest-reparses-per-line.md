# Native text ingest re-parses the page once per line, on the GUI thread

Priority: P1. Tags: `open`.

- Done when: on open, native-text ingest runs off the GUI thread with one `getAllText()` per
  page; per-line queries remain only as a fallback; and every line's rectangle is within 1 pt
  of the exact per-line extraction on a corpus with superscripts and subscripts, two columns,
  rotated text, right-to-left text, ligatures and an OCR'd scan.
- Src: `PdfAdapter.cpp:1631-1690` (`:1672` runs once per line); about 0.3 s on a 20-page file.
  A prototype that takes one `getAllText()` per page assigns wrong rectangles to lines with
  superscripts, which is the [Marks and text selection land on different content than the user pointed at](2026-10-15-marks-land-off-target.md) class of bug, so it needs the corpus gate above.
