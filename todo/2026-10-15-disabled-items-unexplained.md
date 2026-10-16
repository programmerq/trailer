# Disabled items explain nothing, or point at other items

Priority: P1. Tags: `dialogs`.

- Done when: a test walks every menu and toolbar action in the empty, PDF and image states, and
  each disabled action is either a standard edit command (Undo, Redo, Cut, Copy, Paste) or has
  a tooltip that differs from its label and says why, and where to go when there's somewhere
  to go (an image-only command on a PDF says it works on images); switching between a PDF tab
  and an image tab moves no control; there is one OCR command.
- Src: Nine disabled items on a PDF window have a tooltip that only repeats the
  label (Adjust Size…, Adjust Colour…, Remove Background, Instant Alpha…, Smart Lasso…, Export
  As…, Crop Image…, Fill Forms, and one since removed); "Re-run Text Recognition" is disabled
  with a tooltip pointing to another item. `test_uat_recognize_text` (uat_ocr_150, 160, 170)
  pins that Re-run entry today and changes with this fix.
