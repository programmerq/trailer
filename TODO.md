# Trailer — Deferred Work

Items noted during development that aren't blocking the current phase.
Should be picked up before shipping or when the surrounding area is
worked on.

Three recurring sources feed this file:

- **HITL passes** — the maintainer driving the actual app and writing
  down what annoyed them. Captures power-user friction. Dated
  subsections below (e.g. *2026-04-30 HITL pass*, *2026-05-19 HITL
  pass*) are the precedent format.
- **Reference-user smoke sessions** — a non-maintainer opening a
  fresh build and performing three small tasks while a note-taker
  records observations. Captures fresh-eyes friction, which tends to
  point at different bugs than the HITL passes do. Protocol lives in
  [`docs/smoke-session.md`](docs/smoke-session.md); findings land
  here under a dated `## YYYY-MM-DD smoke session` subsection in the
  same shape as the HITL entries.
- **Multi-perspective audits** — read-only sweeps by reviewer-chair
  agents (privacy, accessibility, security, etc.) that surface
  structural gaps the live use doesn't expose. See
  [`docs/audit-2026-05-19.md`](docs/audit-2026-05-19.md) for the
  current snapshot.

## 2026-05-20 HITL pass (post-#25, on `main`)

Captured from a live walkthrough after PR #25 (Post-0.1.0 docs +
release tooling + PDF page-op undo) landed. Each item is described in
terms of the user-visible behaviour and the proposed fix.

> **Note on the "in flight" branch:** at the start of this HITL pass,
> [ROADMAP.md](ROADMAP.md)'s `## In flight (about to land)` section
> still described the Wave 1-4 work as upcoming, even though PR #24
> (`4dba247 HITL waves 1-4`) had already merged the work onto `main`
> on 2026-05-19. The roadmap was authored *after* the merge but not
> updated to reflect it. Items below that look like Wave 1 territory
> (thumbnail density, search-bar collapse, markup-toolbar defaults)
> are scoped against what actually landed in PR #24 and what's still
> rough about it. The roadmap was reframed in the same PR as this
> entry (`969d46f docs(roadmap): reframe after PR #24 landing +
> post-#25 HITL`).

### Thumbnail sidebar still wastes vertical space (Workstream C partial)

The 9beff07 / Workstream C work *did* land in PR #24: the logical
thumbnail size dropped from `128×160` → `80×100`
([src/ui/ThumbnailModel.h:47](src/ui/ThumbnailModel.h:47)), a custom
`ThumbnailDelegate` paints the page number as an in-pixmap badge in
the lower-right corner
([src/ui/Sidebar.cpp:94-121](src/ui/Sidebar.cpp:94)), and its
`sizeHint` returns `iconSize.height() + 2*kThumbVerticalPadding`
(= 108 px) per item
([src/ui/Sidebar.cpp:61-66](src/ui/Sidebar.cpp:61)). On paper this
should make each row tight to the thumbnail.

In live use the rows still render visibly taller than the thumbnail —
the user observed wide vertical gaps between thumbnails on a typical
sidebar width, with each row roughly the *pre*-9beff07 height despite
the new sizing constants. Something between the delegate's `sizeHint`
and the actual `QListView` layout is keeping rows tall.

**Repro hints:** instrument `ThumbnailDelegate::sizeHint` to log what
it returns vs what `m_view->visualRect(index).height()` reports back;
check whether `Qt::SizeHintRole` is being shadowed by another role
elsewhere; verify on both 1× and 2× displays in case DPR-rounding in
`KeepAspectRatio` is leaving slack the delegate doesn't reclaim. If
sizeHint *is* returning 108 and Qt is honouring it, the gap is visual
noise (DPR rounding, scrollbar reservation) that needs a delegate-
level fix.

### Rectangle annotation rough edges

A live session placing one rectangle surfaced three problems. All
three have landed.

- ~~**Auto-switch to Select after placement.**~~ Done. MainWindow
  now connects `AnnotationOverlay::annotationCommitted` to a slot
  that flips the markup toolbar back to Select; the toolbar's
  `activeToolChanged` propagates back to the overlay so toolbar UI
  and overlay state stay in sync. Pinned by UAT-ANN-131. Sticky-
  draw mode (keep the active tool checked after each commit)
  remains an open opt-in setting if anyone misses chained shape
  drawing.

- ~~**Existing shapes are not restyleable from the Inspector.**~~
  Done — same root cause as the "rectangles disappear" item
  below. Both symptoms (colour-doesn't-change AND
  rectangle-vanishes) were the dangling-pointer corruption playing
  out in one of two ways depending on what the freed memory
  happened to contain when the modal returned.

- ~~**Rectangles disappear without explicit deletion.**~~ Done.
  Root cause: `Inspector::Inspector` (lines 125-138 / 144-158)
  held a raw `const Annotation*` from `AnnotationStore::find(id)`
  across `QColorDialog::getColor`, which is modal and spins the
  Qt event loop. Any store mutation that fired during the dialog
  (auto-save flush, queued `AnnotationStore::changed` slot, undo
  coalescing, etc.) could reallocate the underlying
  `std::vector<Annotation>` and dangle the pointer. After the
  modal returned, the dereference read garbage geometry/style and
  `m_store->update(*stale_ptr)` wrote that garbage back to the
  same id — which is why the rectangle "vanished" (off-page or
  zero-size bounds) AND the colour change appeared to be lost.
  Fix: snapshot the initial colour before the modal and re-fetch
  the annotation by id after the dialog returns; mirror the same
  shape in the Fill handler. Pinned by UAT-ANN-130.

### Select All only selects annotations

`Edit → Select All`
([src/ui/MainWindow.cpp:640-644](src/ui/MainWindow.cpp:640)) routes
to `AnnotationOverlay::selectAll()`, which multi-selects annotations
on the current page. There's no path to "select the visible page
raster so I can copy/paste it into a chat program." A common
end-of-session flow is: mark up a page → select everything → copy →
paste image into Slack / iMessage / etc.

Proposal: keep `Cmd-A` as annotation-select when the overlay has
focus *and* the page has annotations; otherwise (image documents,
or a PDF page with no annotations and the page itself focused) treat
`Cmd-A` as "select page for copy" and let `Cmd-C` push a rendered
PNG of the page to the clipboard. A separate
`Edit → Copy Page as Image` menu item may be clearer than overloading
`Cmd-A`.

### Content-aware initial UI defaults

Wave 1 / Workstream I (per-file + per-type + per-window persistence)
landed in PR #24 — that's the right floor. But on *first* open of a
document, when there's no saved state for that file, we can do
better than the global default by reading the document's contents:

- **Lots of fillable AcroForm fields → start with form controls
  shown.** The 2026-04-24 pass already auto-shows the form toolbar
  on fillable PDFs, but the sidebar / Inspector / markup-toolbar
  defaults still come from the global / per-type state. A document
  with ≥ N AcroForm widgets (suggest ≥ 3) is unambiguously a form;
  show the form toolbar, suppress the markup toolbar, and consider
  defaulting the sidebar to hidden.

- **Many pages → start with thumbnail sidebar open.** A document
  with ≥ K pages (suggest ≥ 20) is one the user will want to
  navigate by thumbnail; auto-popping
  `Sidebar::Mode::Pages` on first open saves a click. Once
  the user changes it, three-tier persistence carries forward.

Heuristics should run only when no per-file state is on record; any
explicit user adjustment wins and sticks.

### Navigation shortcuts

- ~~**No keyboard shortcut for page-mode (single / two / continuous).**~~
  Mostly done. `Cmd-1` → Continuous and `Cmd-2` → Single Page are live;
  `Cmd-3` is only *reserved* for Two Pages — that action stays disabled
  for now, so the shortcut does nothing yet. This required moving the
  zoom commands off the digit row (they clashed with the original
  `Cmd-1`/`Cmd-3` proposal): Actual Size is now `Cmd-0`, Fit Page
  `Cmd-9`. **Finding:** Qt's `QPdfView::PageMode` exposes only
  `SinglePage` / `MultiPage`, no facing/two-up layout, so
  `ViewMode::TwoPages` currently aliases `Continuous`. A real
  side-by-side view needs a custom widget; tracked as a larger
  follow-up. `Cmd-3` becomes functional when that lands.

- **Continuous-mode `↓` step is too small.** With pages laid out
  vertically, `↓` advances by the default
  `QAbstractScrollArea` line-step, so reaching the next page on a
  long doc takes dozens to hundreds of presses. Proposal: in
  continuous mode, `↑` / `↓` step by approximately viewport height
  (matching Preview / Acrobat); `PageDown` / `PageUp` should also
  work. Single-page mode already advances by full page on arrows
  via the existing nav wiring.

### ~~Search "Close" button is non-functional~~ Done (`51e59e2`)

Fixed exactly as diagnosed below: `hideSearchBar` / `showSearchBar`
now drive `setVisible()` on the captured `m_searchBarAction`
(the `QWidgetAction` from `addWidget`), so the toolbar slot collapses
instead of leaving an empty gap. Esc and the X button share the path.

The `SearchBar` close button is wired:
[src/ui/SearchBar.cpp:43](src/ui/SearchBar.cpp:43) emits `dismissed`
on click → [src/ui/MainWindow.cpp:145](src/ui/MainWindow.cpp:145)
routes `dismissed` to `hideSearchBar` →
[src/ui/MainWindow.cpp:702](src/ui/MainWindow.cpp:702) calls
`m_searchBar->setVisible(false)`. Yet clicking close does not
collapse the bar in the running app.

Likely cause: the search bar is added to the main toolbar via
`QToolBar::addWidget`
([src/ui/MainWindow.cpp:606](src/ui/MainWindow.cpp:606)), which
wraps the widget in an internally-owned `QWidgetAction`. Hiding
the inner widget without also hiding the wrapping `QAction` leaves
the toolbar slot occupied (visible empty space rather than
collapsed). **Fix:** capture the `QAction *` returned by
`m_mainToolbar->addWidget(m_searchBar)` and call `setVisible(false)`
on the action in `hideSearchBar`, paired with `setVisible(true)`
in the show path. Esc routes through the same `dismissed` signal,
so this likely affects Esc-dismiss too.

## 2026-05-19 HITL pass (live use on Windows 11; applies cross-platform)

Driven by the user opening a real document on Windows and walking through
the friction points. Landed as PR #24 in four waves; the items listed
here are explicit scope deferrals from that pass — features that the PR
addressed in a "good enough for now" form with a known better follow-up,
or behaviours the four agents flagged but didn't fix.

### Annotation handles (Workstream D follow-up)

- **Shape-aware handles for Line / Arrow.** Wave 2 shrank the corner
  handle hit-zone from 10×10 to 6×6 so the arrow body is reachable for
  drag-to-move. The real fix is endpoint-only handles for the line-like
  shapes: `handleAt()` should branch on `Annotation::type` and return
  two endpoint zones for Line/Arrow + the existing four corner zones
  for everything else. `handleRect()` for non-bbox handles needs to
  stop relying on `viewBounds.topLeft()`/`bottomRight()`.

### OCR (Workstream F follow-up)

- **Embed OCR text on PDF export.** When the user exports an image to
  PDF (`MainWindow::onExportAs` → PDF filter) or runs an OCR-required
  export on a raster-text PDF, the recognised `TextBlock`s should be
  embedded as an invisible PDF text layer so the exported file is
  searchable in other PDF readers. The `SelectableTextStore` already
  holds the geometry and text — wire it through `QPdfWriter`.
- **Disk cache of OCR results.** In-memory only today; reopens re-OCR
  the same pages. Key by `(file-path-hash, page-content-hash)` and
  store under `AppPaths::cacheDir()`.
- **Word-level selection.** PP-OCRv3 emits per-region polygons;
  `SelectableTextLayer` snaps the selection to whole blocks. Splitting
  each block into word-bounded sub-rects would feel more natural
  matched against the displayed glyphs.
- **Auto-trigger model download from background OCR submissions.**
  Today only the explicit `Tools → Recognize Text…` dialog drives
  `ensureOcrModelsReady` (the consent + download prompt). The
  visible-page auto-pump silently no-ops when the model isn't on disk.
  Surfacing a one-time "Recognize text on this document?" prompt the
  first time auto-OCR would fire on a model-missing system would close
  the loop, but the UX has to be carefully not-popup-shaped.

### ML scheduler (Workstream J follow-up)

- ~~**Linux power detection.**~~ Done (`9b24eb4`).
  `src/platform/PowerSource.cpp` now scans `/sys/class/power_supply/*`:
  online "Mains" adapter → `OnAC`, offline → `OnBattery`, no AC supply
  (desktop / VM / container) → `OnAC` fallback.

### SAM (Workstream G follow-up)

- **Encoder cache eviction beyond simple LRU.** The 3-entry LRU is a
  conservative default; if users hit memory pressure with multiple
  large PDF pages cached, switch to a memory-budget-based policy or
  expose a settings.toml knob.

### Background removal (Workstream H follow-up)

- **Disk cache of candidate scores.** The CPU heuristic runs once per
  image-document open. Caching the result by file-path-hash would skip
  the recompute on reopen.

### General

- **Sidebar differential update.** Wave 2 debounced
  `AnnotationStore::changed` via 0-ms `singleShot`, which is the big
  win. A follow-up could turn `Sidebar::refreshAnnotations` into a
  proper add/remove/update diff against the `QListWidget` instead of a
  clear-and-rebuild; not worth the complexity until profiling shows it
  matters.
- **Inspector debounce.** Same pattern as Sidebar, lower priority — the
  Inspector's per-emit update is lighter (single annotation) so it
  doesn't dominate the undo-replay cost today.
- **Inspector tracking of restored chrome.** Workstream I restores
  per-file / per-type window geometry + dock state + markup-toolbar
  visibility; Inspector visibility isn't currently in that set. Adding
  it would round out the persistence story.

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

1. ~~**Drag a file onto the Dock icon doesn't open it.**~~ Done.
   In-process path pinned by `uat_fnd_050_fileOpenEventOpensWindow`
   (synthesises a `QFileOpenEvent` with no windows open and
   confirms a window is created). Live drag-onto-Dock confirmed
   on macOS by the user on 2026-05-13. The Info.plist's
   `CFBundleDocumentTypes` for `com.adobe.pdf` and `public.image`
   is the LaunchServices-side prerequisite.
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

16. ~~**Sidebar has explicit modes** (Hide is default).~~ Done as
    of the 2026-05-13 pass. Hidden / Thumbnails / Search Results /
    Table of Contents / Highlights & Notes all render. TOC reads
    `QPdfBookmarkModel` through an `OutlineProxyModel` that maps
    DisplayRole → Title; the picker entry is enabled per-doc by
    `hasOutline()`. Highlights & Notes filters
    `AnnotationStore::annotations()` to text-content types
    (Highlight / Underline / StrikeOut / Note / Text / SpeechBubble)
    and gates the picker entry on `Sidebar::highlightsAndNotesCount`,
    refreshed on every store mutation. Pinned by `uat_toc_010..012`
    and `uat_hn_010..012`.
17. ~~**Some PDF thumbnails missing paper-white**~~ — duplicate of
    #4 above; both fixed.

### Search

18. ~~**Search highlights matches in highlighter yellow.**~~ Done.
    `AnnotationOverlay` gained a `setSearchHighlights` API; the
    paint pass runs underneath user annotations and renders
    siblings at low-opacity yellow with the current match at high
    opacity plus a thin outline. `PdfDocument::refreshSearch
    Highlights` walks `QPdfSearchModel::resultAtIndex(i).
    rectangles()` and pushes the list on every search-model /
    current-index change. Pinned by `uat_vwr_064`.
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
  - ~~**Sidebar thumbnails.**~~ Done in `af6621c`
    ("Sidebar thumbnails render at native resolution on Retina").
    `ThumbnailModel` renders at `m_size * devicePixelRatio` native
    pixels and stamps `setDevicePixelRatio` on the result so Qt
    uses the high-DPI bitmap at logical layout size. No more soft
    thumbnails on Retina.
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

- **PDF undo/redo — done for the five top-level mutations.** A
  `PdfCommand` interface lives in `src/document/PdfCommands.h`,
  paired with concrete commands for every qpdf-level page op the
  user can trigger today: `RotatePageCommand`, `DeletePagesCommand`,
  `MovePageCommand`, `InsertPagesCommand`, and `CropPageCommand`
  (the last one batches an N-page crop into a single undoable
  action). `PdfDocument` keeps undo / redo stacks of `PdfCommand`s
  parallel to the AnnotationStore undo log.

  ~~Better-but-bigger follow-up: merge the AnnotationStore log
  and the PdfCommand stack into one chronological undo list.~~
  Done — `IDocument::undo` / `redo` now pop a per-document
  chronological log of typed entries (one per committed op), so
  multi-action undo always pops the most recent thing the user
  did, regardless of which subsystem produced it. `ImageDocument`
  uses the same structure for its pixel-snapshot stack. Bounded
  histories evict in lockstep with the log
  (`AnnotationStore::historyEvicted` for annotations; local sync
  in `ImageDocument::pushUndoSnapshot` for pixels), and the
  dispatch guards log/stack desync at runtime (warn + no-op, not
  a release-inert assert). The interim last-touched-stack
  heuristic this replaced is gone.

  Other small follow-ups in the same area:
  - Annotation re-indexing on delete/move/insert. The undo
    stacks don't currently coordinate with `AnnotationStore`'s
    `Annotation::page` field, so an annotation on page 3 of a
    4-page doc whose page 1 gets deleted still claims page 3
    even though the actual page-3 content is now at index 2.
    Save-time round-tripping papers over this for the simple
    case but a sidebar that filters by page can show stale
    results until the next reload. See `docs/uat/03-pdf-pages.md`
    UAT-PDF-070 / UAT-PDF-071.

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
> pointer. Remaining: image-batch thumbnail-bar (partial — single
> window works, multi-doc ThumbnailModel still TODO) and Trim My
> Card. Contextual tool gating and signature-placement popover
> were finished in the same audit pass.

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
- ~~**Signature placement uses a popover, not a dialog.**~~ Done.
  New `SignaturePicker::show` (in `src/ui/SignaturePicker.{h,cpp}`)
  pops a `QMenu` of thumbnails anchored under the Sign-Here button
  on the form toolbar. "Add Signature…" goes straight into the
  capture dialog and the new signature is auto-armed on save (no
  re-open-the-picker step). "Manage Signatures…" still opens the
  full `SignaturesDialog` for add/remove flows. `FormToolbar::
  signHereRequested` now carries the anchor position so the
  popover lands under the button the user clicked, not in the
  centre of the screen.

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

- **Designer / non-technical-user review.** Now codified as the
  reference-user smoke session — see
  [`docs/smoke-session.md`](docs/smoke-session.md) for the protocol
  (fresh build, non-maintainer observer, three open-do-close cycles
  on a text PDF + scanned PDF + photo, observations land in a dated
  subsection of this file). The original bullets that lived here —
  modal dialogs that interrupt, controls enabled-but-noop, hidden
  entry points, lost direct manipulation, too-loud/too-quiet feedback
  — are now covered as positive rules in PHILOSOPHY's *How Trailer
  reduces friction* section. The trigger remains: schedule a smoke
  session before any 1.0 polish milestone, and opportunistically
  whenever a willing non-maintainer is in the room.
