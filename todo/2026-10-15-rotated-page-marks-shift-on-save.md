# Marks on rotated pages are saved away from where they were drawn

Priority: P0. Tags: `save`, `data-loss`.

- Done when: a rectangle drawn at the visible top-left of a page rotated 90°, 180° and 270°
  shows at the same visible spot when pdfium renders the saved file with annotations on, and
  Trailer re-reads it at that spot. The same holds on a cropped page.
- Test: `test_known_bugs_pdf_save` (this entry).
- Src: drawn at (20,20, 100×60), pdfium paints (762,20, 60×100). `PdfEditor.cpp:1030-1068`
  ignores /Rotate and the CropBox.
