# ⌘A selects annotations instead of text

Priority: P1. Tags: `text`.

- Done when:
  - With focus on the page, ⌘A selects all text in the document and ⌘C copies it. This holds
    for native and OCR text, in Continuous, Single and Two Pages (decided: match Preview).
  - With focus in the thumbnails, ⌘A selects all pages.
  - In a two-tab window, ⌘A acts only on the visible tab (true for marks since 2026-10-16;
    text Select All must keep it).
- Src: Select All calls `AnnotationOverlay::selectAll()` on the visible tab's page (`MainWindow.cpp`, the Select All action); it selects marks, not text. The two-tab part was fixed on 2026-10-16.
