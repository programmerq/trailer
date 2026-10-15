# In a two-tab window, the Inspector, crop, Instant Alpha and Smart Lasso can ignore the shown tab

Priority: P1. Tags: `annotations`, `ocr`.

- Done when: in a window with two documents, on each tab, both right after opening and after a
  tab switch, selecting a mark shows it in the Inspector, Enter over a crop rectangle crops the
  page, and Instant Alpha and Smart Lasso change the shown image.
- Test: `test_known_bugs_window` (this entry; the Inspector). Not checked: switching back to the
  tab opened second; crop, Instant Alpha and Smart Lasso.
- Src: `MainWindow.cpp:4095` connects these to `findChild<AnnotationOverlay *>()`, which in a
  two-tab window returns the hidden tab's layer, so the tab opened last is never connected and
  each tab switch connects the tab being left. The Instant Alpha and Smart Lasso handlers check
  the current document and return, so those tools do nothing rather than edit the other image
  (inferred from the code). Found while fixing [After a tab is dragged, commands act on the other document](2026-10-15-dragged-tab-wrong-document.md).
