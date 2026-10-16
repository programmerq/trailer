# Untitled screenshots and clipboard documents are lost on a crash

Priority: P1. Tags: `save`, `data-loss`.

- Done when: take a screenshot document and a New from Clipboard document, each with one mark,
  and kill Trailer with SIGKILL 35 s after the edit. At the next launch both come back with the
  image and the mark, still untitled.
- Src: `MainWindow.cpp:1049`, where auto-save skips untitled documents.
