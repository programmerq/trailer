# The Edit menu has no Copy, Cut, Paste or Delete

Priority: P1. Tags: `text`.

- Done when: Edit has Copy, enabled exactly when a selection exists and equal to ⌘C, and Cut,
  Paste and Delete apply to marks, in every view mode.
- Src: ⌘C works only while the overlay or text layer has focus.
  `MainWindow.cpp:1528-1610`.
