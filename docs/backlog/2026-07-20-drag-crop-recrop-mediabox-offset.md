---
id: 2026-07-20-drag-crop-recrop-mediabox-offset
title: Drag-crop of an already-cropped PDF page is offset by the existing CropBox inset
priority: P3
source: review-before-push pass on PR closing 2026-07-15-crop-pages-direct-manipulation
created: 2026-07-20
---

## Threshold

Dragging a crop rectangle on a page that **already has a smaller /CropBox than
its /MediaBox** produces a crop whose kept region matches the dragged rectangle
exactly (within 1pt), the same as the first crop does. Verified: crop a page,
re-activate Crop Pages by Dragging, draw a rectangle inside the visible area,
commit — the resulting /CropBox equals the drawn rectangle in page space.

## Context

`MainWindow::onCropRectCommitted` (`src/ui/MainWindow.cpp`) converts the
overlay's keep-rectangle (in doc coordinates relative to the page's current
/CropBox) into trim margins, but `PdfEditor::cropPage`
(`src/document/PdfEditor.cpp`) applies those margins relative to the
**/MediaBox**. On an un-cropped page CropBox == MediaBox, so the first
drag-crop is exact (this is the flow the accepted threshold of
`2026-07-15-crop-pages-direct-manipulation` and ADR
`2026-07-20-crop-direct-manipulation` cover, and what ships correctly). A
second drag-crop on the now-cropped page is offset by the existing CropBox
inset.

Fix likely needs `cropPage` (or its caller) to account for the current CropBox
origin, or `pageSizeHint`/a sibling to expose the CropBox-vs-MediaBox offset so
the margin conversion can add it. The numeric Crop dialog has the analogous
MediaBox-absolute behaviour; a shared fix would cover both.
