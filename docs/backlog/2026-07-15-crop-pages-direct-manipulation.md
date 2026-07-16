---
id: 2026-07-15-crop-pages-direct-manipulation
title: Crop Pages should support on-document drag with live preview, not blind mm entry
priority: P2
status: open
source: annoyed-end-user persona, 2026-07-15 friction audit
created: 2026-07-15
---

## Threshold

Cropping is achievable by dragging a crop rectangle directly on the page,
with a live preview of the result before committing, instead of typing four
millimetre margins into a no-preview modal. Additionally, pressing OK with
all four margins at zero gives explicit feedback (e.g. a status/flash message)
rather than silently no-opping. Verified: a user can crop a page end-to-end
without opening the numeric dialog, and an all-zero OK produces visible
feedback.

## Context

The Crop Pages command (`src/ui/CropPagesDialog.*`, driven from
`MainWindow::onCropPages`, `src/ui/MainWindow.cpp`) presents a modal with four
`QDoubleSpinBox` margin fields in millimetres and no preview. The user must
guess numeric margins with no visual reference to the page, then commit blind.
This is direct-manipulation friction: the natural gesture — drag a rectangle
over the region to keep — is unavailable.

Separately, `onCropPages` returns early with no feedback when all four margins
are zero (the `if (l == 0.0 && t == 0.0 && r == 0.0 && b == 0.0) return;`
guard), so an accidental all-zero OK looks like nothing happened. Per
PHILOSOPHY → *How Trailer reduces friction*, a committed action that no-ops
should say why.

This is the annoyed-end-user sibling of the Recognize-Text page-range
friction. It is a UX enhancement, not a correctness bug — the single-page
checkbox-noise fix (audit 2026-07-15, Fix B) is a separate, landed change,
recorded in `docs/decision-records/0012-crop-single-page-apply-all-checkbox.md`.

## Provenance

annoyed-end-user persona, 2026-07-15 friction audit. Evidence:
`shots-enduser/dlg-crop-pages.png` (the numeric no-preview modal).
