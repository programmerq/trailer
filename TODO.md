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

## Annotations

- **PDF annotation persistence.** Shapes/text/notes drawn over a PDF live
  only in memory — saving the document does not write them to the file.
  Phase 4 increment 9 will serialise them as `/Annot` objects via qpdf.

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
- **Still to do: thumbnail-bar handles multi-item sets.** When the user
  opens several single-page items together (e.g. a batch of images),
  the nicer UX is one window whose sidebar thumbnail bar navigates
  between them, not N independent windows. Needs:
  - `ThumbnailModel` gains a multi-document mode (today it wraps one
    `IDocument` and renders `pageCount()` rows).
  - `openFiles()` detects the "batch of single-page items" case and
    routes all of them into one window.
  - A window-level "document list" panel in `Sidebar` that switches
    the active `IDocument` the way tabs used to.
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

### AutoFill + AcroForm (bug + UX)

- **AutoFill does not actually fill a PDF the test user tried.** Even
  after populating the contact card, a PDF with genuine fillable fields
  did not receive values. Needs investigation — probably a field-name
  matching bug in the AutoFill mapper, or the FormOverlay is not being
  re-synced from the underlying AcroForm after the card write.
  Repro case: save the specific PDF the reviewer used into
  `tests/data/` and drive it in a new UAT.
- **AutoFill confirmation popup must go.** The "Filled N of M fields"
  dialog after saving the My Card is a context-breaker. The card dialog
  itself is "saved on Enter", so no confirmation is needed. If we want
  to surface how many fields actually got filled, put a subtle status
  line in the form toolbar, not a modal.
- **My Card dialog feels "huge with tons of options".** Audit which
  fields are genuinely useful for AutoFill (name, address lines,
  city / state / ZIP, country, phone, email, signature) vs. fields we
  speculatively added. Trim hard, and consider a progressive-disclosure
  section for the long tail.

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
