# The status bar is always shown, with success toasts and reserved slots that jump

Priority: P1. Tags: `chrome`.

- Done when: `statusBar()` is not visible in the empty, PDF, image and markup states on any OS,
  and no text appears after a successful action ("✓ Saved.", "Page cropped.", "Page copied…",
  "Trailer reset complete."). Anything that must still be shown appears in context and only
  while its state is true. The status bar also carries every error today, so [Errors are posted to the status bar, out of view of what failed](2026-10-15-errors-in-status-bar.md) lands first
  or in the same PR.
- Test: `test_known_bugs_window` (this entry; the PDF state). Not checked: the empty, image and
  markup states; success toasts. `test_uat_constancy_sweep` tolerates this entry in its four
  status-bar rows and, on Linux, fails once one stops moving; the fix deletes those entries.
- Src: Four fixed-width reserved slots take 1,045 px of a 1,280 px window, empty at
  rest (`MainWindow.cpp:176-185`, `:636`, `:778`, `:885`, `:898`), and jump 1–4 px vertically
  when one fills. `flashSuccess` at `:2293`, `:3230`, `:3302`, `:3585`, `:3685`, `:5495`.
