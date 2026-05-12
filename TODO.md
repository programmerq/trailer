# Trailer — Deferred Work

Items noted during development that aren't blocking the current phase.
Should be picked up before shipping or when the surrounding area is
worked on.

## 2026-04-30 HITL pass (live use on macOS)

Captured from the user driving the actual app on a Mac. Each entry is
a discrete change; we'll knock them out in priority order. Crossed-off
items have landed; the commit hash is in the strikethrough line.

> **2026-05-11 audit:** most of this section is already in the code.
> Verified-done items are struck through with the reference; the only
> remaining bullets are #1 (Dock-drop — needs runtime repro on
> macOS), #16 (sidebar TOC / Highlights & Notes — placeholders,
> blocked on underlying features), and #18 (search-match yellow —
> needs a custom highlight overlay over `QPdfView`).

### Bugs (data loss / broken affordance)

1. **Drag a file onto the Dock icon doesn't open it.** macOS sends a
   `QFileOpenEvent` which `Application::event` already routes to
   `openFiles`, but apparently nothing happens. Probably a
   single-instance / argv-routing issue or the event handler runs
   before the registry is wired. Repro and fix. The event handler
   looks correct in code (`Application::event` calls `openFiles` on
   `QEvent::FileOpen`); the bug may have been a launch-timing race
   that the `8bd9ad0` macOS no-window pass cleaned up. **Needs
   live macOS verification before crossing off.**
2. ~~**`Cmd-Tab` while dragging a Zoom Lens leaves an undo-less
   annotation.**~~ Done — `AnnotationOverlay` ctor wires
   `applicationStateChanged` to `abortInFlightDrag`, which clears
   `m_dragging` / `m_movingSelected` / `m_resizingHandle` /
   `m_inkPoints` when the app goes inactive.
3. ~~**Click-drag draws a red rectangle even with the Select tool
   active.**~~ Done — `4b74f80` ("HITL round 1: Select-mode
   preview, defaults, Inspector wiring, Dock open"). `paintEvent`
   now guards on `m_tool != Select && m_tool != None` before
   drawing the shape preview.
4. ~~**Some PDF thumbnails render without paper-white in dark
   mode.**~~ Done — `PdfDocument::renderThumbnail` composites the
   rendered page over an opaque white canvas before returning.

### UX defaults

5. ~~**Default tool should be Select, not box-drawing.**~~ Done —
   `MarkupToolbar` initialises with `selectAction->setChecked(true)`
   and stores `m_tool = AnnotationTool::Select`. Tied to #3 which
   also landed.
6. ~~**Sidebar starts hidden by default.**~~ Done — `Sidebar` ships
   `Mode::Hidden` as the default, `MainWindow` calls
   `m_sidebar->hide()` on construction, and the main-toolbar
   Sidebar picker shows "Hide Sidebar" checked at launch.
7. ~~**Foreground color shouldn't default to red.**~~ Done —
   `AnnotationStyle::stroke` defaults to `QColor(60, 60, 60)`
   (dark grey).
8. ~~**Inspector shortcut → ⌘I**~~ Done — wired to
   `QKeySequence(tr("Ctrl+I"))` in `buildViewMenu`.
9. ~~**macOS: launching with no files opens no window.**~~ Done
   upstream in `8bd9ad0` ("Add macOS no-window file menu actions").
10. ~~**Esc / Cmd-Tab / loss-of-focus deactivates Magnifier.**~~
    Done — `MainWindow::keyPressEvent` un-checks the magnifier
    action on Esc; `applicationStateChanged` un-checks it on app
    deactivate.

### Inspector + selection

11. ~~**Clicking on a placed annotation should NOT auto-show the
    Inspector.**~~ Done — `onAnnotationSelectionChanged` only
    updates the Inspector's data layer; the comment explicitly
    notes "DO NOT pop the pane open just because the user clicked."

### Markup bar

12. ~~**Replace text labels with SVG icons** appropriate to each
    tool.~~ Done — 29 base + 6 filled SVG icons under
    `resources/icons/actions/`, themed via `IconHelper` (commits
    `ff8541a`, `0d7ea5b`, `fae4ccd`). See `docs/icon-guidelines.md`
    for the family + artist brief, and `tools/generate_icon_sheet`
    for a visual review.

### Top-bar redesign

13. ~~**Add a slim main toolbar**~~ Done — `buildMainToolbar` in
    `MainWindow` builds the bar with sidebar-mode picker, zoom
    (out / actual / in), rotate (L / R), markup-toggle, form-
    toggle, and a search field anchored to the right. Markup and
    form toolbars are mutually exclusive and locked
    non-movable / non-floatable (commit `ff8541a`).
    (Title-bar filename pulldown is platform-default chrome on
    macOS and needs no special handling.)
14. ~~**Window menu on macOS**~~ Done — `buildWindowMenu` adds
    Minimize, Bring All to Front, and a dynamic per-window list
    refreshed via `refreshWindowMenuList`.
15. ~~**Go menu**~~ Done — `buildGoMenu` mirrors Preview's First /
    Previous / Next / Last Page entries with the documented
    shortcuts.

### Sidebar modes

16. **Sidebar has explicit modes** (Hide is default). Hidden /
    Thumbnails / Search Results all work. Table of Contents and
    Highlights & Notes are deliberate disabled placeholders in the
    picker — the underlying features (PDF outline parsing,
    text-aware highlights) haven't shipped. Re-enable each entry
    when its data source is in place.
17. ~~**Some PDF thumbnails missing paper-white**~~ — duplicate of
    #4 above; both fixed.

### Search

18. **Search highlights matches in highlighter yellow.** Still open.
    `QPdfView` paints its own selection rectangles in the system
    accent colour and the API doesn't expose a tint hook. Options:
    layer a custom overlay over the view that reads from
    `QPdfSearchModel` and paints siblings at low-opacity yellow,
    or subclass `QPdfView` to intercept its draw call.
19. ~~**"Match X of Y" indicator**~~ Done — `SearchBar::
    setMatchCounter` is wired through every find / next / previous
    handler in `MainWindow`.
20. ~~**Search opens the thumbnail sidebar**~~ Done —
    `Sidebar::Mode::SearchResults` populates from query matches and
    the sidebar auto-switches to it on a non-empty query.

### Misc

21. ~~**`Tools → Reset Trailer Settings…`**~~ Done — see
    `onResetTrailerSettings` in `MainWindow`; wipes Settings,
    RecentFiles, CardStore, signature PNGs, and model cache after
    explicit confirmation.
22. ~~**`File → Export as PDF`** for image documents.~~ Done —
    "PDF document (*.pdf)" is one of the file-type filters in
    `MainWindow::onExportAs`.

## UI

- **Menu organisation review.** Some items currently under Tools may
  belong under File (Export As, Take Screenshot) or Edit (Flip, Rotate,
  Adjust Size, Adjust Colour). Revisit once Phase 4 markup actions land,
  so we can organise them as a group.

## Cross-cutting

- **HiDPI / Retina support — partially done.** Qt 6 enables high-
  DPI scaling by default (`AA_EnableHighDpiScaling` /
  `AA_UseHighDpiPixmaps` are deprecated and effectively always-on),
  so the widgets layer is correct out of the box. The remaining
  gaps were in custom-rendered raster content that asked the
  document for logical-pixel sized images and let Qt scale them up
  blurry on 2x displays.
  - **Sidebar thumbnails** (commit pending) now render at
    `m_size * devicePixelRatio` native pixels and stamp
    `setDevicePixelRatio` on the result so Qt uses the high-DPI
    bitmap at logical layout size. No more soft thumbnails on
    Retina.
  - **Screenshot capture** (`screen->grabWindow(0)`) is already
    DPR-correct: the returned pixmap is native pixels and saving
    to PNG writes the high-resolution data.
  - **Magnifier overlay** is DPR-correct because both the source
    grab and the magnifier widget share the same screen DPR; Qt
    handles the native-to-native sample.
  - **SignatureCanvas render** already emits at 2× canvas
    resolution.
  - **Mixed-DPR multi-monitor** (window dragged between a 1x and a
    2x display mid-session) is *not* handled: thumbnail cache
    won't refresh on screen change. Edge case; deferred.
  - **Test on 1x, 2x, 3x displays** — manual; CI is offscreen
    only.

- **PDF undo/redo — rotate done, others scoped.** A
  `PdfCommand` interface lives in `src/document/PdfCommands.h`,
  paired with the first concrete command (`RotatePageCommand`).
  `PdfDocument` keeps undo / redo stacks of `PdfCommand`s
  parallel to the existing AnnotationStore undo log; the unified
  `IDocument::undo` / `redo` route to the most-recently-touched
  stack via an `m_lastUndoSource` heuristic.

  Remaining qpdf mutations to wire up the same way:
  - **DeletePagesCommand** — needs to capture
    `QPDFObjectHandle` references before the delete so the
    revert path can re-insert them at the original positions.
    qpdf retains the underlying objects until garbage-collected
    on save, so retained handles stay valid through the
    in-memory session.
  - **MovePageCommand** — trivial inverse: move(to, from).
  - **InsertPagesCommand** — capture insert index + count;
    revert deletes that range.
  - **CropPageCommand** — capture original `/CropBox`; revert
    re-sets it.

  Better-but-bigger follow-up: merge the AnnotationStore log
  and the PdfCommand stack into one chronological undo list so
  multi-action undo always pops the most recent thing the user
  did, regardless of which subsystem produced it. The
  `m_lastUndoSource` heuristic gets it right for the common
  one-action-back case but not for interleaved sequences.

- ~~**PDF save / export to a worker thread.**~~ Done — split into
  `PdfDocument::saveBeginQpdfPhase` (worker-thread qpdf phase) and
  `saveCommitOnUi` (UI-thread file rename + `QPdfDocument` reload).
  `MainWindow::saveDocumentAsync` orchestrates via
  `QFutureWatcher` with a `QProgressDialog`. Image saves stay
  synchronous because `QImage::save` is already fast and wrapping
  it in QtConcurrent::run only adds latency.

## Annotations

- **PDF annotation persistence — DONE for the data, /AP partial.**
  The TODO entry that claimed "shapes/text/notes drawn over a PDF live
  only in memory" was stale: `PdfEditor::writeAnnotations` /
  `readAnnotations` round-trip 10 annotation subtypes (Rectangle,
  Ellipse, Line, Arrow, Ink, Text, Note, HighlightShape, SpeechBubble,
  Highlight, Underline, StrikeOut). Signature flattens into the
  content stream; Redaction destroys content; ZoomLens has no
  standard PDF subtype and is intentionally skipped.

  **/AP appearance streams** are now emitted for **Rectangle,
  HighlightShape, Ellipse, Line, Arrow, and Ink**. Each shape's
  builder lives next to its property-only writer in
  `PdfEditor.cpp`. The remaining types (FreeText for Text /
  SpeechBubble, /QuadPoints-based Highlight / Underline / StrikeOut)
  still rely on the property-based fallback. FreeText needs a font
  resource in /Resources and a `BT` text block — non-trivial.
  Highlight / Underline / StrikeOut typically render fine without
  /AP because their /QuadPoints carry the geometry; viewers
  reconstruct them from properties more reliably than they do for
  shape annotations.

- **PDF multi-page / continuous-mode overlay.** The annotation overlay
  uses the `pageNavigator()->currentPage()` for its page reference and
  centres it in the viewport. In `Continuous` mode that means annotations
  only draw correctly on whatever QPdfView reports as the current page;
  coordinates can drift on other visible pages. Revisit with a per-page
  overlay or a view-geometry query that Qt PDF does not yet expose.

## Screenshot

- **Region / window / app pickers on Linux and Windows.** macOS uses
  `screencapture -i`; Linux falls back to gnome-screenshot if available;
  Windows currently only supports full-screen. Native region-select
  overlays would be a follow-up.

## UX polish pass (2026-04-24 HITL review)

A human-in-the-loop walkthrough surfaced a batch of paper-cuts the
automated UATs don't catch. These are grouped by theme so each can be
picked up as a coherent increment rather than one sprawling "fix UX"
task. When tackling any of these, land a UAT alongside it so the
regression doesn't return the next time the surrounding code is
rearranged.

> **2026-05-11 audit:** most of this section is also already done.
> Verified items struck through with a one-line implementation
> pointer. Remaining: image-batch thumbnail-bar (partial), contextual
> tool gating (partial — disabled vs hidden), signature-placement
> popover (still a modal dialog), Trim My Card.

### Window / document model

- **Window-per-file is now the default** (2026-04-24 pass). `open_files_in`
  defaults to `new_window`; `openFiles({a, b, c})` spawns three windows.
  `DocumentView` sets `tabBarAutoHide` so single-doc windows show no tab
  strip. Tabs remain available as an opt-in via `open_files_in = "new_tab"`.
- **Image-batch consolidation is partial.** `Application::openFiles`
  now detects "all paths are images" and routes them into one window
  so they share the QTabWidget tab strip — the user can flip through
  them without arranging multiple frames. But the original ask was
  "use the thumbnail bar for moving around," and the multi-document
  ThumbnailModel mode is still TODO:
  - `ThumbnailModel` is currently 1:1 with one `IDocument`. A
    multi-doc mode would let the sidebar render "1 thumbnail per
    document" instead of "1 thumbnail per page of one document."
  - A window-level "document list" panel in `Sidebar` could replace
    the tab strip for navigation. (Tabs work, the user feels the
    strip is too clicky for a 5-photo batch.)
- Revisit whether `MainWindow`'s `QTabWidget` central widget should be
  replaced with a plain `QStackedWidget` once the tab codepath has no
  remaining users. The `QTabWidget` + `tabBarAutoHide` pair is fine for
  now but carries tab-specific API no longer reached in the default
  flow.

### PDF text selection + text-aware markup (bug, high priority)

- ~~**Click-and-drag on a PDF with a text layer does not select
  text.**~~ Done. `PdfDocument::createView` wires
  `overlay->setTextSelectionProvider(...)` to
  `QPdfDocument::getSelection`, so Select-mode drags populate
  `m_pendingSelection` with text quads instead of drawing a shape.
  The "even when the markup toolbar is not visible" case is
  belt-and-suspenders covered by the new `visibilityChanged`
  handler in `MainWindow` that resets the toolbar to Select on
  hide (commit `ff8541a`).
- ~~**Underline and Highlight must be text-aware.**~~ Done — the
  `Highlight` / `Underline` / `StrikeOut` branch of
  `mouseReleaseEvent` calls `m_textSelection(start, end, page)` and
  stores the result in `Annotation::quads`. The annotation renderer
  walks the quads, not a single bounding rect, so the highlight
  follows wrapped text correctly.
- ~~**Contextual tool availability.**~~ Done — `MarkupToolbar::
  setToolVisible` hides (rather than disables) the text-aware
  trio on documents without a text layer, and hides the preceding
  separator when the whole group goes empty so the user doesn't
  see two adjacent dividers around nothing. Pinned by
  `test_markup_toolbar`.

### Annotation editing — selection, move, resize, restyle

- ~~**Annotations must be re-selectable after creation.**~~ Done.
  `AnnotationOverlay` has full `hitTest`, `m_selectedAnnotationId`,
  `ResizeHandle` corner-drag, body-drag-to-move, Delete /
  Backspace, arrow-key nudge, and an Inspector pane that tracks
  the selection. (See commits `3a9a5bc`, `d611d1b`, `0b8d274`.)
- ~~**Markup toolbar default visibility.**~~ Done — on first
  annotatable document `onCurrentDocumentChanged` auto-shows the
  markup toolbar (per-doc one-shot; an explicit user-hide is
  sticky).

### Inline editing (no modal popups for things that live in the document)

- ~~**Text boxes edit in place.**~~ Done — commit `deb1a40`
  ("Capture annotation text inline instead of via modal
  QInputDialog"). `AnnotationOverlay::m_inlineEditor` is a
  `QPlainTextEdit` parented to the overlay that captures text on
  blur / Enter and cancels on Escape.
- **Signature placement uses a popover, not a dialog.** Still open.
  `MainWindow::onSignHere` calls `SignaturesDialog dialog(this);
  dialog.exec()`, a modal. Replace with a `QMenu` containing
  custom `QWidgetAction` entries (or a frameless `QDialog` anchored
  to the Sign Here tool button) so the picker pops down from the
  button instead of pushing a modal in front of the document.

### High-fidelity signature + freehand capture (hardware input)

- **Signature canvas — DONE for the 80% case.** The 2026-04-29 pass
  rebuilt `SignatureCanvas` to:
  - Bigger default canvas (640×200) so signatures get pixels to
    breathe and don't pixelate when stamped on a 300 DPI page.
  - Render at 2x scale into the saved PNG so the cached raster is
    sharp at output time.
  - Pressure source priority: `QTabletEvent::pressure()` for
    Wacom / Bamboo / Surface Pen, then `QPointerEvent::points()
    .first().pressure()` for Apple Force Touch trackpads (Cocoa
    backend in Qt 6.5+), then a 0.5 default for plain mice.
  - Cubic pressure → width curve (1..7 px) for visible dynamic
    range.
  - Per-stroke 3-point centred moving average smoothing on
    release, hides quantisation jitter from mouse / trackpad
    input. Tablet pens are already smooth so the pass is
    effectively a no-op for them.
  - Sub-event sampling via `QPointerEvent::points()` so fast
    strokes on a Force Touch trackpad don't lose intermediate
    samples to OS coalescing.
- **Vector storage as a sidecar.** Today the canvas stores strokes
  in memory but writes only the rasterised PNG. A
  `<id>.strokes.json` sidecar with `[stroke[(x,y,pressure)*]]*`
  would let future viewers re-render at any DPI without
  re-rasterising. Not blocking — the 2x raster is plenty for
  paper-size output.
- ~~**Pressure-aware Ink (Freehand) tool.**~~ Done — `Annotation`
  gained a `pressures` parallel vector, AnnotationOverlay overrides
  `tabletEvent` for stylus input and reads `QEventPoint::pressure`
  for Force Touch trackpads. The on-screen renderer draws each
  segment with its own pressure-derived width, and the saved /AP
  Form XObject does the same per-segment width emission so the
  rendered ink matches what the user drew. Caveat: PDF's standard
  /Ink subtype carries only x/y in `/InkList`; cross-app
  round-trip preserves the polyline shape but loses pressure
  unless we add a Trailer-specific extension key (deferred).
- **Apple Pencil / iOS** — out of scope. Trailer is Qt6 widgets,
  desktop only. If a tablet build ever happens, QTouchEvent on
  iPad surfaces Apple Pencil pressure / tilt and the existing
  pressure pipeline could pick it up.

### AcroForm fields — direct manipulation, not auto-magic

The 2026-04-28 follow-up reframes this whole area: the user's actual
need is "I see the field, I click it, I type." AutoFill (My Card →
field-name matcher) is a gimmick dead end — even when it works it's
solving a problem nobody asked us to solve, and any per-PDF tuning
to make the matcher catch a specific form's quirks is wasted effort.
The 2026-04-24 AutoFill fixes (commits a6ad259, c66091a area) still
stand because they removed user-hostile behaviour (the modal popup,
the silent stale overlay), but no further investment in the matcher
is warranted.

What to do instead:

- ~~**Form widgets visible by default on any fillable PDF.**~~ Done.
  `onCurrentDocumentChanged` auto-toggles `m_fillFormsAction` on the
  first time it sees a fillable document (one-shot per-doc; an
  explicit user-off is sticky).
- ~~**A subtle visual cue for fillable regions.**~~ Done — `FormOverlay`
  applies `border: 1px solid rgba(0, 100, 200, 80)` to every field
  with `rgba(0, 100, 200, 220)` on focus. Matches the Preview
  convention.
- ~~**Tab navigates between fields in document order.**~~ Done —
  `FormOverlay` sorts fields by reading order (page asc, top-y asc,
  left-x asc) and chains them with `QWidget::setTabOrder`.
- ~~**Demote AutoFill in the menu.**~~ Done — `Tools → Forms →`
  submenu hosts `Fill Forms` and `AutoFill from My Card` as siblings
  inside the submenu rather than peers at the top level.
- **Trim My Card.** Still open. The 2026-04-24 review called it
  "huge with tons of options." With AutoFill deprioritised, the card
  is just the signature-block source plus a few address bits. Audit
  the `MyCardDialog` fields hard.

### Test-shape principle: synthesise variety, don't pin examples

Captured 2026-04-28 after a near-miss with a real-world fixture.
Pattern to avoid: writing a UAT whose floor is "this specific PDF
the reviewer dropped on us yields N fills." Even gated on an env
var that keeps the file out of the repo, the approach over-fits the
test to one example — a regression in the matcher only trips the
test if it happens to break THIS form, and a successful matcher
change is hard to land because the floor was tuned to one corpus.

Use generative fixtures instead:

- A `writeRandomFormPdf(seed, recipe)` helper in the test harness
  that emits a synthetic PDF with N text fields whose `/T` and `/TU`
  are sampled from a corpus of real-world quirks (cryptic indices,
  hierarchical dot-names, suffixed `_2/_3` duplicates, designer
  typos, multi-purpose tooltip labels, etc.).
- Assertions stated as **invariants**, not counts: "for every field
  whose canonicalised name contains `email`, the recognized email
  value is written" / "for every field whose canonicalised name
  contains nothing recognisable, the value is empty after AutoFill."
  Run across many seeds.
- Keep `uat_af_010..030` (small, named cases — they document the
  intended behaviour) but resist adding `uat_af_NNN_specific_form`
  slots. If you find yourself wanting to, that's a signal the
  generator needs a new quirk in its corpus.

The same principle applies to OCR (don't pin to a single sample
image), background removal (don't pin to a single subject photo),
and any future feature where input variety is the whole point.

### Cross-cutting polish items

- **Designer / non-technical-user review.** The items above came out of
  one ~15-minute walkthrough. A focused pass that watches a real user
  drive the app end-to-end (open a file → markup → sign → save) will
  surface more of these subtle behaviours. Schedule this before any 1.0
  polish milestone. Watch for:
  - Any modal dialog that interrupts work on the document.
  - Tools that appear enabled but do nothing (or the wrong thing) for
    the active document type.
  - Any action that requires the user to already know where to look
    (hidden toolbars, menu-only entry points for common tasks).
  - Loss of direct manipulation (things the user made but can't then
    grab, move, or edit).
  - Feedback that's too loud (popups) or too quiet (no visible change
    after a successful action).
