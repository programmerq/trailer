---
id: 2026-08-02-image-pixmap-rebuild-on-screen-dpr-change
title: Image view does not rebuild its pixmap when the window moves to a different-dpr screen
priority: P3
source: noted while implementing the pixel-exact zoom stop, 2026-08-02
status: open
created: 2026-08-02
---

## Threshold

With an image open on a mixed-DPI multi-monitor setup, drag the window from
a 2x screen to a 1x screen (and back). Without touching any zoom control:

- The zoom readout continues to match the rendered size, and
- if the document was sitting on the pixel-exact stop, it is still rendered
  unresampled after the move (built pixmap pixel size == source pixel size,
  stamped with the **new** screen's devicePixelRatio).

Verified by a test that changes the reported screen dpr and asserts the
rebuilt pixmap, plus a hand check on real mixed-DPI hardware.

## Context / Body

`ImageDocument::viewDevicePixelRatio()` (`src/document/ImageAdapter.h`)
reads the screen dpr **live** from the view label, so the pixel-exact zoom
stop (`src/document/ZoomStops.h`) and the unresampled pass-through in
`buildDisplayPixmap` (`src/document/ImageAdapter.cpp`) always use the
correct value *at the moment they are called*.

What is missing is a trigger: nothing re-runs `applyScale()` when the
window is dragged onto a screen with a different devicePixelRatio, so the
already-built pixmap keeps the old screen's density until the next explicit
zoom action (or fit recompute) happens to rebuild it. The consequence is
cosmetic and self-healing — one zoom tap fixes it — which is why this was
left out of the fix rather than bundled into it.

The likely shape of the fix is a `QEvent::DevicePixelRatioChange` handler on
the view label (or on the scroll area) that calls `applyScale(m_scale)`.
Note the existing `FitModeResizeWatcher` already installs an event filter on
the viewport, so there is a natural home for it; be careful that a rebuild
during a screen change does not fight the fit-mode recompute (see the
`reapplyFitMode` notify-only-on-genuine-change hardening, 2026-07-26).

This gap is documented in-code on the declaration of
`viewDevicePixelRatio()` so it cannot be mistaken for an oversight.

## Provenance

Noted while implementing
`docs/decision-records/2026-08-02-pasted-capture-scale-and-pixel-exact-zoom-stop.md`.
