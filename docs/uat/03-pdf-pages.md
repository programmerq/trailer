# UAT — PDF Page Operations

Covers the Phase 2 work: rotate, delete, move, crop, extract, and insert
pages; and saving the result. PDF viewing is in
[02-viewer.md](02-viewer.md). PDF annotation is in
[05-annotations.md](05-annotations.md).

All operations here produce a modified PDF on disk only after `File > Save`
(or `Save As…`). Before save, the tab shows a `• ` prefix.

---

## Rotate page

### UAT-PDF-001 — Rotate current page right

**Preconditions:** Multi-page PDF open on page 1.
**Steps:**
1. `Tools > Rotate Right` (shortcut `Ctrl+R`).
**Expected:**
- Page 1 rotates 90° clockwise in the view.
- The Sidebar thumbnail for page 1 also rotates.
- Other pages are unchanged.
- The tab becomes dirty (title gains `• `).

### UAT-PDF-002 — Rotate current page left

**Preconditions:** Multi-page PDF open.
**Steps:**
1. `Tools > Rotate Left` (shortcut `Ctrl+L`).
**Expected:**
- Current page rotates 90° counter-clockwise.
- Other pages unchanged.
- Tab marked dirty.

### UAT-PDF-003 — Rotate persists after save

**Preconditions:** PDF with rotation pending (dirty).
**Steps:**
1. `File > Save`.
2. Close the tab.
3. Re-open the same file.
**Expected:**
- The rotated page appears in the same rotation on reopen.
- The tab title has no `• ` prefix.

### UAT-PDF-004 — Rotating a selected page from the Sidebar

**Preconditions:** Multi-page PDF open. Select a non-current page in the
Sidebar `Pages` tab.
**Steps:**
1. With the selection active, `Tools > Rotate Right`.
**Expected:**
- The *selected* page rotates (not just the one in the main viewport).
- Any multi-selection rotates every selected page.

### UAT-PDF-005 — 360° rotation cancels out

**Preconditions:** PDF open, dirty state known before test.
**Steps:**
1. Rotate Right four times on page 1.
2. Save.
3. Reopen.
**Expected:**
- Page 1 looks identical to the original.
- File on disk is valid.

---

## Delete page

### UAT-PDF-010 — Delete via menu

**Preconditions:** 4-page PDF open. Sidebar focused with page 2 selected.
**Steps:**
1. Press `Delete` (or `Backspace`).
**Expected:**
- Page 2 is removed.
- The Sidebar now shows 3 thumbnails, re-numbered 1–3.
- The main view adjusts so no gap is visible.
- Tab marked dirty.

### UAT-PDF-011 — Delete multiple pages

**Preconditions:** 5-page PDF open. In the Sidebar, multi-select pages
2 and 4 (Ctrl/Cmd-click).
**Steps:**
1. Press `Delete`.
**Expected:**
- Pages 2 and 4 are removed.
- 3 thumbnails remain (the original 1, 3, 5), renumbered 1–3.
- Tab marked dirty.

### UAT-PDF-012 — Cannot delete the only remaining page

**Preconditions:** 1-page PDF open.
**Steps:**
1. Select the single page.
2. Press `Delete`.
**Expected:**
- Either the action is rejected with a message, or it is a no-op.
- The PDF still has at least one page — we do not end up with a
  zero-page PDF that can't be saved.

### UAT-PDF-013 — Delete persists after save

**Preconditions:** PDF with deletions pending.
**Steps:**
1. Save.
2. Close and reopen.
**Expected:**
- The deleted pages are gone from the reopened PDF.
- Remaining pages are in the correct order.

### UAT-PDF-014 — Delete is undoable (Known gap)

**Preconditions:** PDF open, a page just deleted (dirty, not yet saved).
**Steps:**
1. `Edit > Undo` (`Cmd+Z` / `Ctrl+Z`).
**Expected (future):** the deletion reverses; the page reappears.
**Current:** PDF edits are not on the undo stack; `Undo` is either
disabled or only reverses annotation edits. Cross-ref TODO.md.

---

## Move page

### UAT-PDF-020 — Drag to reorder

**Preconditions:** 4-page PDF open. Sidebar `Pages` tab visible.
**Steps:**
1. Drag the thumbnail of page 1 below page 3.
**Expected:**
- The thumbnail list reorders; what was page 1 is now in position 3.
- The Sidebar thumbnails renumber 1–4 based on new positions.
- The main view updates to match.
- Tab marked dirty.

### UAT-PDF-021 — Drag multi-selection

**Preconditions:** 5-page PDF open. Select pages 1 and 2 in the Sidebar.
**Steps:**
1. Drag the selection to after page 4.
**Expected:**
- The two pages move as a group.
- New order: original 3, 4, 1, 2, 5 (or 3, 4, 5, 1, 2 depending on drop
  position) — document the actual behaviour observed.
- Tab marked dirty.

### UAT-PDF-022 — Move persists after save

**Preconditions:** PDF with pending reorder.
**Steps:**
1. Save.
2. Reopen.
**Expected:**
- The order matches what the Sidebar showed before save.

---

## Insert pages from file

### UAT-PDF-030 — Insert at end

**Preconditions:** 2-page PDF "A" open. Have a separate 3-page PDF "B"
on disk.
**Steps:**
1. `Tools > Insert Pages from File…`.
2. Pick file "B".
3. Choose "Insert at end" (or the equivalent).
4. Confirm.
**Expected:**
- The active PDF now has 5 pages.
- Pages 3–5 contain "B"'s content in order.
- Tab marked dirty.

### UAT-PDF-031 — Insert before / after current page

**Preconditions:** 3-page PDF open, currently on page 2. Another 2-page
PDF on disk.
**Steps:**
1. `Tools > Insert Pages from File…`, pick the second file, choose
   "Insert after current page" (or similar wording).
**Expected:**
- The PDF now has 5 pages.
- Page 3 is the first page of the inserted PDF.
- Page 4 is the second page of the inserted PDF.
- Page 5 is the original page 3.
- Tab marked dirty.

### UAT-PDF-032 — Insert a non-PDF file

**Preconditions:** PDF open.
**Steps:**
1. `Tools > Insert Pages from File…`.
2. Pick a PNG.
**Expected:**
- An error is shown ("Not a PDF" or similar).
- The document is unchanged. Not marked dirty.

### UAT-PDF-033 — Cancel insert dialog

**Preconditions:** PDF open.
**Steps:**
1. `Tools > Insert Pages from File…`.
2. Cancel the file picker (or the insert confirmation).
**Expected:**
- No modification.
- Tab state unchanged.

### UAT-PDF-034 — Insert persists after save

**Preconditions:** PDF with pending insertion.
**Steps:**
1. Save.
2. Reopen.
**Expected:**
- Page count matches what the Sidebar showed before save.
- Inserted pages are in the correct positions and render correctly.

---

## Extract pages

### UAT-PDF-040 — Drag selected pages out to an external app

**Preconditions:** 5-page PDF open. Sidebar shown. File manager window
visible alongside.
**Steps:**
1. Select pages 2 and 4 in the Sidebar.
2. Drag the selection into the file manager.
3. Release.
**Expected:**
- A new PDF file appears at the drop location.
- Opening that PDF shows exactly 2 pages (the original 2 and 4).
- The source PDF in Trailer is unchanged and not marked dirty.

### UAT-PDF-041 — Single-page drag-out

**Preconditions:** Multi-page PDF open.
**Steps:**
1. Drag a single thumbnail into the file manager.
**Expected:**
- A 1-page PDF is created.
- Source is unchanged.

---

## Crop pages

### UAT-PDF-050 — Open the crop dialog

**Preconditions:** A PDF is open.
**Steps:**
1. `Tools > Crop Pages…`.
**Expected:**
- A dialog titled `Crop Pages` appears with four margin spin-boxes —
  Left, Top, Right, Bottom — each suffixed `mm` and accepting
  fractional values (0.0 – 500.0, one decimal).
- An `Apply to all pages` checkbox, checked by default. Unchecking it
  scopes the crop to the current page only. There is no page-range
  picker yet.
- OK / Cancel buttons.

### UAT-PDF-051 — Apply crop to current page

**Preconditions:** PDF open, crop dialog open.
**Steps:**
1. Set top=10, right=10, bottom=10, left=10.
2. Scope: current page.
3. Apply.
**Expected:**
- The current page's visible area shrinks to exclude the margins.
- Other pages are unchanged.
- Tab marked dirty.

### UAT-PDF-052 — Apply crop to all pages

**Preconditions:** PDF open, crop dialog open.
**Steps:**
1. Enter equal margins on all sides.
2. Scope: all pages.
3. Apply.
**Expected:**
- Every page is cropped identically.
- Tab marked dirty.

### UAT-PDF-053 — Reject oversized margins

**Preconditions:** Crop dialog open.
**Steps:**
1. Enter margins that sum to more than the page size (e.g. 500mm / 500mm
   on an A4 page).
2. Apply.
**Expected:**
- An error appears.
- The document is unchanged and not marked dirty.

### UAT-PDF-054 — Cancel crop dialog

**Preconditions:** Crop dialog open, some margins entered.
**Steps:**
1. Cancel.
**Expected:**
- Dialog closes. No change to document. Not marked dirty.

### UAT-PDF-055 — Crop persists after save

**Preconditions:** PDF with applied crop (dirty).
**Steps:**
1. Save.
2. Reopen.
**Expected:**
- Cropped pages reopen with the same visible area.

---

## Save and Save As

### UAT-PDF-060 — Save overwrites the original

**Preconditions:** PDF open with any pending edit (rotate, delete, etc.).
**Steps:**
1. `File > Save` (`Cmd+S` / `Ctrl+S`).
**Expected:**
- No dialog appears.
- The original file on disk is overwritten.
- Tab title no longer has the `• ` prefix.

### UAT-PDF-061 — Save As writes to a new path

**Preconditions:** PDF open with or without pending edits.
**Steps:**
1. `File > Save As…` (`Cmd+Shift+S` / `Ctrl+Shift+S`).
2. Pick a new filename and directory.
3. Confirm.
**Expected:**
- A new file is created at the chosen path.
- The tab now refers to the new path (its tooltip reflects the new
  path).
- The tab title updates to the new basename and loses any dirty prefix.
- The new file is added to `File > Open Recent`.
- The original file on disk is unchanged.

### UAT-PDF-062 — Save As with overwrite confirmation

**Preconditions:** Save As dialog open. Pick an existing file path.
**Steps:**
1. Click Save.
**Expected:**
- An overwrite confirmation appears (native dialog behaviour).
- Confirming overwrites. Cancelling leaves everything untouched.

### UAT-PDF-063 — Save is disabled for clean documents (optional)

**Preconditions:** PDF just opened, no edits made.
**Steps:**
1. Check `File > Save`.
**Expected:** either enabled (and saving a clean doc is a no-op) or
disabled. Document actual behaviour.

### UAT-PDF-064 — Save fails gracefully on read-only destination

**Preconditions:** Open a PDF from a read-only directory, or make the
destination read-only before saving.
**Steps:**
1. `File > Save`.
**Expected:**
- An error is shown explaining the failure.
- The in-memory document retains its dirty state.
- The app does not crash.

---

## Interaction with annotations

### UAT-PDF-070 — Delete page with annotations

**Preconditions:** PDF with annotations on at least pages 1, 2, and 3.
Delete page 2.
**Steps:**
1. Select page 2 in Sidebar, press Delete.
**Expected:**
- Page 2 is removed.
- Annotations that were on page 2 disappear from the Sidebar
  `Annotations` list.
- Annotations on pages 1 and 3 remain.
- After save+reopen, page 3's annotations are still on what is now
  page 2 (since pages are renumbered).

### UAT-PDF-071 — Move page with annotations

**Preconditions:** PDF with an annotation on page 1.
**Steps:**
1. Move page 1 to position 3.
2. Save and reopen.
**Expected:**
- The annotation remains with its page (now page 3).
- The annotation's position on the page is unchanged.

### UAT-PDF-072 — Rotating a page does not rotate its annotations (document actual behaviour)

**Preconditions:** PDF with a rectangle annotation on page 1.
**Steps:**
1. Rotate page 1 right.
2. Observe the annotation.
**Expected:** document what we actually do. Both "rotate with the page"
and "keep in page coordinates" are defensible; the case exists so the
behaviour is deliberate.

---

## Known gaps

### UAT-PDF-090 — PDF undo/redo for page edits (Known gap)

See TODO.md. PDF edit actions (rotate, delete, move, crop, insert) are
not on the undo stack. Annotation edits are. Selecting `Edit > Undo`
while only page-edits are pending either does nothing or only reverses
annotations.

### UAT-PDF-091 — PDF export to image (Known gap)

**Preconditions:** PDF open.
**Steps:**
1. `Tools > Export As…`.
**Expected (future):** export pages as PNG / JPEG.
**Current:** the action is disabled for PDFs. Only image documents can
be exported.
