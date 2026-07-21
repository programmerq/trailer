# UAT — Annotations / Markup

Covers the Phase 4 work: the Markup Toolbar, creation and editing of
every annotation type, the Inspector's Annotations tab, the PDF text
markup context menu, inline text editing, annotation undo/redo, and
PDF annotation round-trip (save → reopen → annotations preserved).

PDF and image viewing basics are in [02-viewer.md](02-viewer.md).

Annotations are in-memory document state until the document is saved.
Image saves flatten annotations into pixels (UAT-IMG-060); PDF saves
serialise them as `/Annot` objects.

---

## Markup Toolbar

### UAT-ANN-001 — Toolbar presence and tools

**Preconditions:** Any editable document is open.
**Steps:**
1. Look at the Markup Toolbar (top of window).
**Expected:**
- Contains the following tools (radio group): Select, Rectangle,
  Ellipse, Line, Arrow, Freehand, Text, Note, Bubble, Hl Shape (short
  for "Highlight Shape"), Zoom Lens, Highlight, Underline, Strikeout.
- Exactly one tool is active at a time.
- Contains style controls: Stroke (colour), Fill (colour), Width
  (spinner, 0.5–20.0), Dash (dropdown: Solid, Dashed, Dotted).

### UAT-ANN-002 — Toolbar hidden at launch

**Preconditions:** Fresh app launch, no tabs.
**Steps:**
1. Look for the Markup Toolbar.
**Expected:**
- **Current:** the toolbar is hidden by default. Toggle it on via
  `View > Toggle Markup Toolbar` (`Ctrl+Shift+A`). With no tabs, its
  controls are disabled once shown. Cross-ref UAT-FND-001.

### UAT-ANN-003 — Toolbar state per tab

**Preconditions:** Two tabs — one PDF, one stub/`.xyz`.
**Steps:**
1. Switch to the stub tab.
**Expected:**
- Toolbar tools are disabled.
2. Switch to the PDF tab.
- Toolbar tools re-enable.

### UAT-ANN-004 — Style controls persist across tool changes

**Preconditions:** A document supports markup; set stroke colour red,
width 4.0, dash Dashed.
**Steps:**
1. Activate Rectangle. Draw a rectangle.
2. Activate Ellipse. Draw an ellipse.
**Expected:**
- Both shapes are drawn with the same stroke colour (red), width (4.0),
  and dash (Dashed).

### UAT-ANN-005 — Stroke colour picker

**Preconditions:** Markup Toolbar active.
**Steps:**
1. Click the Stroke colour button.
**Expected:**
- A colour dialog appears.
- Picking a colour updates the button's swatch.
- Newly drawn annotations use the new colour.

### UAT-ANN-006 — Fill colour picker with alpha

**Preconditions:** Markup Toolbar active.
**Steps:**
1. Click Fill.
2. Pick a colour with alpha < 255.
**Expected:**
- The swatch shows the new colour, including partial transparency.
- New shape annotations draw with the chosen semi-transparent fill.

### UAT-ANN-007 — Width control

**Preconditions:** Markup Toolbar active.
**Steps:**
1. Set Width = 10.0.
2. Draw a Line.
**Expected:**
- The line is noticeably thicker than a 2.0-width default line.

### UAT-ANN-008 — Dash control

**Preconditions:** Markup Toolbar active.
**Steps:**
1. Set Dash = Dotted.
2. Draw a Rectangle.
**Expected:**
- The rectangle outline is drawn with a dotted pattern.

---

## Creating annotations — shapes

### UAT-ANN-010 — Rectangle

**Preconditions:** PDF open on page 1. Rectangle tool active.
**Steps:**
1. Click-drag from a point to another point.
2. Release.
**Expected:**
- A rectangle annotation appears with the dragged bounds.
- Its stroke and fill match the current toolbar style.
- The tool remains Rectangle (does not auto-switch to Select — unless
  we decide that's the UX; document actual behaviour).
- Tab marked dirty.
- The Sidebar `Annotations` list shows the new entry, page number
  matching.

### UAT-ANN-011 — Ellipse

Identical behaviour to UAT-ANN-010, but an ellipse is drawn bounded by
the dragged rectangle.

### UAT-ANN-012 — Line

**Preconditions:** Line tool active on any editable doc.
**Steps:**
1. Click-drag from A to B.
**Expected:**
- A straight line from A to B.
- Two endpoints stored; no arrowhead.

### UAT-ANN-013 — Arrow

**Preconditions:** Arrow tool active.
**Steps:**
1. Click-drag from A (tail) to B (head).
**Expected:**
- A line with a filled arrowhead at B.
- The arrowhead is oriented along A→B.

### UAT-ANN-014 — Freehand (Ink)

**Preconditions:** Freehand tool active.
**Steps:**
1. Press the mouse button and draw a curvy path without releasing.
2. Release.
**Expected:**
- The drawn path is stroked in the current style.
- The annotation has ≥2 points. Very short jitters are stored as-is.

### UAT-ANN-015 — Highlight Shape

**Preconditions:** Hl Shape tool active, fill colour set with alpha
(e.g. 40% yellow).
**Steps:**
1. Click-drag a rectangle over some page content.
**Expected:**
- A translucent rectangle is drawn; content beneath remains visible.
- The shape is styled using the fill colour (alpha respected).

### UAT-ANN-016 — Zoom Lens

**Preconditions:** Zoom Lens tool active. On any document with visible
content at the click location.
**Steps:**
1. Click-drag a bounds.
**Expected:**
- A circular lens overlay appears at the dragged bounds.
- The lens shows magnified content sampled from the underlying page
  (zoom factor 2× by default, configurable via Inspector — document
  if/where this is exposed).

### UAT-ANN-017 — Select tool does not draw a shape

**Preconditions:** An editable document is open with the Select tool
active (the default). No annotation lies under the drag path.
**Steps:**
1. Click-drag across an empty region of the page.
**Expected:**
- No annotation is created — the annotation count is unchanged and
  there is nothing to undo. Select is for selection and text-selection,
  never shape creation; a drag must not leave a stray rectangle.

### UAT-ANN-018 — Ink captures pen pressure

**Preconditions:** The Ink (freehand) tool is active and input is a
pressure-sensitive stylus.
**Steps:**
1. Draw a stroke, varying pen pressure along the way.
**Expected:**
- The Ink annotation records a pressure value per point, so the
  rendered stroke can taper with pressure. Mouse/trackpad input without
  pressure falls back to a constant-width stroke.

---

## Creating annotations — text

### UAT-ANN-020 — Text (free-text box)

**Preconditions:** Text tool active on any editable document.
**Steps:**
1. Click-drag a rectangle for the text bounds.
2. A text prompt appears; enter "Hello".
3. Confirm.
**Expected:**
- A text annotation renders at the dragged bounds with "Hello" text.
- Text wraps within the bounds.
- Style (stroke colour is used as text colour; font family/weight/size
  match current Inspector defaults).

### UAT-ANN-021 — Note (sticky)

**Preconditions:** Note tool active.
**Steps:**
1. Click once (or click-drag) at a point.
2. A text prompt appears; enter "reminder".
3. Confirm.
**Expected:**
- A small sticky-note icon appears at the click point (≈18×18 px).
- Hovering or selecting reveals the note text.

### UAT-ANN-022 — Speech Bubble

**Preconditions:** Bubble tool active.
**Steps:**
1. Click-drag bounds.
2. Text prompt; enter "Speaking here".
3. Confirm.
**Expected:**
- A rounded-rectangle bubble with a tail points toward the click origin.
- The bubble contains the text, centred / wrapped.

### UAT-ANN-023 — Cancelling the text prompt

**Preconditions:** Creating any text-bearing annotation; the text
prompt opens.
**Steps:**
1. Press Cancel (or Escape).
**Expected:**
- No annotation is committed.
- The tab is not marked dirty as a result.

---

## Creating annotations — text markup (PDF)

Text markup (Highlight / Underline / Strikeout) is driven by selecting
text in the document. These types attach to text runs, not arbitrary
coordinates.

### UAT-ANN-030 — Select text with the Select tool

**Preconditions:** PDF with selectable text is open. Select tool active.
**Steps:**
1. Click-drag across a line of text.
**Expected:**
- The selected text is visually highlighted (standard selection
  colour).
- Releasing preserves the selection.

### UAT-ANN-031 — Highlight via right-click menu

**Preconditions:** Text is selected.
**Steps:**
1. Right-click on the selection.
2. Choose `Highlight`.
**Expected:**
- A highlight annotation is created covering the selected text runs
  (one or more quads).
- The highlight uses the current fill colour if set, else a default
  (yellow).
- The selection is cleared.
- Tab marked dirty.
- Annotation appears in Sidebar list.

### UAT-ANN-032 — Underline via right-click menu

Same as UAT-ANN-031 but choose `Underline`. A thin line is drawn under
each selected line.

### UAT-ANN-033 — Strikeout via right-click menu

Same but `Strikeout`. A line is drawn through each selected line.

### UAT-ANN-034 — Toolbar Highlight/Underline/Strikeout with no selection

**Preconditions:** No text selected. Click Highlight button in toolbar.
**Steps:**
1. Observe.
**Expected:**
- Either a tooltip / message indicates the action needs a text
  selection, or the action is a no-op. No annotation is created.

### UAT-ANN-035 — Text markup on image documents

**Preconditions:** A PNG is open.
**Steps:**
1. Try to select text (no text is present).
**Expected:**
- Nothing is selected.
- The Highlight/Underline/Strikeout tools do nothing meaningful.
  They may still be enabled — document actual behaviour.

---

## Inspector

### UAT-ANN-036 — Highlight stores one quad per text run

**Preconditions:** A PDF with selectable text is open; the Highlight
tool is active.
**Steps:**
1. Drag the highlight across text that spans more than one line.
**Expected:**
- The resulting Highlight annotation stores one rectangle ("quad") per
  covered text run, not a single bounding box — so a wrapped selection
  hugs each line instead of painting the whole block.

### UAT-ANN-040 — Inspector Document tab

**Preconditions:** A document is open. Inspector visible.
**Steps:**
1. Switch to the `Document` tab.
**Expected:**
- Name, Path, Pages (or `—`), Dimensions (or `—`), Status (Modified /
  Clean) are shown.
- Path is selectable for copy.

### UAT-ANN-041 — Properties tab placeholder

**Preconditions:** No annotation selected.
**Steps:**
1. Switch to the `Properties` tab.
**Expected:**
- Shows a placeholder message ("Select an annotation to inspect." or
  similar).

### UAT-ANN-042 — Properties tab for a Rectangle

**Preconditions:** Document has a Rectangle annotation; select it (via
Sidebar or Inspector `Annotations` list, or click on canvas).
**Steps:**
1. Look at the Properties tab.
**Expected:**
- Page (label), Type (label) shown.
- Stroke and Fill colour buttons shown with current colours.
- Width spinner, Dash dropdown shown.
- Font controls are hidden or disabled (Rectangle has no text).
- Text edit is hidden or disabled.

### UAT-ANN-043 — Properties tab for a Text annotation

**Preconditions:** Text annotation selected.
**Steps:**
1. Look at the Properties tab.
**Expected:**
- In addition to UAT-ANN-042 fields: Font Size, Font Family, Weight
  controls are visible.
- A multi-line Text field shows the annotation body.

### UAT-ANN-044 — Edit stroke colour via Inspector

**Preconditions:** Annotation selected (any type).
**Steps:**
1. Click the Inspector Stroke button.
2. Pick a new colour.
**Expected:**
- The annotation in the canvas updates immediately.
- Tab is dirty (or stays dirty).
- Undo reverses the colour change.

### UAT-ANN-045 — Edit width via Inspector

**Preconditions:** Annotation selected.
**Steps:**
1. Change Width to 8.0 with the spinner.
**Expected:**
- Annotation redraws with the new stroke width.
- Undo reverses the change.

### UAT-ANN-046 — Edit text content via Inspector

**Preconditions:** A Text annotation is selected.
**Steps:**
1. Edit the Text field.
**Expected:**
- The annotation on the canvas updates as you type (or on focus-out —
  document actual behaviour).
- Undo reverses the edit.

### UAT-ANN-047 — Inspector Annotations tab (document-level)

**Preconditions:** Document has multiple annotations.
**Steps:**
1. Switch to the `Annotations` tab.
**Expected:**
- A list shows every annotation, formatted roughly as:
  `p<page> <Type> — <first line of text>`.
- Clicking an entry selects that annotation (Properties tab updates).
- Double-clicking navigates to the annotation's page.

---

## Selecting and editing on canvas

### UAT-ANN-050 — Click-to-select

**Preconditions:** A Rectangle annotation exists. Select tool active.
**Steps:**
1. Click inside the rectangle's bounds.
**Expected:**
- The rectangle becomes selected (visual indicator — handles, dashed
  border, or similar).
- Inspector Properties tab shows its properties.

### UAT-ANN-051 — Click-to-deselect

**Preconditions:** An annotation is selected.
**Steps:**
1. Click on empty canvas (outside any annotation).
**Expected:**
- The selection clears.
- Inspector Properties tab returns to placeholder.

### UAT-ANN-052 — Multi-annotation hit-testing

**Preconditions:** Two overlapping annotations (e.g. Rectangle under a
Note).
**Steps:**
1. Click the overlapping region.
**Expected:**
- The topmost (most recently drawn) annotation is selected.

### UAT-ANN-053 — Double-click to edit text inline

**Preconditions:** A Text or SpeechBubble annotation exists.
**Steps:**
1. Double-click it.
**Expected:**
- An inline editor appears on top of the annotation.
- Focus is in the editor.
- Typing updates the body.

### UAT-ANN-054 — Commit inline edit with Ctrl+Return

**Preconditions:** Inline editor open.
**Steps:**
1. Type some text.
2. Press `Ctrl+Return` (or `Cmd+Return`).
**Expected:**
- Editor closes.
- The annotation shows the new text.
- Tab marked dirty.

### UAT-ANN-055 — Cancel inline edit with Escape

**Preconditions:** Inline editor open; field has been modified.
**Steps:**
1. Press Escape.
**Expected:**
- Editor closes.
- Annotation shows its pre-edit text.
- Tab dirtiness is unchanged.

### UAT-ANN-056 — Focus-out commits

**Preconditions:** Inline editor open.
**Steps:**
1. Modify text. Click elsewhere in the window.
**Expected:**
- Editor closes.
- Annotation reflects the new text (committed on focus-out).

### UAT-ANN-057 — Delete key removes the selected annotation

**Preconditions:** An annotation is selected.
**Steps:**
1. Press Delete (or Backspace).
**Expected:**
- The annotation is removed from the canvas and from the Sidebar /
  Inspector lists.
- Tab marked dirty.
- Undo restores the deleted annotation.

### UAT-ANN-058 — Delete with no selection

**Preconditions:** No annotation selected.
**Steps:**
1. Press Delete.
**Expected:**
- Nothing happens.
- No crash.

---

## Undo / redo for annotations

### UAT-ANN-060 — Undo add

**Preconditions:** Document clean.
**Steps:**
1. Add a Rectangle.
2. `Edit > Undo`.
**Expected:**
- The rectangle disappears.
- Sidebar / Inspector lists no longer show it.
- Tab dirty state reverts (clean, since that was the only change).

### UAT-ANN-061 — Undo delete

**Preconditions:** Document has one annotation; delete it.
**Steps:**
1. `Edit > Undo`.
**Expected:**
- The annotation reappears at its original position with original style.

### UAT-ANN-062 — Undo colour / style edit

**Preconditions:** Change the stroke colour of an existing annotation
via the Inspector.
**Steps:**
1. Undo.
**Expected:**
- The colour reverts. Other properties are unaffected.

### UAT-ANN-063 — Redo after undo

**Preconditions:** Any annotation change made, then undone.
**Steps:**
1. `Edit > Redo`.
**Expected:**
- The change is reapplied.

### UAT-ANN-064 — Redo stack clears on new edit

**Preconditions:** Annotation change made, then undone. Redo available.
**Steps:**
1. Make a different annotation change.
**Expected:**
- Redo becomes unavailable (the previous redo branch is discarded).

### UAT-ANN-065 — Undo stack size bound

**Preconditions:** Document open.
**Steps:**
1. Make more than 64 annotation changes.
2. Undo all the way back.
**Expected:**
- The earliest changes are not recoverable (bound is hit).
- App remains responsive.

### UAT-ANN-140 — Interleaved page-op + annotation undo is chronological

**Preconditions:** A multi-page PDF is open.
**Steps:**
1. Delete a page, add an annotation, then delete another page.
2. Undo three times, then redo three times.
**Expected:**
- Undo unwinds in strict reverse order — the second page delete, then
  the annotation, then the first page delete — and redo replays them
  forward. Undo never skips the annotation to unwind both page deletes
  first.

---

## PDF annotation round-trip

### UAT-ANN-070 — Shapes survive save/reopen

**Preconditions:** PDF with a Rectangle, Ellipse, Line, Arrow, and Note
added.
**Steps:**
1. `File > Save`.
2. Close the tab.
3. Reopen the same file.
**Expected:**
- All five annotations reappear.
- Their page, bounds, stroke colour, and fill colour are preserved
  (within tolerance for colour round-tripping).
- The Note's text is preserved.
- Line and Arrow have their two endpoints.

### UAT-ANN-071 — Text markup survives save/reopen

**Preconditions:** PDF with Highlight, Underline, and Strikeout
annotations on selected text.
**Steps:**
1. Save and reopen.
**Expected:**
- All three markup annotations reappear.
- Their per-run quads (positions covering the original text) are
  preserved.
- They still visually align with the underlying text.

### UAT-ANN-072 — Freehand ink survives save/reopen

**Preconditions:** PDF with an Ink annotation (≥10 points).
**Steps:**
1. Save and reopen.
**Expected:**
- The ink annotation reappears with its points in order.
- Line style (width, colour) is preserved.

### UAT-ANN-073 — Reopening via Recent menu

Same as UAT-ANN-070 but reopen via `File > Open Recent` instead of the
dialog. Behaviour must match.

### UAT-ANN-074 — Dirty state after reopen

**Preconditions:** Annotated PDF is saved and reopened.
**Steps:**
1. Observe the tab title and Inspector Status.
**Expected:**
- Tab has no `• ` prefix.
- Inspector `Document` tab shows Status: Clean.
- Loaded annotations do NOT count as pending changes.

### UAT-ANN-075 — Saving with Save As preserves annotations

Same as UAT-ANN-070 but save via `File > Save As…` to a new path. The
new file must contain the annotations; the original is unchanged.

---

## Image annotation round-trip

Image saves flatten annotations. Annotations are not recoverable after
save. This is deliberate and should be documented to the user.

### UAT-ANN-080 — Image save flattens annotations

**Preconditions:** PNG open. Add a Rectangle annotation with red stroke.
**Steps:**
1. `File > Save`.
2. Close the tab.
3. Reopen the same PNG.
**Expected:**
- The rectangle stroke is visible in the image as pixels.
- The annotation is not present as an editable annotation (the Sidebar
  `Annotations` list is empty; the Inspector `Annotations` list is
  empty).

### UAT-ANN-081 — Export As also flattens

Same as UAT-ANN-080 but via `Tools > Export As…` to a new path. The
exported file shows the annotation as pixels; the in-memory document
retains the annotation as an editable object (contrast with save,
which clears it).

---

## Multi-page annotation behaviour (PDF)

### UAT-ANN-090 — Annotations on page 2 stay on page 2

**Preconditions:** Multi-page PDF, currently showing page 1.
**Steps:**
1. Scroll to page 2.
2. Draw a rectangle there.
3. Scroll back to page 1.
**Expected:**
- Page 1 has no new annotation.
- The rectangle is only visible on page 2.
- Sidebar `Annotations` list shows the entry with page 2.

### UAT-ANN-091 — Sidebar list jump to page

**Preconditions:** PDF with annotations on multiple pages.
**Steps:**
1. Click an annotation entry in the Sidebar `Annotations` tab.
**Expected:**
- Main view navigates to that annotation's page.
- The annotation is selected.

### UAT-ANN-092 — Continuous-mode coordinate mapping

**Preconditions:** Multi-page PDF in Continuous view mode.
**Steps:**
1. Scroll such that pages 2 and 3 are both partially visible.
2. Draw a rectangle on page 3 (not page 2).
**Expected:**
- The rectangle is attached to page 3 at the correct coordinates.
- After scrolling to page 3 alone, the rectangle is in the expected
  location.
- After save+reopen, the rectangle is on page 3 at the same spot.

### UAT-ANN-125 — Select All selects every annotation

**Preconditions:** A document with two or more annotations.
**Steps:**
1. With the Select tool active, press `Cmd/Ctrl+A` (or use
   `Edit > Select All`).
**Expected:**
- All annotations receive a dashed selection outline.
- `Edit > Select All` is enabled whenever the document has an
  annotation store (even if currently empty — the action is a
  no-op when no annotations exist).

### UAT-ANN-126 — Select All then Delete removes all in one undo step

**Preconditions:** A document with two or more annotations.
**Steps:**
1. Press `Cmd/Ctrl+A` to select all.
2. Press `Delete` or `Backspace`.
**Expected:**
- All annotations are removed in a single operation.
- A single `Cmd/Ctrl+Z` (Undo) restores all removed annotations
  at once (not one-by-one).

### UAT-ANN-127 — Drag-to-move generates one undo step

**Preconditions:** A document with at least one annotation. Select
tool active; the annotation is selected.

**Steps:**
1. Press inside the annotation's bounds.
2. Drag the mouse across the page (multiple mouse-move events fire
   per drag).
3. Release.
4. Press `Cmd/Ctrl+Z`.

**Expected:**
- The annotation moves with the drag (its bounds shift in doc
  space).
- Exactly one `Cmd/Ctrl+Z` returns the annotation to its
  pre-drag position. The drag is not unwound in micro-steps.
- The same applies to dragging a corner resize handle.

### UAT-ANN-128 — Drawing-tool press over an existing annotation starts a new shape

**Preconditions:** A document with at least one annotation. A bounded
drawing tool (Rectangle / Ellipse / Line / Arrow) is active.

**Rationale:** DRAWING-TOOL PARITY (owner ruling "parity",
2026-07-20; decision record
[`2026-07-20-drawing-tool-parity.md`](../decision-records/2026-07-20-drawing-tool-parity.md)).
The bounded shape tools now match the free-form Ink tool: a press with
a drawing tool active always starts a NEW mark, Preview/Acrobat-style.
Selecting an existing annotation is a **Select-tool-only** gesture.
This inverts the pre-parity behaviour, where a bounded-tool press
selected the shape underneath.

**Steps:**
1. Press-drag starting on top of the existing annotation and drag off
   to the side.

**Expected:**
- A NEW shape of the active tool's type is created (the store gains one
  entry).
- The existing annotation is **not** selected and **not** moved — no
  selection ring appears as a result of the press.
- To select the underlying shape, the user switches to the Select tool
  (UAT-ANN-120).

### UAT-ANN-129 — Freehand (Ink) press over an existing stroke starts a new stroke

**Preconditions:** A document with at least one existing Ink
annotation. The **Ink** (freehand) tool is active.

**Rationale:** Like Preview/Acrobat, a freehand press is always "new
mark" — otherwise the user could never draw over their own ink. Since
DRAWING-TOOL PARITY (2026-07-20) the bounded shape tools behave the
same way (UAT-ANN-128): every drawing-tool press starts a new shape,
and selecting an existing annotation is a Select-tool-only gesture.
This case pins the behaviour specifically for the free-form Ink tool.

**Steps:**
1. Press-drag a freehand stroke that **starts inside** the bounds of
   the existing Ink annotation.
2. Release.

**Expected:**
- A NEW Ink annotation is created (the store gains one entry).
- The original Ink annotation is **not** selected and **not** moved —
  its points are unchanged.
- No selection ring appears as a result of the press.

### UAT-ANN-130 — Stroke colour picker survives concurrent store mutation

**Preconditions:** A document with at least one rectangle annotation.
The rectangle is selected (its selection ring is drawn).

**Steps:**
1. Open the Inspector if it is not already visible (`Cmd-I` on
   macOS, `Ctrl-I` elsewhere).
2. Click the **Stroke** colour swatch button. The
   `QColorDialog` opens.
3. While the picker is open, an event-loop-driven mutation hits
   the `AnnotationStore` (auto-save flush, queued
   `AnnotationStore::changed` slot, a parallel undo, etc.) and
   reallocates its underlying `std::vector<Annotation>`.
4. Pick a new colour and click **OK**.

**Expected:**
- The rectangle is still on the page (it did not vanish).
- Its `bounds` are unchanged from before the dialog opened.
- Its `style.stroke` is now the colour the user picked.
- `Cmd/Ctrl+Z` reverts the colour change in one step.

**Background:** `AnnotationStore::find(id)` returns a raw pointer
into `m_annotations`, which is invalidated by any `add` / `remove`
/ `undo` / `restore` that occurs while a modal dialog is spinning
the Qt event loop. The Inspector must snapshot the initial colour
before opening the dialog and re-fetch the annotation by id after
the dialog returns, rather than holding the `find()` pointer
across the modal.

### UAT-ANN-131 — Bounded shape tool stays sticky after a commit

**Preconditions:** Markup toolbar visible. A bounded drawing tool
(Rectangle / Ellipse / Line / Arrow) is armed via the toolbar.

**Rationale:** DRAWING-TOOL PARITY (owner ruling "parity",
2026-07-20; decision record
[`2026-07-20-drawing-tool-parity.md`](../decision-records/2026-07-20-drawing-tool-parity.md)).
The bounded shapes now match Freehand's sticky-draw. This inverts the
pre-parity behaviour, where a bounded shape auto-reverted to Select on
commit.

**Steps:**
1. Drag on the page to draw a rectangle; release.
2. Drag again on empty space to draw a second rectangle; release.

**Expected:**
- Both rectangles are committed to the document.
- After the first shape the markup toolbar's checked tool STAYS on
  **Rectangle** — it does NOT revert to Select.
- The second drag therefore draws a new shape; it does not become a
  rubber-band selection.

**Note:** All the bounded shape tools (Rectangle / Ellipse / Line /
Arrow) and Freehand (Ink) are sticky. The non-sticky tools that revert
to Select on commit are the stamp/region tools (HighlightShape,
Redaction, ZoomLens, Signature) and the text-markup tools (Highlight,
Underline, StrikeOut); the text/edit tools (Text, Note, SpeechBubble)
open an inline editor instead of committing through this path.

---

### UAT-ANN-132 — Freehand (Ink) is sticky: consecutive strokes all draw

**Preconditions:** Markup toolbar visible. Freehand (Ink) tool armed
via the toolbar button.

**Steps:**
1. Drag on the page to draw a first stroke; release.
2. Drag again (anywhere, including over the first stroke) to draw a
   second stroke; release.

**Expected:**
- Both strokes are committed as independent Ink annotations.
- After the first stroke the toolbar's checked tool STAYS on
  **Freehand** — it does NOT revert to Select.
- The second drag therefore draws a new stroke; it does not become a
  rubber-band selection.

**Why:** Freehand is inherently multi-stroke (you sketch, you don't
place one mark). Auto-reverting to Select after every stroke (the
pre-fix CF-3 behaviour, backlog
`2026-07-20-freehand-auto-revert-drawover-noop`) made the second
draw-over drag silently select instead of draw, with no feedback.
This is the Preview-style sticky-draw behaviour. Since DRAWING-TOOL
PARITY (2026-07-20) it is the default for the bounded shape tools too
(UAT-ANN-131 / UAT-ANN-134), not just Freehand.

---

### UAT-ANN-133 — Bounded drawing tools draw-first over an existing shape

**Preconditions:** A document open. For each bounded tool
(Rectangle / Ellipse / Line / Arrow) the test arms the tool and draws
one shape first.

**Rationale:** DRAWING-TOOL PARITY (owner ruling "parity",
2026-07-20; decision record
[`2026-07-20-drawing-tool-parity.md`](../decision-records/2026-07-20-drawing-tool-parity.md)).
Per-tool coverage of the draw-first-on-press behaviour that UAT-ANN-128
pins for Rectangle — extended here to Ellipse, Line, and Arrow.

**Steps (per tool):**
1. Arm the tool via the markup toolbar; drag to draw one shape.
2. Press-drag STARTING on top of that shape and drag off to the side.

**Expected (per tool):**
- A NEW shape of the same type is created (store count +1 per drag).
- The first shape is neither selected nor moved (its bounds are
  unchanged; `selectedAnnotationId` stays 0).

---

### UAT-ANN-134 — Bounded drawing tools are sticky (per tool, via toolbar)

**Preconditions:** Markup toolbar visible.

**Rationale:** DRAWING-TOOL PARITY (owner ruling "parity",
2026-07-20; decision record
[`2026-07-20-drawing-tool-parity.md`](../decision-records/2026-07-20-drawing-tool-parity.md)).
Per-tool coverage of the sticky-draw behaviour that UAT-ANN-131 pins
for Rectangle — extended here to Ellipse, Line, and Arrow. Must be
driven through the toolbar, since the sticky/revert decision lives in
`MainWindow::onAnnotationCommitted`, which reads the toolbar's tool
(arming on the overlay bypasses it; see UAT-ANN-132).

**Steps (per tool):**
1. Arm the tool via the markup toolbar; drag to draw one shape; release.
2. Observe the toolbar's checked tool. Drag again on empty space.

**Expected (per tool):**
- After the first shape the toolbar AND overlay both STAY on the tool.
- The second drag draws a second shape of the same type; it does not
  become a rubber-band selection (`selectedAnnotationId` stays 0).

---

## Known gaps

### UAT-ANN-100 — Annotation z-order control (Known gap)

**Preconditions:** Two overlapping annotations.
**Steps:**
1. Look for "Bring to Front" / "Send to Back" controls.
**Expected (future):** context menu or Inspector buttons for z-order.
**Current:** annotations render in creation order; no reordering UI.

### UAT-ANN-101 — Zoom Lens parameters in Inspector (Known gap)

The Zoom Lens annotation supports a zoom factor in its data model, but
the Inspector does not yet expose a control to edit it. Confirm that
the default (2×) is what newly created Zoom Lens annotations use.
