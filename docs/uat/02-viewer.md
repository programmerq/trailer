# UAT — Viewer

Covers opening documents, the PDF and image views, zoom, the tab bar,
thumbnails in the Sidebar, search, print, and the magnifier. Annotation
creation is in [05-annotations.md](05-annotations.md); page-editing
operations are in [03-pdf-pages.md](03-pdf-pages.md). Image edits are in
[04-image-editing.md](04-image-editing.md).

---

## File > Open

### UAT-VWR-001 — Open a PDF via the dialog

**Preconditions:** App launched.
**Steps:**
1. `File > Open…` (shortcut `Cmd+O` / `Ctrl+O`).
2. Select a valid multi-page PDF.
3. Click Open.
**Expected:**
- A new tab opens with the file's basename as its title.
- The PDF renders in the central area.
- Page 1 is visible at the top of the viewport.
- The Sidebar `Pages` tab shows thumbnails for every page.

### UAT-VWR-002 — Open a PNG via the dialog

**Preconditions:** App launched.
**Steps:**
1. `File > Open…`, select a PNG.
**Expected:**
- A tab opens showing the image centred in the viewport.
- The Sidebar `Pages` tab shows a single thumbnail (or a placeholder —
  see UAT-VWR-082).

### UAT-VWR-003 — Dialog filter

**Preconditions:** App launched.
**Steps:**
1. `File > Open…`.
2. Inspect the file-type filter at the bottom of the dialog.
**Expected:**
- **Current:** the filter offers only `All files (*)` — users pick any
  file and routing is performed by `DocumentRegistry` from the
  extension / MIME type. Expanding the dropdown to per-type filters
  (PDF, PNG, JPEG, TIFF, …) is a Known Gap tracked in TODO.md.
- Native dialog chrome (sidebar, path bar) is provided by the OS.

### UAT-VWR-004 — Cancelling the dialog

**Preconditions:** App launched.
**Steps:**
1. `File > Open…`.
2. Click Cancel.
**Expected:**
- The dialog closes.
- No new tab is opened.
- No file is added to `File > Open Recent`.

### UAT-VWR-005 — Opening multiple files at once

**Preconditions:** App launched.
**Steps:**
1. `File > Open…`.
2. Select three files with multi-select (Shift/Cmd/Ctrl click).
3. Click Open.
**Expected:**
- Three new tabs appear in order.
- All three files are added to `File > Open Recent`.

---

## Tab bar behaviour

### UAT-VWR-010 — Switch tabs with the mouse

**Preconditions:** Three tabs open.
**Steps:**
1. Click each tab in turn.
**Expected:**
- The clicked tab becomes active.
- The central area shows that document.
- The Sidebar thumbnails and Inspector content update to match.

### UAT-VWR-011 — Close a tab via its X button

**Preconditions:** Two tabs open.
**Steps:**
1. Click the X on one tab.
**Expected:**
- That tab closes.
- The remaining tab is active.
- **Current:** close is unconditional even if the tab is dirty — no
  confirmation prompt. Cross-ref UAT-FND-092 (Known gap) for the
  planned save/discard/cancel dialog.

### UAT-VWR-012 — Reorder tabs by dragging

**Preconditions:** Three tabs open in order A, B, C.
**Steps:**
1. Drag tab C to the left of tab A.
**Expected:**
- Tabs now appear in the order C, A, B.
- The active tab is whichever was active before the drag.

### UAT-VWR-013 — Tab tooltip shows full path

**Preconditions:** A tab is open for a file with a known path.
**Steps:**
1. Hover over the tab.
**Expected:**
- A tooltip appears showing the document's absolute path.

### UAT-VWR-014 — Dirty tab shows a prefix

**Preconditions:** An editable document is open and clean.
**Steps:**
1. Perform any edit (e.g. rotate a page).
**Expected:**
- The tab title gains a `• ` prefix.
2. Save the document.
**Expected:**
- The prefix disappears.

### UAT-VWR-015 — Closing the last tab

**Preconditions:** One tab open.
**Steps:**
1. Close it via X.
**Expected:**
- The tab area becomes empty.
- The window remains open.
- Document-dependent menu items become disabled.

---

## PDF view

### UAT-VWR-020 — Render quality

**Preconditions:** A multi-page PDF is open.
**Steps:**
1. Look at the first page.
**Expected:**
- Text is legible and properly kerned.
- Vector graphics have no obvious aliasing at 100% zoom.
- Images in the PDF are not corrupted or colour-shifted.

### UAT-VWR-021 — Continuous scroll mode (default)

**Preconditions:** Multi-page PDF is open; `View > Continuous` is the
active view mode.
**Steps:**
1. Scroll down with the mouse wheel or trackpad.
**Expected:**
- The viewport moves smoothly through consecutive pages.
- Page separators are visible between pages.
- The current page indicator (if shown) updates as the next page comes
  into view.

### UAT-VWR-022 — Single-page mode

**Preconditions:** Multi-page PDF is open.
**Steps:**
1. `View > Single Page`.
**Expected:**
- Only the current page is visible, centred in the viewport.
- Scrolling does not jump to the next page — `View > Next Page` or
  `Page Down` is required.

### UAT-VWR-023 — Previous / Next Page

**Preconditions:** Multi-page PDF is open.
**Steps:**
1. `View > Next Page` (or `Page Down`).
2. `View > Previous Page` (or `Page Up`).
**Expected:**
- First trigger advances one page.
- Second returns to the original page.
- Navigating past the last or before the first page is a no-op.

### UAT-VWR-024 — Two-Page mode (Superseded)

**Superseded by** the "Two-page (facing) layout" section below
(UAT-VWR-070..073 in that section, plus UAT-VWR-101/102) — the real
facing-page `TwoPageView` shipped in PR #113 (0a78b7c). This entry is kept,
not deleted, per this doc's append-only ID rule; it no longer describes
current behaviour. Two Pages is enabled for multi-page PDFs and renders a
real side-by-side spread, not a disabled placeholder.

### UAT-VWR-025 — Continuous-mode keyboard scroll steps by a screenful

**Preconditions:** A multi-page PDF is open with `View > Continuous`
active, and the document is tall enough to need scrolling.
**Steps:**
1. Click into the page view to give it keyboard focus.
2. Press `Down` (`↓`). Then `Space`. Then `Up` (`↑`).
3. Press `Page Down`, then `Page Up`.
**Expected:**
- Each `↓` scrolls down by roughly one viewport height — a screenful,
  matching Preview / Acrobat — not the tiny line-step
  `QAbstractScrollArea` uses by default (which would take dozens to
  hundreds of presses to cross a single page). `Space` does the same
  as `↓`.
- `↑` scrolls back up by the same screenful.
- `Page Down` / `Page Up` are unchanged: they jump to the next /
  previous page (the Next Page / Previous Page shortcuts), not a
  screenful scroll.
- Scrolling stops cleanly at the top and bottom of the document (no
  wrap, no overscroll). Single-page mode is unaffected — it still
  advances a whole page per arrow press (UAT-VWR-023).

The automated harness slot `uat_vwr_025_continuousArrowStepsByViewport`
in `tests/uat/test_uat_viewer.cpp` pins only the `Down` / `Space` / `Up`
screenful stepping. `Page Down` / `Page Up` remaining the Next /
Previous Page shortcuts is verified at the MainWindow / manual level: a
view-only harness can't distinguish the arrow keys falling through from
the base class applying its own screenful handling.

---

## Image view

### UAT-VWR-030 — Static image display

**Preconditions:** A PNG is open.
**Steps:**
1. Look at the tab.
**Expected:**
- The image is centred in the scroll area.
- If it exceeds the viewport, scrollbars appear.
- The Inspector `Document` tab shows the image's pixel dimensions.

### UAT-VWR-031 — Animated GIF plays on open

**Preconditions:** An animated GIF (≥2 frames) is open.
**Steps:**
1. Watch the view.
**Expected:**
- Frames cycle at their native speed.
- An Animation Bar appears at the bottom of the view with play/pause,
  frame slider, and frame counter.
- The frame counter updates as frames advance.

### UAT-VWR-032 — Animation Bar play/pause

**Preconditions:** Animated image is open and playing.
**Steps:**
1. Click pause.
2. Click play.
**Expected:**
- Pause halts the animation; the frame counter freezes.
- Play resumes playback from the paused frame.

### UAT-VWR-033 — Animation Bar scrubbing

**Preconditions:** Animated image is open.
**Steps:**
1. Drag the frame slider to an arbitrary position.
**Expected:**
- The visible frame jumps to match the slider position.
- The frame counter updates accordingly.
- If the animation was playing, it pauses while scrubbing.

### UAT-VWR-034 — Animation Bar hides for static images

**Preconditions:** A static image (1 frame) is open.
**Steps:**
1. Look for the Animation Bar.
**Expected:**
- The Animation Bar is not shown.

---

## Zoom

Fresh documents open at **fit-to-content** — the page (PDF) or image
is scaled to fit the viewport, capped at 100%. Documents that already
fit at 100% are shown at actual size rather than upscaling. Use
`Actual Size` (Ctrl+1 / Cmd+1) to override.

### UAT-VWR-040 — Zoom In

**Preconditions:** A PDF or image is open at 100% zoom.
**Steps:**
1. `View > Zoom In`. Two shortcuts are bound: the platform
   `QKeySequence::ZoomIn` default (typically `Cmd++` on macOS,
   `Ctrl++` on Windows/Linux) and the explicit `Ctrl+=` /
   `Cmd+=` (for the common case of `+` requiring Shift on US
   layouts). Try both.
**Expected:**
- The rendered content grows. Successive triggers continue to grow up
  to an internal ceiling (roughly 32× for images — above that the
  trigger is a no-op rather than an error).

### UAT-VWR-041 — Zoom Out

**Preconditions:** A PDF or image is open at 100% zoom.
**Steps:**
1. `View > Zoom Out` (shortcut `Cmd+-` / `Ctrl+-`).
**Expected:**
- The rendered content shrinks. Hits a floor (roughly 5%) and stops.

### UAT-VWR-042 — Actual Size

**Preconditions:** Document is zoomed in or out from 100%.
**Steps:**
1. `View > Actual Size` (shortcut `Ctrl+0` / `Cmd+0`).
**Expected:**
- Zoom snaps to exactly 100%.
- Menu state updates.

### UAT-VWR-043 — Fit to Width

**Preconditions:** Document is open; window is resized.
**Steps:**
1. `View > Fit to Width` (shortcut `Ctrl+1` / `Cmd+1`).
**Expected:**
- The document resizes so its full width is visible in the viewport.
- Resizing the window after this may or may not refit — clarify during
  testing.

### UAT-VWR-044 — Zoom disabled for animated images

**Preconditions:** An animated GIF is open.
**Steps:**
1. Try `View > Zoom In`.
**Expected:**
- The action is disabled, or triggering it is a no-op.
- The animation continues to play.

---

## Sidebar — Pages tab

### UAT-VWR-050 — Pages tab shows thumbnails for every page

**Preconditions:** A multi-page PDF (≥3 pages) is open.
**Steps:**
1. Switch the Sidebar to the `Pages` tab.
**Expected:**
- One thumbnail per page, in order.
- Each thumbnail is labelled with its page number.
- The active page is visually indicated (e.g. highlighted border).

### UAT-VWR-051 — Click a thumbnail to navigate

**Preconditions:** Multi-page PDF is open.
**Steps:**
1. Click a thumbnail other than the current page.
**Expected:**
- The main view scrolls / jumps to that page.
- The thumbnail becomes the highlighted one.

### UAT-VWR-052 — Thumbnails scale with page count

**Preconditions:** A large PDF (≥20 pages).
**Steps:**
1. Scroll the Sidebar.
**Expected:**
- All thumbnails are reachable by scrolling.
- Initial scroll position corresponds to the current page.
- The app remains responsive while scrolling (no frozen UI).

### UAT-VWR-053 — Image document shows one thumbnail

**Preconditions:** A PNG is open.
**Steps:**
1. Switch to the `Pages` tab.
**Expected:**
- Exactly one thumbnail is shown, representing the image.

### UAT-VWR-054 — Animated image placeholder

**Preconditions:** An animated GIF is open.
**Steps:**
1. Switch to the `Pages` tab.
**Expected:**
- A single placeholder entry is shown (rendering animated thumbnails is
  a known gap).
- The app does not crash.

### UAT-VWR-055 — Content-aware first-open sidebar default

**Preconditions:** A document the app has never seen before, with no
saved per-file view state on record.
**Steps:**
1. Open a long PDF (≥ 20 pages).
2. Separately, open a form PDF (≥ 3 fillable AcroForm fields) of fewer
   than 20 pages.
**Expected:**
- The long document opens with the `Pages` (thumbnail) sidebar showing,
  ready to navigate — even if the per-type "last PDF" default had the
  sidebar hidden.
- The form opens with the sidebar **hidden** for a clean filling view
  (the form-filling toolbar surfaces separately), even if the per-type
  default had the sidebar open.
- A long form (both ≥ 20 pages and ≥ 3 fields) is treated as long:
  thumbnails win, because paging through it is the priority.
- These heuristics run only on first open with no saved per-file state;
  any explicit choice the user has made for this exact file is restored
  as-is and always wins. Thresholds: ≥ 20 pages, ≥ 3 fields.

Pinned by `tests/test_content_aware_defaults.cpp` (the decision matrix),
plus integration slots `uat_pdf_080_longDocOpensThumbnailSidebar`
(`tests/uat/test_uat_pdf_pages.cpp`) and
`uat_frm_060_formForcesSidebarHiddenOverridingTypeDefault`
(`tests/uat/test_uat_forms.cpp`).

---

## Search (Edit > Find)

### UAT-VWR-060 — Open the search bar

**Preconditions:** A PDF with text is open.
**Steps:**
1. `Edit > Find…` (shortcut `Cmd+F` / `Ctrl+F`), or click the
   magnifying-glass icon on the right of the main toolbar.
**Expected:**
- The search button on the main toolbar expands inline into the full
  search field with arrows and X button.
- Focus is in the search field.
- An X button is visible for dismissal.
- Dismissing the bar (X or Escape) collapses it back to the icon button.

### UAT-VWR-061 — Find matches in a PDF

**Preconditions:** Search bar open. The PDF contains a known word.
**Steps:**
1. Type the known word.
**Expected:**
- Matches are highlighted in the view.
- The current match is visually distinguished.
- A count of matches is shown.

### UAT-VWR-062 — Find Next / Find Previous

**Preconditions:** Search bar open with matches.
**Steps:**
1. Click Next (or `Return`, or `Edit > Find Next`).
2. Click Previous.
**Expected:**
- Next advances to the next match and scrolls it into view.
- Previous returns. Wraps at the end / beginning of the document.

### UAT-VWR-063 — Clear search via Escape

**Preconditions:** Search bar open, matches highlighted.
**Steps:**
1. Press Escape while the field has focus.
**Expected:**
- The search bar collapses back to its icon button on the main toolbar.
- Highlights are cleared.

### UAT-VWR-064 — Clear search via X button

**Preconditions:** Search bar open.
**Steps:**
1. Click the X.
**Expected:**
- Search bar collapses back to its icon button; highlights cleared.

### UAT-VWR-065 — Search with no matches

**Preconditions:** Search bar open. Type a string that doesn't appear.
**Steps:**
1. Type the string.
**Expected:**
- No matches are highlighted.
- The search bar indicates "0 matches" or equivalent.
- Find Next / Find Previous are no-ops.

### UAT-VWR-067 — Search shows an "X of Y" match counter

**Preconditions:** A PDF containing a keyword that occurs N times is open.
**Steps:**
1. `Edit > Find…` and type the keyword.
**Expected:**
- A counter appears in the search bar reading "<current> of <N>" once
  matches are found, so the user knows how many hits there are and
  where they are among them.

### UAT-VWR-066 — Search on image documents (Known gap)

**Preconditions:** An image is open.
**Steps:**
1. `Edit > Find…`.
**Expected:**
- Either the action is disabled (current behaviour if a doc doesn't
  support search), or the bar opens but finds nothing. The app must not
  crash. (OCR-based image search is deferred to Phase 6.)

### UAT-VWR-068 — Search seeds the first match at or after the current page

**Preconditions:** A multi-page PDF whose keyword appears on several
distinct, known pages is open. (ADR 0006, accepted — Option B.)
**Steps:**
1. Navigate to a middle page `k`.
2. `Edit > Find…` and type the keyword.
**Expected:**
- The initially-selected match is the **first** match whose page is
  `>= k` (position-aware seed, `>=` not `>`, so a match sitting on page
  `k` itself is the seed).
- If the viewport is past the last match's page, the seed **wraps** to
  the first match in the document (index 0).
- The seed lands correctly on the asynchronous large-doc populate path,
  not just the synchronous/cached one — the final seed after the model
  fully populates is the at/after-page match, never a provisional
  earlier-page seed frozen mid-stream.

### UAT-VWR-069 — Position-aware seed preserves whole-document coverage

**Preconditions:** As UAT-VWR-068, seeded from a middle page.
**Steps:**
1. Seed the search from a middle page (seed lands on a later page).
2. Walk backward with Find Previous.
**Expected:**
- The match total `Y` still spans the whole document — matches before
  the current page are still present and counted, not filtered out.
- Find Previous from the seed reaches the earlier-page matches, so
  coverage is unchanged; only the initial seed index moved.

### UAT-VWR-103 — Search seed scrolls the viewport to the match, not just the model

**Preconditions:** As UAT-VWR-068 (matches on distinct, known pages).
**Steps:**
1. Navigate to a middle page `k` with no match of its own.
2. `Edit > Find…` and type the keyword.
**Expected:**
- Beyond UAT-VWR-068 (which pins the search MODEL's recorded seed page),
  the document's own current page — what the sidebar highlights and what
  the visible page surface actually paints — also lands on the seeded
  match's page. A match selected in the model but not scrolled into view
  is functionally invisible and reads as "search doesn't do anything" —
  the real dogfooding report ("it starts at match 1 being highlighted,
  even though I'm on page 161").

### UAT-VWR-104 — Find Next / Find Previous scroll the viewport to the match

**Preconditions:** A multi-page PDF with matches on several distinct pages
is open; Find is seeded on the first match.
**Steps:**
1. Press Enter (Find Next) repeatedly.
2. Press Shift+Enter (Find Previous) repeatedly.
**Expected:**
- Each press that advances to a match on a DIFFERENT page moves the
  document's current page there — not just the highlighted index/counter.
  Real dogfooding report: "When I hit enter, it selects the next match,
  but doesn't jump me to the next match."

### UAT-VWR-105 — Shift+Enter in the search field triggers Find Previous

**Preconditions:** The search bar has focus with a query that has matches.
**Steps:**
1. Press Shift+Enter.
**Expected:**
- Find Previous fires (the current match index moves backward), NOT Find
  Next. A plain Enter right after still fires Find Next — the fix does not
  disturb the unmodified case. Real dogfooding report: "When I do
  shift+enter when doing search, that should do 'previous match' instead
  of next match."

---

## Print

### UAT-VWR-070 — Print PDF

**Preconditions:** A PDF is open.
**Steps:**
1. `File > Print…` (shortcut `Cmd+P` / `Ctrl+P`).
**Expected:**
- The system print dialog appears.
- It lists installed printers.
- Accepting submits the PDF to the printer.

### UAT-VWR-071 — Print image

**Preconditions:** A static image is open.
**Steps:**
1. `File > Print…`.
**Expected:**
- Print dialog appears.
- Accepting sends the image to the printer, scaled to fit the page
  while preserving aspect ratio.

### UAT-VWR-072 — Print is disabled for invalid/animated/stub docs

**Preconditions:** Open the stub adapter (an `.xyz` file) or an animated
GIF with no frames decoded.
**Steps:**
1. Check the Print menu item state.
**Expected:**
- `File > Print…` is disabled.

### UAT-VWR-073 — Cancel print dialog

**Preconditions:** Print dialog open.
**Steps:**
1. Click Cancel.
**Expected:**
- Dialog closes. No job is submitted. App state is unchanged.

---

## Document open (off the GUI thread)

### UAT-VWR-100 — Open shows a "Loading…" placeholder, then the page

The residual `QPdfDocument::load` runs on a worker thread (backlog
`2026-07-15-offthread-pdf-open-placeholder`, deferred (b) of decision record
0006): the open's file reads never run on the GUI thread. Until the load
settles, the document view is an honest "Loading…" placeholder (G3: it is not
a lying control — it offers no action that cannot act); when the load
completes the placeholder is replaced in place by the real page view.

**Preconditions:** A PDF whose initial open is still in flight.
**Steps:**
1. Build the document view while the open has not yet settled.
2. Let the background open complete.
**Expected:**
- Before the open settles: the view shows a centered "Loading…" label — not a
  blank pane and not fake content.
- After the open settles: the label is gone and the real page view is shown.
- No document-open file read is performed on the GUI thread
  (guarded structurally by `tests/test_perf_gui_thread_io.cpp`).

---

## Magnifier

### UAT-VWR-080 — Toggle Magnifier

**Preconditions:** A document is open.
**Steps:**
1. `View > Magnifier` (shortcut `` ` ``, backtick).
**Expected:**
- A magnifier overlay appears and follows the cursor.
- The menu item shows a check mark.
- Moving the mouse over content shows a zoomed view around the cursor.

### UAT-VWR-081 — Magnifier toggles off

**Preconditions:** Magnifier active.
**Steps:**
1. Press `` ` `` again (or `View > Magnifier` again).
**Expected:**
- Overlay disappears. Menu check mark clears.

### UAT-VWR-082 — Magnifier with no document

**Preconditions:** App open, no document.
**Steps:**
1. Press `` ` ``.
**Expected:**
- Either disabled, or the magnifier toggles but shows nothing (empty
  viewport). Must not crash.

### UAT-VWR-083 — Escape deactivates the Magnifier

**Preconditions:** A document is open and the Magnifier is active.
**Steps:**
1. Press `Esc`.
**Expected:**
- The magnifier overlay disappears and the `View > Magnifier` menu
  item's check mark clears. Esc is the escape hatch for the sticky
  lens mode; Cmd-Tab / app-deactivate clears it the same way.

## Two-page (facing) layout

Ratified in `docs/decision-records/2026-07-21-two-page-layout.md`. The first
increment (PR1) ships a custom continuous facing-spread view (`TwoPageView`)
used only in Two-Pages mode; QPdfView keeps driving Single/Continuous. Markup,
text-selection, and search are honestly degraded (disabled-with-tooltip) in
Two-Pages mode; parity is the committed PR2 follow-up. Harness:
`tests/uat/test_uat_two_page.cpp`.

### UAT-VWR-070 — "Two Pages" toggle enablement (G3)

**Preconditions:** App open.
**Steps:**
1. Open a multi-page PDF; inspect `View > Two Pages`.
2. Open a single-page PDF; inspect `View > Two Pages`.
3. Open an image; inspect `View > Two Pages`.
**Expected:**
- Multi-page PDF: enabled.
- Single-page PDF: disabled with tooltip "This document has only one page."
- Image: disabled with tooltip "Images don't have pages to face."
- No enabled-but-inert toggle, no popup-that-says-no.

### UAT-VWR-071 — Cover-alone facing spreads

**Preconditions:** A multi-page (≥5) PDF is open.
**Steps:**
1. `View > Two Pages` (⌘3 / Ctrl+3).
**Expected:**
- Two pages render side by side in a continuous vertical spread scroll.
- Cover-alone-ON (default): page 1 alone, then (2,3),(4,5)…; a trailing
  unpaired page renders alone.
- The spread canvas is wider than a single page and scrolls vertically
  through the stacked spreads.

### UAT-VWR-072 — Per-page Actual Size + truthful zoom (dpr matrix)

**Preconditions:** A multi-page PDF is open in Two-Pages mode.
**Steps:**
1. `View > Actual Size` (⌘0).
**Expected:**
- Each page renders at 1 PDF point → 1 logical pixel (not spread-fits-window).
- The zoom-% readout is the true per-page zoom (100% here), meaning the same
  physical page size as Single/Continuous at 100%.
- Layout holds crisply at devicePixelRatio 1, 1.5, and 2 (pts × zoom × dpr
  device pixels), verified by running this slot across the dpr matrix.

### UAT-VWR-073 — Honest degradation in Two-Pages mode (D2-A, G3)

**Preconditions:** A multi-page PDF is open.
**Steps:**
1. Switch to Two-Pages mode.
2. Inspect the markup toggle and Find.
3. Switch back to Continuous.
**Expected:**
- In Two-Pages mode, markup and search (Find) are disabled with a tooltip
  that points back: "Switch to Single Page or Continuous to use this in a
  facing-page view."
- Leaving Two-Pages restores markup and search to their normal enabled state.

### UAT-VWR-101 — Switching view modes preserves the current page

**Preconditions:** A multi-page PDF is open, scrolled/paged to a page far
from the start (real dogfooding report: page 161 of a 365-page PDF).
**Steps:**
1. From Single Page mode, `View > Continuous` (Cmd-1).
2. From Continuous, `View > Single Page` (Cmd-2).
3. From Single Page, `View > Two Pages` (Cmd-3).
4. From Two Pages, `View > Continuous` (Cmd-1).
**Expected:**
- After every switch, the current-page indicator (sidebar highlight,
  `currentPage()`) and the actual visible surface agree — both report the
  SAME page the user was on before the switch. Neither resets to page 1.
- This was a real bug: the model (and sidebar) kept reporting the old page
  while the QPdfView surface silently reset to page 1 on a mode switch — a
  lying-UI disagreement between the model and the view. The view now
  follows the model, established as the source of truth.

### UAT-VWR-102 — Entering Two-Pages mode re-fits a Fit-Page zoom to the spread

**Preconditions:** A multi-page PDF is open in Single Page mode with
`View > Fit Page` (Cmd-9) active — a real dogfooding report's exact state.
**Steps:**
1. `View > Two Pages` (Cmd-3).
**Expected:**
- The facing spread fits the viewport width; no horizontal scrollbar is
  populated by a page spilling off the document area.
- The re-fit zoom is smaller than the single-page Fit-Page zoom it would
  otherwise have inherited unchanged — proof the spread was actually
  refit for two pages, not left at a number computed for one.
- The zoom-% readout still matches what's actually painted (truthful
  across the mode switch, not just when a zoom action is used directly —
  UAT-VWR-072/079 cover the latter).
- A literal Custom/Actual zoom (a % the user explicitly chose) is
  unaffected — it still carries over unchanged (UAT-VWR-072).

---

## Known gaps

### UAT-VWR-090 — Tab detach to new window (Known gap)

See UAT-FND-090.

### UAT-VWR-091 — Image OCR / text search (Known gap)

See UAT-VWR-066. OCR lands in Phase 6.

### UAT-VWR-092 — HiDPI rendering (Known gap)

**Preconditions:** Running on a 2× or 3× display (Retina etc.).
**Steps:**
1. Open a PDF and zoom to 100%.
2. Inspect render quality.
**Expected (future):** crisp pixel-perfect rendering at the native
device-pixel ratio.
**Current:** content may appear soft; thumbnails and screenshots may
capture logical (not native) pixels. Cross-ref TODO.md.
