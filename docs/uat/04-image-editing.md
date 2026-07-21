# UAT — Image Editing

Covers the Phase 3 image edits (rotate, flip, resize, crop, colour
adjust, export), save behaviour, animation playback controls, and
per-image undo/redo. Image viewing basics are in
[02-viewer.md](02-viewer.md); image annotations are in
[05-annotations.md](05-annotations.md).

Image edits are saved to the same format as the source on `File > Save`.
`Tools > Export As…` writes to a format the user chooses.

---

## Rotate

### UAT-IMG-001 — Rotate right

**Preconditions:** A wide image (e.g. 40×20 PNG) is open.
**Steps:**
1. `Tools > Rotate Right` (shortcut `Ctrl+R`).
**Expected:**
- The image rotates 90° clockwise in the view.
- The Inspector `Document` tab reports the new dimensions (e.g. 20×40).
- Tab marked dirty.

### UAT-IMG-002 — Rotate left

**Preconditions:** Image open.
**Steps:**
1. `Tools > Rotate Left` (`Ctrl+L`).
**Expected:**
- The image rotates 90° counter-clockwise.
- Dimensions swap accordingly.
- Tab marked dirty.

### UAT-IMG-003 — Rotation is undoable

**Preconditions:** Image open, dirty=false.
**Steps:**
1. Rotate right.
2. `Edit > Undo`.
**Expected:**
- The image returns to its original orientation.
- Dimensions return to original.
- Tab is no longer dirty (or remains dirty only if there were other
  edits).

### UAT-IMG-004 — Rotation persists across save

**Preconditions:** Image rotated, dirty.
**Steps:**
1. `File > Save`.
2. Close tab.
3. Reopen the same file.
**Expected:**
- Image opens in the rotated orientation.

---

## Flip

### UAT-IMG-010 — Flip horizontal

**Preconditions:** Image with recognisable left/right asymmetry is open.
**Steps:**
1. `Tools > Flip Horizontal`.
**Expected:**
- The image is mirrored horizontally in the view.
- Tab marked dirty.

### UAT-IMG-011 — Flip vertical

**Preconditions:** Image open.
**Steps:**
1. `Tools > Flip Vertical`.
**Expected:**
- The image is mirrored vertically.
- Tab marked dirty.

### UAT-IMG-012 — Flip is undoable

**Preconditions:** Image open, no pending edits.
**Steps:**
1. Flip horizontal.
2. `Edit > Undo`.
**Expected:**
- The image returns to its original state.

### UAT-IMG-013 — Flip persists across save

**Preconditions:** Flipped, dirty image.
**Steps:**
1. Save.
2. Close and reopen.
**Expected:**
- The reopened image reflects the flip.

---

## Resize (Adjust Size)

### UAT-IMG-020 — Open the Adjust Size dialog

**Preconditions:** Image open.
**Steps:**
1. `Tools > Adjust Size…`.
**Expected:**
- A dialog appears with:
  - Width input (px), prefilled with the current width.
  - Height input (px), prefilled with the current height.
  - An aspect-ratio lock toggle (default on).
  - A smooth-scaling toggle.
- An Apply / Cancel pair.

### UAT-IMG-021 — Change width with aspect lock

**Preconditions:** Adjust Size dialog open on a 100×50 image. Lock on.
**Steps:**
1. Enter width = 200.
**Expected:**
- Height auto-updates to 100 (preserves 2:1 ratio).
2. Click Apply.
- The image is now 200×100.
- Tab marked dirty.

### UAT-IMG-022 — Change width without aspect lock

**Preconditions:** Adjust Size open. Lock off.
**Steps:**
1. Enter width=200, height=75.
2. Apply.
**Expected:**
- Image is 200×75.
- Aspect ratio is not preserved.
- Tab marked dirty.

### UAT-IMG-023 — Reject invalid dimensions

**Preconditions:** Adjust Size open.
**Steps:**
1. Enter width=0 (or a negative number).
2. Apply.
**Expected:**
- Apply is either disabled or shows an error.
- The document is unchanged; not dirty.

### UAT-IMG-024 — Cancel dialog

**Preconditions:** Adjust Size open with changed values.
**Steps:**
1. Click Cancel.
**Expected:**
- Dialog closes. Document unchanged. Not dirty.

### UAT-IMG-025 — Resize is undoable

**Preconditions:** Image resized.
**Steps:**
1. Undo.
**Expected:**
- Previous dimensions restored.

---

## Crop

### UAT-IMG-030 — Open Crop dialog

**Preconditions:** Image open.
**Steps:**
1. `Tools > Crop Image…`.
**Expected:**
- Dialog with X, Y, Width, Height inputs (px), prefilled to a sensible
  default (likely the full image or a centred sub-region).
- Apply / Cancel.

### UAT-IMG-031 — Apply a valid crop

**Preconditions:** 100×80 image. Crop dialog open.
**Steps:**
1. Enter X=10, Y=10, Width=50, Height=40.
2. Apply.
**Expected:**
- Image is now 50×40 and shows the selected subregion.
- Tab marked dirty.

### UAT-IMG-032 — Reject crop fully outside image

**Preconditions:** 100×80 image. Crop dialog open.
**Steps:**
1. Enter X=1000, Y=1000, W=10, H=10.
2. Apply.
**Expected:**
- An error appears (or Apply is disabled).
- Image unchanged.

### UAT-IMG-033 — Crop is undoable

**Preconditions:** Image cropped.
**Steps:**
1. Undo.
**Expected:**
- Original pixels and dimensions restored.

---

## Adjust Colour

### UAT-IMG-040 — Open Adjust Colour dialog

**Preconditions:** Image open.
**Steps:**
1. `Tools > Adjust Colour…`.
**Expected:**
- Dialog with sliders for Brightness, Contrast, Saturation.
- Each slider ranges from a low to a high value (typically -100 .. +100
  or -1 .. +1).
- Default position is the neutral mid-point.
- The main view updates live as sliders move (preview).
- Apply / Cancel.

### UAT-IMG-041 — Live preview, then apply

**Preconditions:** Adjust Colour open.
**Steps:**
1. Increase brightness significantly.
**Expected:**
- The image in the main view brightens as the slider moves.
2. Click Apply.
- The brightness adjustment is committed.
- Tab marked dirty.

### UAT-IMG-042 — Cancel discards preview

**Preconditions:** Adjust Colour open. Some sliders adjusted so the
preview differs from the original.
**Steps:**
1. Click Cancel.
**Expected:**
- Dialog closes.
- Main view returns to the pre-dialog appearance.
- Not dirty.

### UAT-IMG-043 — Adjust Colour is undoable

**Preconditions:** Colour adjustment applied.
**Steps:**
1. Undo.
**Expected:**
- Image returns to the pre-adjustment pixels.

---

## Export As

### UAT-IMG-050 — Export as JPEG

**Preconditions:** PNG image open.
**Steps:**
1. `Tools > Export As…`.
2. Choose a destination path with `.jpg` extension.
3. (If a quality prompt appears) accept default.
4. Save.
**Expected:**
- A JPEG file is written at the chosen path.
- The file opens in any standard image viewer.
- The source document in Trailer is unchanged (Export is not Save).

### UAT-IMG-051 — Export respects explicit format choice

**Preconditions:** PNG open.
**Steps:**
1. `Tools > Export As…`.
2. In the format selector, pick TIFF.
3. Save with a `.tif` (or `.tiff`) filename.
**Expected:**
- A TIFF file is written.

### UAT-IMG-052 — Export flattens annotations

**Preconditions:** PNG open with a Rectangle annotation drawn over it.
**Steps:**
1. `Tools > Export As…`, save as PNG.
2. Open the exported file in another viewer.
**Expected:**
- The rectangle is visible in the exported PNG as pixels (it is burnt
  into the image).
- The source document retains the annotation as an editable object.

### UAT-IMG-053 — Export rejects unsupported formats

**Preconditions:** Image open.
**Steps:**
1. `Tools > Export As…`.
2. Type a filename with an extension Qt image plugins don't support
   (e.g. `.xyz`).
3. Save.
**Expected:**
- An error is shown explaining the format is unsupported.
- No file is written.

---

## Save / Save As

### UAT-IMG-060 — Save overwrites source

**Preconditions:** Image open with any pending edit.
**Steps:**
1. `File > Save`.
**Expected:**
- File on disk is overwritten with the edited pixels.
- Tab no longer dirty.
- Annotations (if any) are flattened into the saved pixels.

### UAT-IMG-061 — Save As writes to new path

**Preconditions:** Image open.
**Steps:**
1. `File > Save As…`, pick a new name.
**Expected:**
- A new file is created.
- Tab now refers to the new path and is clean.
- Original file on disk unchanged.
- New file added to Recent.

### UAT-IMG-062 — Save format follows file extension

**Preconditions:** PNG image open.
**Steps:**
1. `File > Save As…`.
2. Choose `foo.jpg` as destination.
**Expected:**
- The written file is a JPEG (not a PNG with the wrong extension).

### UAT-IMG-063 — Save fails gracefully on read-only destination

**Preconditions:** Make source file read-only, or open from a read-only
directory.
**Steps:**
1. Apply any edit.
2. `File > Save`.
**Expected:**
- An error is shown.
- Tab remains dirty (not falsely marked clean).
- App does not crash.

---

## Animation playback

### UAT-IMG-070 — Animation Bar appears for multi-frame files

Covered by UAT-VWR-031 through UAT-VWR-034.

### UAT-IMG-071 — Editing disabled for animated images

**Preconditions:** Animated GIF open.
**Steps:**
1. Check `Tools` menu state.
**Expected:**
- Rotate, Flip, Adjust Size, Crop, Adjust Colour are disabled.
- Export As may be enabled (writes the currently-visible frame only —
  document actual behaviour).

### UAT-IMG-072 — Switching frame via slider

**Preconditions:** Animated GIF paused.
**Steps:**
1. Drag the slider to frame N.
**Expected:**
- The displayed frame matches N.
- The counter shows "N / total".
- No dirty state is introduced.

---

## Undo / redo

### UAT-IMG-080 — Undo stack covers multiple edits

**Preconditions:** Image open.
**Steps:**
1. Rotate right.
2. Flip horizontal.
3. Adjust colour.
4. Press Undo three times.
**Expected:**
- Each Undo reverses the most recent edit in LIFO order.
- After three Undos, the image is back to its original state.
- `Edit > Redo` is enabled and reapplies edits in order.

### UAT-IMG-081 — Undo on an image with annotations

**Preconditions:** Image open; rotate the image; then add a Rectangle
annotation.
**Steps:**
1. Press Undo.
**Expected:**
- The annotation is removed first (annotation undo takes precedence).
- The image remains rotated.
2. Press Undo again.
- The image un-rotates.

### UAT-IMG-082 — Redo after new edit discards forward stack

**Preconditions:** Image edited, undone once (redo available).
**Steps:**
1. Make a different edit (e.g. flip).
2. Check `Edit > Redo`.
**Expected:**
- Redo is disabled (the previous redo path was discarded).

### UAT-IMG-083 — Undo limit

**Preconditions:** Image open.
**Steps:**
1. Perform more than the undo stack's max edits (≈32).
2. Attempt to undo all the way back.
**Expected:**
- The earliest edits are not recoverable (stack is bounded).
- App does not crash.

---

## Remove Background (Phase 6)

Uses the U²-Net Portable ONNX model (~4.4 MB, Apache 2.0). The model is
downloaded on first use and cached under `AppPaths::modelsDir()`.
Thereafter invocations are offline.

### UAT-IMG-100 — Remove Background first-use download

**Preconditions:** Image open, model cache empty (first run, or the
user deleted `<modelsDir>/u2netp.onnx`).
**Steps:**
1. `Tools > Remove Background`.
**Expected:**
- Confirmation dialog appears describing the ~4.4 MB download and the
  Apache 2.0 licence. Accepting kicks off a progress dialog.
- On success, the image alpha channel is replaced so the background
  pixels are transparent and the foreground remains opaque.
- Tab marked dirty; Undo is available.

### UAT-IMG-101 — Remove Background already-cached

**Preconditions:** Image open, model previously downloaded.
**Steps:**
1. `Tools > Remove Background`.
**Expected:**
- No download dialog. The action runs immediately (sub-second on a
  modern CPU).
- Alpha channel populated; tab dirty; undoable.

### UAT-IMG-102 — Remove Background on PDF (disabled)

**Preconditions:** PDF open.
**Steps:**
1. Inspect `Tools` menu.
**Expected:**
- `Remove Background` is greyed out for PDF documents (image-only
  feature).

### UAT-IMG-103 — Remove Background download cancel

**Preconditions:** Image open, model cache empty.
**Steps:**
1. `Tools > Remove Background`.
2. Accept the confirmation dialog.
3. Click **Cancel** in the progress dialog before the download
   completes.
**Expected:**
- The download is abandoned silently. The image is unchanged; dirty
  state unchanged.
- A subsequent invocation re-attempts the download.

### UAT-IMG-104 — Remove Background undo

**Preconditions:** Image with background removed (alpha channel
populated).
**Steps:**
1. `Edit > Undo`.
**Expected:**
- The original opaque image returns.
- Undo stack clears this entry; `Edit > Redo` is available.

### UAT-IMG-105 — Remove Background menu-entry status glyph

Background removal runs asynchronously with **no** progress bar or
spinner widget. The `Tools > Remove Background` menu entry itself is the
status surface (a subtle glyph); the document stays the focus. See
[DR 2026-07-21-bg-removal-menu-status-glyph](../decision-records/2026-07-21-bg-removal-menu-status-glyph.md).
Harness: `uat_bgr_070/080/090` in `tests/uat/test_uat_background_removal.cpp`.

**Preconditions:** An image is open; the U²-Net model is available.
**Steps / Expected:**
1. Open the Tools menu. `Remove Background` is actionable (no status
   glyph, or the "good candidate" sparkle badge when the image scores
   well).
2. Choose `Remove Background`. While it calculates, the entry shows a
   **busy** glyph and stays enabled; its tooltip notes that choosing it
   again cancels. No progress bar or modal appears.
3. Choose the entry again while it is calculating → the op cancels; the
   image is left byte-for-byte unchanged (not dirty, no undo entry) and
   the glyph returns to its actionable state.
4. If an op fails transiently (null result), the entry shows a **failed**
   glyph with a retry tooltip and stays enabled; the document is
   untouched.
5. When the op can't be triggered (non-image document, or the model is
   set to *Never Download*), the entry is disabled and carries a muted
   **unavailable** glyph plus its explain-why tooltip.

---

## Instant Alpha (Phase 6)

Instant Alpha uses MobileSAM to turn a click on the subject into an
alpha cut-out. The encoder (~28 MB) runs once per image; each click
triggers the lighter decoder (~16 MB). Both models download on first
use from the pinned MobileSAM manifest; subsequent runs reuse the
on-disk cache under `AppPaths::modelsDir()`.

### UAT-IMG-110 — Instant Alpha first-use download

**Preconditions:** Image open. No MobileSAM files in the models
cache (fresh install or cache wiped).
**Steps:**
1. `Tools > Instant Alpha…`.
**Expected:**
- A confirmation prompt mentioning that the MobileSAM encoder +
  decoder (~45 MB) will be downloaded once.
- On accept, a progress dialog shows download progress; both models
  land in the models cache with matching SHA-256.
- The Instant Alpha preview dialog opens with the image rendered and
  the instructions for left-click/shift-click prompting.

### UAT-IMG-111 — Instant Alpha already-cached

**Preconditions:** Both MobileSAM files present in the models cache.
**Steps:**
1. `Tools > Instant Alpha…`.
**Expected:**
- No confirmation prompt, no network activity.
- Preview dialog opens immediately with the image ready to prompt.

### UAT-IMG-112 — Instant Alpha click prompting + apply

**Preconditions:** Preview dialog open over an image with a clearly
distinct subject (e.g. centred disc on dark background).
**Steps:**
1. Left-click on the subject.
2. (Optional) Shift-click on background pixels to refine; right-click
   to drop the nearest prompt.
3. Press `OK`.
**Expected:**
- After each click a blue-tinted mask preview appears over the
  subject within ~100 ms of the click (decoder latency).
- `OK` closes the dialog and the background pixels become
  transparent on the canvas.
- Tab marks dirty, `Edit > Undo` is available.

### UAT-IMG-113 — Instant Alpha on PDF (disabled)

**Preconditions:** A PDF is the active document.
**Expected:**
- `Tools > Instant Alpha…` is greyed out (image-only feature).

### UAT-IMG-114 — Instant Alpha undo

**Preconditions:** Instant Alpha just applied.
**Steps:**
1. `Edit > Undo`.
**Expected:**
- The original opaque image returns.
- `Edit > Redo` reapplies the alpha mask.

---

## Smart Lasso (Phase 6)

Smart Lasso reuses the same MobileSAM pipeline as Instant Alpha but
turns the segmentation mask into a polygon outline. Phase 6C ships
Smart Lasso as a *crop-to-object*: accepting the selection crops the
image to the polygon's bounding rectangle. A true polygon-mask +
feather flow is a later phase.

### UAT-IMG-115 — Smart Lasso first-use download

**Preconditions:** Image open. No MobileSAM files in the models
cache. (If the cache was populated during UAT-IMG-110, wipe it first
via `AppPaths::modelsDir()`.)
**Steps:**
1. `Tools > Smart Lasso…`.
**Expected:**
- Same confirmation + progress flow as UAT-IMG-110 — Smart Lasso and
  Instant Alpha share the MobileSAM manifest entries, so a
  previously-downloaded cache is reused across both features.
- On success, the Smart Lasso preview dialog opens.

### UAT-IMG-116 — Smart Lasso click prompting + polygon preview

**Preconditions:** Preview dialog open over an image with a clearly
distinct subject.
**Steps:**
1. Left-click on the subject.
2. (Optional) Shift-click / right-click to refine prompts.
**Expected:**
- A thin amber (255,200,40) polygon outline appears over the mask
  preview, updating each time the prompts change.
- The polygon traces the foreground object's boundary with
  ≥ 3 vertices.

### UAT-IMG-117 — Smart Lasso crop-to-object

**Preconditions:** Polygon preview showing for a subject that is
smaller than the full image.
**Steps:**
1. Press `OK`.
**Expected:**
- Dialog closes and the image is cropped to the polygon's bounding
  rectangle; pixels outside that rect are discarded.
- Inspector `Document` tab reports the new (smaller) dimensions.
- Tab marks dirty; `Edit > Undo` restores the full image.

### UAT-IMG-118 — Smart Lasso on PDF (disabled)

**Preconditions:** A PDF is the active document.
**Expected:**
- `Tools > Smart Lasso…` is greyed out (image-only feature).

### UAT-IMG-119 — Smart Lasso cancel

**Preconditions:** Preview dialog open with a polygon showing.
**Steps:**
1. Press `Cancel` (or close the dialog).
**Expected:**
- Dialog closes without mutating the document.
- Tab is not marked dirty; no undo entry is added.

---

## Recognize Text (Phase 6 / Workstream F)

Recognize Text uses PP-OCRv3 (PaddleOCR) to extract text from raster
content. Two models ship: a DBNet detector (~2.4 MB) and a CRNN
recognizer (~8.9 MB). Both download on first use; subsequent runs
reuse the on-disk cache under `AppPaths::modelsDir()`.

After Workstream F the feature is no longer a "dump text into a
dialog" affair. OCR results feed each document's
`SelectableTextStore`, and the user reads them in-place via a
transparent `SelectableTextLayer` overlay sitting beneath the
annotation overlay. The I-beam cursor shows only when the cursor is
inside a recognised text block; drag-select snaps to block
boundaries; Ctrl+C / Cmd+C copies the joined selection to the
clipboard in reading order.

Submissions go through `MlScheduler`:
- Visible page on the active document: `VisiblePage` priority.
- ±1 neighbour pages: `Prefetch` priority.
- Explicit `Tools > Recognize Text…` dialog: `UserAction` priority.

Large documents (>50 pages) skip the automatic visible-page pump.
A status-bar offer ("Text isn't selectable here. Recognize text on
this page") provides the explicit affordance instead.

### UAT-IMG-120 — Recognize Text first-use download

**Preconditions:** Image containing legible text open (e.g. a scan
of a receipt or document). No PP-OCR files in the models cache.
**Steps:**
1. `Tools > Recognize Text…`.
**Expected:**
- Confirmation prompt mentioning the ~11 MB download (Apache 2.0).
- On accept, a progress dialog shows both models downloading in
  sequence; they land in the models cache with matching SHA-256.
- The Recognize Text parameter dialog opens (page scope: Current /
  All / Range; force-rerun checkbox hidden because the image has
  no text layer to bypass).
- Clicking Run submits the OCR through the ML scheduler; the
  status-bar ML indicator shows "Recognizing text…" while it runs.

### UAT-IMG-121 — Recognize Text already-cached

**Preconditions:** Both PP-OCR files present in the cache.
**Steps:**
1. `Tools > Recognize Text…`.
**Expected:**
- No confirmation prompt, no network activity, no progress dialog.
- The parameter dialog opens immediately.
- After Run, the status-bar indicator briefly shows the running
  task; once recognition completes the I-beam cursor appears over
  the lines on the image.

### UAT-IMG-122 — Drag-select recognised text

**Preconditions:** Recognise Text completed on the current image.
**Steps:**
1. Move the cursor over a recognised line — the I-beam shape
   appears.
2. Press and drag across one or more lines.
**Expected:**
- The dragged blocks receive a translucent selection highlight.
- Pressing Ctrl+C / Cmd+C copies the joined text to the clipboard
  with newlines between blocks in reading order.

### UAT-IMG-123 — Cursor is honest outside text

**Preconditions:** Recognise Text completed on the current image
with at least one recognised block.
**Steps:**
1. Move the cursor over the empty area outside any recognised line.
**Expected:**
- The cursor stays as the arrow / default; no I-beam appears.

### UAT-IMG-124 — Recognize Text on PDF

**Preconditions:** A PDF is the active document (any number of
pages).
**Expected:**
- `Tools > Recognize Text…` is enabled — Workstream F brought PDFs
  into scope.
- The dialog offers a "Force re-run even if a text layer exists"
  checkbox so a corner-watermark-only PDF can be fully OCR'd.

### UAT-IMG-125 — Recognize Text on a blank image

**Preconditions:** An image with no text (e.g. a solid-colour
swatch).
**Steps:**
1. `Tools > Recognize Text…`.
2. Click Run.
**Expected:**
- The status-bar indicator briefly shows the running task.
- The SelectableTextStore receives an entry with zero blocks.
- No I-beam appears anywhere over the image.
- No error popup.

### UAT-IMG-126 — Recognize Text is read-only

**Preconditions:** Fresh image opened (no prior edits).
**Steps:**
1. `Tools > Recognize Text…`.
2. Click Run.
**Expected:**
- Tab is still clean (no dirty indicator).
- `Edit > Undo` is disabled — OCR adds no undo entry.

### UAT-IMG-127 — Large document hint chip

**Preconditions:** A PDF with more than 50 pages, no text layer, no
cached OCR for the visible page.
**Expected:**
- The status-bar shows a small "Text isn't selectable here.
  Recognize text on this page" chip.
- Clicking the link kicks off a UserAction OCR for the visible
  page; the chip hides once results land.

---

## Known gaps

### UAT-IMG-090 — PDF flip / colour adjust (Known gap)

Flip Horizontal/Vertical, Adjust Size, Crop, Adjust Colour, and Export
As are image-only. They should be disabled when a PDF is active.
Confirm that state during UAT.

### UAT-IMG-091 — HiDPI image rendering (Known gap)

Large images on 2× displays may show soft rendering. See UAT-VWR-092.
