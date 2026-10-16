# Unsaved work lives in two stores, and Reset doesn't say what it leaves behind

Priority: P2. Tags: `save`.

- Done when:
  - recovery copies and ⌥⌘Q drafts share one store under AppPaths, and their existing tests
    stay green;
  - Reset Trailer's confirmation lists every store it deletes or keeps: the OCR cache is
    deleted, and unsaved work is kept and named as kept;
  - a test checks that the disk matches that list.
- Src: `RecoveryStore.cpp:13-19` uses `QStandardPaths::AppDataLocation/autosave`;
  `AppPaths.cpp:98-99`; `MainWindow.cpp:5456-5495`.
