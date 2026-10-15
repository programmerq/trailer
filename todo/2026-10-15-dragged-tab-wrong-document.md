# After a tab is dragged, commands act on the other document

Priority: P0. Tags: `save`, `data-loss`.

- Done when: in a two-tab image window, after dragging the first tab to second place with the
  mouse, Save, Rotate, ⌘Z, Close and Export each act on the selected tab's document, and each
  tab keeps its name. (⌘A on a two-tab window is [⌘A selects annotations instead of text, and can act on a background tab](2026-10-15-select-all-selects-annotations.md).)
- Test: `test_known_bugs_window` (this entry; which document is current after the drag). Not
  checked: the five commands themselves; the tab names.
- Src: `DocumentView.cpp:7` calls `setMovable(true)` and nothing handles `tabMoved`.
  `DocumentView.cpp:27-33` looks documents up by index. Opening several images at once makes a
  tabbed window, so this is reachable by default. After the drag, clicking a tab also renames it
  to the other document's name (tooltips stay right).
