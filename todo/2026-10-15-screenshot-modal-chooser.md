# Tools ▸ Take Screenshot is a modal chooser with greyed options, duplicating File ▸ Screenshot

Priority: P1. Tags: `dialogs`, `screenshot`.

- Done when: the only entry point is File ▸ Take Screenshot ▸ From Selection… / From Window… /
  From Entire Screen, with Preview's names. Each item starts its capture with no intermediate
  dialog. Tools ▸ Take Screenshot and its chooser are gone.
- Src: `MainWindow.cpp:2188` and `:3083-3151` (radio buttons disabled off macOS);
  `Application.cpp:1326-1395`.
