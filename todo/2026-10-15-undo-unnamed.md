# Undo and Redo don't name the operation

Priority: P2. Tags: `undo`.

- Done when: Edit ▸ Undo and Redo read "Undo Rotate", "Undo Delete Page", "Undo Add Rectangle"
  and so on for every operation type.
- Src: `MainWindow.cpp:1533`; `PdfCommand::description()` is never shown.
