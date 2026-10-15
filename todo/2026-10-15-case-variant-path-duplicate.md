# On macOS and Windows, the same file spelled with different letter case opens a second copy

Priority: P2. Tags: `platform`, `data-loss`.

- Done when: with `Report.pdf` open, opening `report.pdf` brings the existing window forward on
  macOS and Windows. On Linux, two files that differ only in case stay two documents.
- Src: `PathKey.h:21-28`.
