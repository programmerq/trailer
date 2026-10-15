# Close → Save on a file changed on disk fails with a status-bar line

Priority: P2. Tags: `save`.

- Done when: a close-time Save of an externally changed file offers the same Reload / Keep mine
  choice as ⌘S.
- Src: `MainWindow.cpp:5773-5797` ("Could not save X; close cancelled.").
