# Opening a large image or animated GIF blocks the GUI for the full decode

Priority: P1. Tags: `open`.

- Done when: with the image decode gated, `openFiles()` returns and the GUI thread has not run
  `ensureDecoded()` before the gate is released. The badge scorer runs after the first paint,
  off the GUI thread. Animated GIF frames decode off the GUI thread.
- Src: `MainWindow.cpp:3791-3822` and `:4297`; `ImageAdapter.cpp:302-311`.
