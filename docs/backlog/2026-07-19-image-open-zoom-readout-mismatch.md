---
id: 2026-07-19-image-open-zoom-readout-mismatch
title: On open, image renders larger than its pixel size while the zoom readout reads 100%
priority: TBD
status: open
source: surfaced by the ux-walkthrough Tier-1 drive harness (tools/ux-walkthrough/), path 03, 2026-07-19
created: 2026-07-19
---

## Threshold

On open of an image whose pixel size is at or below the viewport (dpr=1), the
image renders **at its pixel size** and the status-bar zoom readout matches the
**actual on-screen magnification**. Concretely, checkable pass/fail:

- Open a 600×420 PNG in a window larger than 600×420 at dpr=1.
- The rendered image is 600 px wide (±rounding), AND the `zoomIndicator`
  readout matches that magnification (i.e. reads `100%` only if the image is
  actually drawn 1:1). The readout and the rendered size must agree.

The bug is Done when the readout no longer disagrees with actual magnification
on open — either the render is corrected to true 1:1, or the readout reflects
the real magnification, whichever root-cause analysis shows is correct.

## Context

The ux-walkthrough drive harness (`tools/ux-walkthrough/`, golden path 3
"open-image → zoom → navigate") captured this on a fresh Linux/Xvfb build:
the committed evidence
[`docs/uat/images/ux-walkthrough-03-opened-100pct.png`](../uat/images/ux-walkthrough-03-opened-100pct.png)
shows the status bar reading **100%** while the 600×420 fixture is drawn
roughly **1165 px** wide — about **194%** actual magnification at dpr=1. The
zoom readout and the real on-screen size disagree on open.

This is exactly the flow/interaction class the ux-walkthrough gate exists to
catch, and it lands in the neighbourhood of the manual-pass findings #2
(windows/zoom open at the wrong size) and #5 (zoom-level indicator) recorded in
[`docs/decision-records/2026-07-16-ux-walkthrough-platform-parity-personas.md`](../decision-records/2026-07-16-ux-walkthrough-platform-parity-personas.md).

**Not root-caused here.** This item only records the *observed* discrepancy from
the harness capture; the cause (default-zoom computation, fit-on-open logic, the
readout's source value, or a dpr/logical-px mismatch in the offscreen/xcb path)
needs to be verified before a fix. Do **not** assume the readout is the wrong
half — confirm which of {rendered size, readout value, intended default} is
correct against the DESIGN/oracle first. The relevant code is around
`MainWindow::updateZoomIndicator()` (`m_zoomIndicator->setText(... zoomFactor()*100 ...)`)
and the image open/zoom path in the document adapter.

Reproduce with: `tools/ux-walkthrough/run.sh 03` and read
`uat-screenshots/ux-walkthrough/<ts>/03-open-zoom-navigate/01-opened.*`.

## Provenance

Surfaced by the ux-walkthrough Tier-1 drive harness during its own
verification run on 2026-07-19, on the branch that landed the harness
(`claude/ux-walkthrough-drive-harness`). Filed so the discrepancy the harness
caught is tracked to a root-cause + fix rather than lost in a session note.
