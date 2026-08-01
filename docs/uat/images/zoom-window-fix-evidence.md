# Zoom-on-open / window-size-on-open fix + transient readout — evidence

## Defects 1+2: stale per-type Custom zoom / window geometry

| | image |
|---|---|
| **Before** (per-type default poisoned with `Custom 80%` zoom + a 2400×1500 window geometry, matching the owner's report) | `zoom-window-defects-before-full.png` / `-before-closeup.png` |
| **After** (same poison, with the fix applied) | `zoom-window-defects-after-full.png` / `-after-closeup.png` |

Same document in both (a synthetic 504×375 JPEG, matching the report's
exact dimensions), same window, only the code differs. **Before**: window
798×774, status-bar readout "80%" (`ZoomMode::Custom`). **After**: window
720×720, readout "100%" (`ZoomMode::Actual`).

**Honesty / caveats:**
- The offscreen Qt platform used to generate these has a small, **fixed**
  virtual screen (~800×800, not configurable via `-platform
  offscreen:size=...` in this Qt build). Both before/after window sizes are
  therefore clamped well below what a real desktop screen would show — on
  a real screen the "before" case is a window filling nearly the whole
  display (matching the reported "HUGE window"), and "after" settles at
  the app's normal 1100×750 floor. The *relative* difference and the
  underlying code path are faithfully reproduced (these are real,
  measured outputs of the actual production code, not fabricated); only
  the absolute pixel scale is compressed by the test screen. The paired
  unit/UAT tests assert the underlying invariant directly (the window
  never matches a reference restore of the poisoned geometry) rather than
  relying on an absolute size threshold, for exactly this reason.
- Generated via `tools/scratch_zoom_window_evidence.cpp`, a throwaway
  evidence harness removed before this PR's final commit (see the PR
  description for the before/after build+run steps used).

## Transient zoom readout (owner directive + gate G10)

| | image |
|---|---|
| **Revealed** (immediately after Zoom In, an explicit zoom action) | `zoom-readout-transient-revealed-full.png` / `-closeup.png` |
| **Faded** (same window, ~3s later, no further zoom action) | `zoom-readout-transient-faded-full.png` |

Same document, same 900×700 window, same Zoom In trigger — only elapsed
time differs. The floating "125%" pill (bottom-right of the document
area) is visible in the first capture and fully gone in the second; no
status-bar chrome is present in either, consistent with the readout no
longer being permanent (DR 2026-07-31-transient-zoom-readout) and no
longer being a status-bar widget at all (DR 2026-07-31-transient-zoom-
readout's G10 addendum — see `uat_zoom_ind_070` for the regression test
this design change was driven by).
