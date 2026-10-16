# ⌘A selects annotations instead of text, and can act on a background tab

Priority: P1. Tags: `text`.

- Done when:
  - With focus on the page, ⌘A selects all text in the document and ⌘C copies it. This holds
    for native and OCR text, in Continuous, Single and Two Pages (decided: match Preview).
  - With focus in the thumbnails, ⌘A selects all pages.
  - In a two-tab window, ⌘A acts only on the visible tab.
- Src: `MainWindow.cpp:1570-1575` calls `findChild<AnnotationOverlay*>()->selectAll()`.
