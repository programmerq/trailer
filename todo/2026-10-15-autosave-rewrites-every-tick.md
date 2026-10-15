# Auto-save rewrites every dirty document every 30 s on the GUI thread, even when nothing changed

Priority: P1. Tags: `save`, `open`.

- Done when: a tick after no new edit writes nothing, and snapshot work causes no GUI-thread
  stall over one frame (counted as GUI-thread engine and file calls: zero).
- Src: 69–88 ms per tick on a 300-page, 22.5 MB file, 247 ms with one redaction; about
  2.7 GB an hour while a large document stays dirty. `MainWindow.cpp:995-1003`, `:1067`.
