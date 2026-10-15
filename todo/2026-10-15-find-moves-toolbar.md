# Find moves the toolbar, opens the sidebar, and leaves it open

Priority: P1. Tags: `chrome`.

- Done when: ⌘F → type → Esc changes no main-toolbar control's or the document view's
  `geometry()` at any step; the sidebar is visible after Esc iff it was before ⌘F; at window
  widths ≥ 900 px the search field is visible at rest. Asserted in Continuous, Single and Two
  Pages at dpr 1 and 2.
- Test: `test_known_bugs_window` (this entry; Continuous). Not checked: the typing step; Single and
  Two Pages. `test_uat_constancy_sweep` tolerates this entry in its Find row (two entries), which the
  fix deletes.
- Src: The toolbar grows 33 → 39 px, all icons shift, the search control moves
  1250 → 889 px, and the sidebar opens before anything is typed (document +266 px).
  `MainWindow.cpp:1616-1660`; `:1626-1631` hides the widget but not its QWidgetAction.
