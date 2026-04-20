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

## Known gaps

### UAT-IMG-090 — PDF flip / colour adjust (Known gap)

Flip Horizontal/Vertical, Adjust Size, Crop, Adjust Colour, and Export
As are image-only. They should be disabled when a PDF is active.
Confirm that state during UAT.

### UAT-IMG-091 — HiDPI image rendering (Known gap)

Large images on 2× displays may show soft rendering. See UAT-VWR-092.
