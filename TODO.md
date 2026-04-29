# Trailer — Deferred Work

Items noted during development that aren't blocking the current phase.
Should be picked up before shipping or when the surrounding area is
worked on.

## UI

- **Menu organisation review.** Some items currently under Tools may
  belong under File (Export As, Take Screenshot) or Edit (Flip, Rotate,
  Adjust Size, Adjust Colour). Revisit once Phase 4 markup actions land,
  so we can organise them as a group.

## Cross-cutting

- **HiDPI / Retina support.** The app does not yet handle device-pixel
  ratio > 1. Symptoms: screenshots capture logical pixels (not native),
  and raster content may render soft on 2x displays. Needs:
  - `Qt::AA_EnableHighDpiScaling` / `Qt::AA_UseHighDpiPixmaps` audit.
  - `QScreen::devicePixelRatio()` propagated to all grab/paint paths
    (screenshot, thumbnail render, image scaling).
  - Test on 1x, 2x, 3x displays.

- **PDF undo/redo.** Image edits have an undo stack; PDF edits do not
  (cloning `QPDF` per mutation is expensive). Design options:
  - Command pattern: record inverse of each operation (undo rotate =
    rotate opposite direction; undo delete = re-insert).
  - Snapshot-based with copy-on-write tricks in qpdf if possible.

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

  **/AP appearance streams** are now emitted for **Rectangle and
  HighlightShape** so Apple Preview renders them correctly without
  reconstructing from /C and /BS. The remaining types (Ellipse,
  Line, Arrow, Ink, FreeText, Highlight/Underline/StrikeOut quads)
  still rely on the reader's property-based fallback. Each
  follows the same `buildSquareAppearance` pattern with a
  type-specific content stream — incremental work, not a
  blocker for files Trailer itself reads back perfectly.

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

- **Click-and-drag on a PDF with a text layer does not select text.**
  Instead it starts drawing a rectangle in the last-used markup colour,
  even when the markup toolbar is not visible. This is a regression from
  whatever layer intercepts pointer events; probably the annotation
  overlay is eating them unconditionally. The fix should:
  - Default to QPdfView's native text selection when no markup tool is
    active.
  - Only swallow pointer events when a markup / shape tool is explicitly
    selected.
- **Underline and Highlight must be text-aware.** Today they are just
  box-drawing tools that discard the non-edge parts of the rectangle.
  They should use the PDF's text-layer hit-testing to select characters
  / words / lines, the same primitive used for copy. Depends on the
  fix above.
- **Contextual tool availability.** For an image with no detected text,
  hide text-centric markup (Underline, Highlight, Strikethrough). Keep
  Redact available on images (it is inherently pixel-region based).
  Re-expose Underline / Highlight on images once OCR results exist for
  the page.

### Annotation editing — selection, move, resize, restyle

- **Annotations must be re-selectable after creation.** Currently the
  only recourse is Undo. A shape (box, bubble, arrow, freehand stroke,
  text) once placed can't be picked up to move, resize, recolour, or
  delete. Needed:
  - Hit-testing in `AnnotationOverlay` for existing primitives.
  - Selection handles (drag, resize, rotate where sensible).
  - An Inspector panel or contextual toolbar for properties (colour,
    stroke width, fill, font).
  - Keyboard: Delete/Backspace removes the selected annotation; arrow
    keys nudge it.
- **Markup toolbar default visibility.** When the active document can
  receive edits (any PDF, any image), show the markup toolbar without
  requiring a menu toggle. The toolbar becomes the discoverability
  surface for the tools above.

### Inline editing (no modal popups for things that live in the document)

- **Text boxes edit in place.** Creating a text-box annotation currently
  opens a modal dialog to capture the text. It should drop an editable
  text element into the overlay and focus it — typing commits on blur
  or Enter, Escape cancels. The dialog is a context-breaker.
- **Signature placement uses a popover, not a dialog.** The signature
  capture flow today is a modal dialog. Qt's `QMenu` with a custom
  widget as a `QWidgetAction`, or a frameless `QDialog` anchored to the
  signature-tool button, can stand in for the Apple popdown. This also
  makes the thing feel like part of the document rather than a separate
  mode.

### High-fidelity signature + freehand capture (hardware input)

- **Stop rasterising the signature to a tiny bitmap.** Store signatures
  as vector strokes (polyline + per-sample pressure + timestamp) and
  rasterise only at paint time for the target DPI. The canvas in the
  capture surface should be as large as the popover allows, not a
  shrunken swatch.
- **Use hardware pressure + unaccelerated position where available.**
  Qt exposes tablet input via `QTabletEvent` (pressure, tilt, tangential
  pressure) and touch via `QTouchEvent` with per-point pressure on
  supported hardware. We want:
  - Physical / device coordinates (no OS pointer acceleration), which
    tablet events deliver natively; for mouse fall back to the current
    behaviour.
  - Pressure recorded per sample, mapped to stroke width.
  - Graceful fall-back to mouse — same code path, constant pressure.
- **Apply the same to the freehand drawing tool.** Pressure-varying
  stroke width when the user is drawing with a stylus or pressure-aware
  trackpad.

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

- **Form widgets visible by default on any fillable PDF.** Today the
  form overlay is hidden until the user toggles `Tools → Fill Forms`.
  That hides discoverability. When `doc->supportsFormFilling()` is
  true on document load, turn the overlay on automatically. Keep the
  menu item so users who want the cleaner read-only view can flip it
  off. This single change is closer to what the user described than
  any matcher refinement.
- **A subtle visual cue for fillable regions.** Even with the overlay
  active, blank `QLineEdit`s blend into a white page. A faint hover
  or always-on outline on the widget rect (one-pixel light blue, say,
  matching macOS Preview's convention) makes the affordance obvious.
- **Tab navigates between fields in document order.** Verify this
  works; if not, wire it up.
- **Demote AutoFill in the menu.** It's still there for users who
  want it, but it shouldn't be a peer of `Fill Forms` in the Tools
  menu. Move it under `Tools → Forms →` as a sub-action, or behind a
  preferences toggle.
- **Trim My Card.** The 2026-04-24 review called it "huge with tons
  of options." If AutoFill is deprioritised, the card is just the
  signature-block source plus a few address bits. Audit hard.

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
