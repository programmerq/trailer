# Errors are posted to the status bar, out of view of what failed

Priority: P2. Tags: `dialogs`.

- Done when: each failure shows on the document (a banner) or as a sheet and stays until
  dismissed; no error is posted to the status bar.
- Src: 18 `flashError` sites (12 s) and 8 `flashStatus` sites (4 s), `MainWindow.cpp:5227`,
  `:5241`.
