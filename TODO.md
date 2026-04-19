# Trailer — Deferred Work

Items noted during development that aren't blocking the current phase.
Should be picked up before shipping or when the surrounding area is
worked on.

## UI

- **Menu organisation review.** Some items currently under Tools may
  belong under File (Export As, Take Screenshot) or Edit (Flip, Rotate,
  Adjust Size, Adjust Colour). Revisit once Phase 4 markup actions land,
  so we can organise them as a group.

## Cross-cutting

- **HiDPI / Retina support.** The app does not yet handle device-pixel
  ratio > 1. Symptoms: screenshots capture logical pixels (not native),
  and raster content may render soft on 2x displays. Needs:
  - `Qt::AA_EnableHighDpiScaling` / `Qt::AA_UseHighDpiPixmaps` audit.
  - `QScreen::devicePixelRatio()` propagated to all grab/paint paths
    (screenshot, thumbnail render, image scaling).
  - Test on 1x, 2x, 3x displays.

- **PDF undo/redo.** Image edits have an undo stack; PDF edits do not
  (cloning `QPDF` per mutation is expensive). Design options:
  - Command pattern: record inverse of each operation (undo rotate =
    rotate opposite direction; undo delete = re-insert).
  - Snapshot-based with copy-on-write tricks in qpdf if possible.

## Screenshot

- **Region / window / app pickers on Linux and Windows.** macOS uses
  `screencapture -i`; Linux falls back to gnome-screenshot if available;
  Windows currently only supports full-screen. Native region-select
  overlays would be a follow-up.
