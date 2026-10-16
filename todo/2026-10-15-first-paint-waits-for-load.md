# The window waits for the whole PDF to load before its first paint

Priority: P1. Tags: `open`.

- Done when: with every file read blocked, `openFiles()` returns, a posted event runs, and the
  "Loading…" placeholder paints before the read is released. `addDocument` makes no
  `pageCount()` call, not even inside recorder arguments ([The compiled-out recorder still changes behaviour, and PR CI tests a different configuration from the one that ships](2026-10-15-recorder-changes-behaviour.md)).
- Src: `MainWindow.cpp:5250-5253`; `PdfAdapter.cpp:781-786`; `src/uxrecord/UxRecord.h:53`.
