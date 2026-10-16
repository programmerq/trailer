# Deleting or renaming a document's folder leaves the change monitor deaf

Priority: P2. Tags: `save`, `data-loss`.

- Done when: the open file's parent directory is removed or renamed, then recreated with the
  file, and the monitor still reports the next change. Checked by a headless
  `ExternalChangeMonitor` test.
- Src: `ExternalChangeMonitor.cpp:62-70`.
