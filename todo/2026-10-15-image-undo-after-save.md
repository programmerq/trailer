# On an image, ⌘Z after Save can never remove a mark

Priority: P0. Tags: `undo`, `data-loss`.

- Done when: on a PNG, the sequence draw a rectangle → Save → ⌘Z removes the rectangle from the
  displayed pixels, and ⌘⇧Z restores it. A Save after the ⌘Z writes the file without it. After
  Save, marks are still selectable and editable.
- Test: `test_known_bugs_image` (this entry). Not checked: selecting and editing the mark after Save;
  the test checks only that it is still among the document's marks.
- Src: Save burns the mark into the pixels and clears it from the model
  (`ImageAdapter.cpp:1383-1391`). ⌘Z re-adds the mark over the burned-in pixels; a second ⌘Z
  leaves the pixels red with nothing left to undo.
