# Deleting, moving or inserting pages leaves marks on the wrong page, and Save then drops or misplaces them

Priority: P0. Tags: `annotations`, `data-loss`.

- Done when: take a 3-page PDF with marks on pages 1 and 3.
  - Deleting page 1 removes page 1's mark and shows the page-3 mark on the new page 2: in the
    view, in the annotation store and in the saved file.
  - Moving page 1 to the end carries its mark with it.
  - Inserting a page at the front shifts both marks by one page.
  - ⌘Z after each operation restores the previous placement.
- Test: `test_known_bugs_pdf_save` (this entry). Not checked: the view; moving page 1 to the end,
  which no page can reach until [Dragging a page forward lands it one slot early, and no page can reach the end](2026-10-15-page-drag-lands-early.md) is fixed (the test moves page 3 to the front instead).
- Src: page operations never touch the annotation store (`PdfAdapter.cpp:2259-2276`,
  `:2324-2343`, `:2345-2362`). After deleting page 1 and saving, page 1's mark lands on the new
  page 1, page 3's mark is dropped from the file, and the store keeps it on a page that no
  longer exists.
