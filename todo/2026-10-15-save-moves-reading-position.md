# Save, page operations and Undo move the reading position

Priority: P1. Tags: `nav`.

- Done when: after Save, Save As, any page operation, Undo or Redo, the same document point
  stays under the same viewport pixel (±2 px).
- Test: `test_known_bugs_window` (this entry; Save, and Undo and Redo of a rotation). Not checked:
  Save As; the page operations themselves; Undo and Redo of other operations.
- Src: Save resets scrollY 2113 → 0, back to page 1. Undo or Redo of a rotation
  of page 4 while page 3 is on screen jumps to the top of page 3 (1695 → 1454). Every page op
  reloads the viewer, and `jump(savedPage, {})` drops the offset within the page
  (`PdfAdapter.cpp:2140-2166`, `:2694-2714`).
