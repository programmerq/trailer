# Trailer — Design Document

> A cross-platform document and image workbench. Open it, look at it, mark it
> up, sign it, export it. No accounts. No cloud. No telemetry. Just the file
> in front of you.

**Status:** Phases 0–5 shipped; Phase 6 in flight (ML core landed, format/colour-management half unstarted). See [AGENTS.md](AGENTS.md) §*Phase status* for the live state and [TODO.md](TODO.md) for the work queue.
**Target platforms:** Windows 10/11 (x64, ARM64), macOS 12+ (Apple Silicon shipped, x86_64 deferred — see [docs/packaging-macos.md](docs/packaging-macos.md)), Linux (x64, ARM64; X11 and Wayland).
**License:** MIT — see [LICENSE](LICENSE) and the constraints in [PHILOSOPHY.md](PHILOSOPHY.md).

---

## 1. Vision

Trailer is a single desktop application that handles the long tail of "I just
want to look at this file, do one thing to it, and move on." Most desktop
operating systems split this across half a dozen tools: one for PDFs, one for
images, one for scans, one for screenshots, one for signatures, one for cropping.
Trailer collapses that into one app that opens almost anything visual, lets you
do reasonable edits inline, and gets out of your way.

Trailer is local-first by construction. It does not sign you in, sync your
files, or call home. Anything that would require a network round-trip during
ordinary use is either out of scope or behind an explicit, off-by-default
toggle.

### 1.1 Name

A *trailer* previews what's coming up. The name is also a deliberate nod to the
prior art on macOS, while staking out distinct ground (cross-platform, no
account system, no cloud).

---

## 2. Goals and non-goals

### 2.1 Goals

1. **One app, many file types.** PDFs and the dozen-or-so common image formats
   are first-class. 3D scene formats (USD, Collada) are a stretch goal.
2. **Inline edits, not a full editor.** Trailer is to image editors what a
   notepad is to a word processor: enough to fix the file in front of you,
   not enough to replace dedicated tools.
3. **Cross-platform parity.** A Linux user and a Windows user should see the
   same feature set. Platform-native conventions (menu placement, file dialogs,
   keyboard modifiers) are respected, but no feature is gated by OS.
4. **Local-first and offline-capable.** Every core feature works with the
   network unplugged.
5. **Keyboard-driven.** Every common action has a discoverable, learnable
   shortcut.
6. **Predictable file ownership.** Edits land on disk where the user expects.
   Auto-save and version history are local, transparent, and reversible.

### 2.2 Non-goals

1. **Cloud sync of any kind.** No "your signatures everywhere," no document
   sync, no shared drives. Files live where the user puts them.
2. **Account systems.** No login, no profile, no telemetry.
3. **AI assistants for writing.** OCR yes (it's a deterministic transformation
   the user asked for); generative rewriting/summarizing no.
4. **Real-time collaboration.** Single-user workflow. No simultaneous editing.
5. **Replacing Photoshop / Affinity / GIMP / Krita.** Trailer is for quick
   edits; full raster editors exist for the rest.
6. **Replacing Acrobat for forensic PDF work.** Redaction is supported but
   should be considered "good enough for ordinary use," not a defense-grade
   tool.
7. **Mobile.** Desktop only.
8. **Browser extension or web version.** Native only.

---

## 2.3 Design philosophy for ongoing development

These principles apply to every phase and every feature. Contributors,
reviewers, and AI coding agents should treat them as standing constraints,
not aspirational notes.

**The document is the UI.**
Every piece of application chrome — toolbar, sidebar, status bar, panel — exists
entirely in service of the document in front of the user. When in doubt, remove
UI rather than add it. The document should feel like it *fills the room*, not
like it is sitting inside an application container.

**Progressive disclosure.**
The full feature set (OCR, background removal, annotations, signatures, colour
management) must never overwhelm a first-time user who just wants to read a PDF.
The app should boot into a state that a technically unsophisticated user can
navigate within five seconds. Advanced features reveal themselves at the moment
of need — not up front.

**Forgiveness and reversibility.**
Destructive operations (delete page, crop, flatten, redact) must require
explicit confirmation. Undo must work across all edit types. Auto-save snapshots
exist locally and are transparent. Users should never be afraid of the app.

**Zero jargon in the default surface.**
Terms such as "colour space," "ICC profile," "DPI," "rasterize," and "flatten
annotations" must not appear in menus or status bars visible to default users.
These terms belong in the Advanced settings pane or developer documentation.
Default-mode language should read like: *Save a copy*, *Print quality*,
*Sharpen*.

**Platform conventions are non-negotiable.**
- macOS: `⌘W` closes a tab (not the window); the menu bar lives at the top of
  the screen; files open into existing windows by default.
- Windows: Alt-underlined keyboard accelerators in menus; title-bar controls on
  the right; standard common-dialog patterns.
- Linux: XDG-portal dialogs preferred; respect the active desktop environment's
  window-management idioms.

These conventions must be enforced as part of code review, not left to
individual contributor judgment.

**Keyboard-first, pointer-friendly.**
Every action has a keyboard shortcut that is discoverable from the menu.
Pointer targets meet a minimum of 44 × 44 pt (following platform HIG
recommendations). Scroll, zoom, and pan are instant and smooth — jank at this
layer destroys trust more than almost anything else.

**Measured, not decorative.**
Animations exist only to communicate state changes (panel opening, progress
completion, drag-and-drop confirmation). No gratuitous transitions. Maximum
duration: 150–200 ms. Prefer easing that decelerates (ease-out) to give a
sense of physical weight without feeling sluggish. The "Reduce motion"
setting (§6.12) must suppress all non-essential animation.

---

## 2.4 Designing for real, non-technical end users

### 2.4.1 The developer and the user are not the same person

The developer (or AI coding agent) is fluent in the codebase, knows what the
app is *supposed* to do, and interprets ambiguous UI charitably. A real
non-technical end user does not. They will:

- Click where the developer never expected.
- Read error messages literally and completely.
- Abandon a flow at the first moment of uncertainty.
- Blame themselves before blaming the software.

This asymmetry is permanent and must be designed around, not explained away.

AI coding agents are subject to the same limitation in a different direction:
they can generate and evaluate code fluently but cannot simulate the reaction of
a 58-year-old retired teacher trying to redact a name from a PDF for the first
time. Neither developer intuition nor agent output is a substitute for exposure
to real users.

### 2.4.2 Practical guidelines

**Write the walk-through before building the feature.**
Before implementation starts, write down the steps a person who has never seen
the app would take to accomplish the goal. If step 3 requires knowing what
step 1 did, the design has failed. Refactor the design until the steps are
self-evident.

**Error messages must answer: "What do I do now?"**
Not "what went wrong technically." The message "Could not open file" is
incomplete. "Could not open file — the file may be damaged or in an unsupported
format. Try exporting it from its original application first." is actionable.

**Empty states must be welcoming and actionable.**
No recent files, no search results, failed-to-open — each of these states must
contain a clear, friendly suggestion. An icon and silence is not sufficient.

**First-run experience has exactly one job.**
The app should open with an unambiguous prompt: *Open a file* with a visible
drag-target and a file-picker button. Nothing more. All preferences, settings,
and advanced features are one deliberate navigation away, never in the way.

**Every preference that can be mis-set should have a visible reset.**
If a non-technical user can accidentally place a setting in a bad state, a
"Reset to default" control should be adjacent. The privacy wipe in settings
is a good model: one deliberate action, clearly labelled.

**Don't trust developer intuition alone.**
Informal hallway tests — "here is the app, try to crop this image" with someone
who is not an engineer — routinely surface friction points that are completely
invisible from inside the codebase. Encourage these tests at each phase
milestone.

---

## 2.5 UX friction testing and personas

### 2.5.1 The gap between agents and users

No current LLM or automation agent navigates a native desktop GUI with the
fluency of a real human user. Playwright/Selenium-style agents work for web
apps but are awkward for Qt widgets. Vision-capable models can *review* a
screenshot and identify layout problems but cannot yet fluidly interact with
the app to discover flow friction under realistic conditions.

This gap is a known and temporary limitation. The infrastructure to support
richer agent-based UX testing should be designed *now*, even though the
execution model is not yet mature.

### 2.5.2 Persona-based critique (available today)

A vision-capable LLM can be given a screenshot of any UI state plus a persona
prompt and asked to narrate what a user with that persona would do next, what
is confusing, and what they are afraid to click. This produces actionable
feedback with zero additional tooling investment.

**Defined personas for Trailer:**

| Persona | Description | Key anxieties |
|---|---|---|
| **The office non-technical user** | Works with PDFs daily (invoices, contracts, forms) but has no image-editing background. Uses a Windows laptop. | "Will this break my file?" "Where did my changes go?" "I don't understand this error." |
| **The older careful user** | Retired or semi-retired; cautious; double-checks everything; prefers explicit Save over auto-save. | "I need to know when it saves." "Small text is hard to read." "Too many options." |
| **The power migrator** | Moving from macOS Preview or Adobe Acrobat Reader; has strong muscle memory; frustrated by missing keyboard shortcuts or changed terminology. | "Where is the equivalent of [feature]?" "Why is this in a different menu?" |
| **The occasional user** | Opens the app once every few weeks to do one specific thing (sign a form, crop a screenshot). Has forgotten everything from last time. | "What does this button do again?" "I don't remember how I did this last time." |

### 2.5.3 Process: screenshot audit at each milestone

Before any phase milestone is declared complete:

1. Take a screenshot (or short screen recording) of every distinct UI state
   introduced by that phase: the default empty state, a document open, the
   new feature in use, any dialog or confirmation prompt, and any error state
   reachable through normal use.
2. Run each screenshot through at least the *office non-technical user* and
   *older careful user* persona critique prompts.
3. File issues for any friction point that would cause a user in that persona
   to hesitate, mis-navigate, or abandon the flow.
4. Attach the screenshots as CI artifacts so reviewers can inspect them without
   running the app.

### 2.5.4 Long-term: structured agent UX test suites

As multimodal and tool-using agents mature, the expectation is to extend this
process into a repeatable agent test suite that walks through defined user
flows and reports friction points in a structured format. The design of those
flows, the defined personas above, and the screenshot infrastructure from
§2.5.3 are the foundations that make this extension possible when the agent
capability catches up.

Contributions that improve the scaffolding for agent-driven UX testing are
explicitly welcome.

---

## 3. Recommended technology stack

The agent may substitute equivalents, but these are the recommended choices.
Justifications follow.

### 3.1 Core framework

**Primary recommendation: Qt 6 with C++**
- Mature, native-feeling on all three target platforms
- First-class image and PDF support
- Excellent printing, file-dialog, and tablet/stylus integration
- Single codebase, single build system (CMake)
- Stylus / pressure-sensitive input via `QTabletEvent`
- Native menu-bar handling on macOS (the menu lives at the top of the screen
  automatically)

**Alternative for faster iteration: PySide6 (Qt 6 in Python).** Slower for
heavy image manipulation but cuts development time substantially. Acceptable
if performance hot paths are pushed into compiled extensions.

**Not recommended:** Electron (too heavy for a document utility), GTK4 (weak
on Windows and macOS), Flutter (immature desktop story for document apps).

### 3.2 Key libraries

| Concern | Library | Notes |
|---|---|---|
| PDF rendering | Poppler (Qt6 bindings) or MuPDF | Poppler for liberal license fit; MuPDF for raw speed |
| PDF manipulation (merge/split/encrypt) | qpdf | Mature, scriptable, well-documented |
| Image I/O — common formats | Qt's built-in `QImage` plugins | PNG, JPEG, GIF, BMP, TIFF, WebP |
| HEIC/HEIF | libheif | Optional dependency; gate the format if not available |
| OpenEXR | OpenEXR (Academy Software Foundation) | High dynamic range |
| RAW formats (CR2, DNG, etc.) | LibRaw | Optional |
| ICC color management | Little CMS (lcms2) | Replaces Apple's ColorSync role |
| OCR | Tesseract (with Leptonica) | Local, language-pack-driven |
| Background removal | ONNX Runtime + a U2-Net or rembg-derived model | Local inference, ~50-200 MB model bundled or downloaded on first use with consent |
| 3D viewing (USD) | OpenUSD (Pixar) or Open3D | Stretch goal; can defer |
| 3D viewing (Collada/.dae) | Assimp + a small OpenGL viewer | Stretch goal |
| Screenshot capture | Qt's `QScreen::grabWindow` baseline; platform-specific helpers for window-by-id capture | See §13 |
| Scanner access | SANE (Linux), WIA (Windows), ImageCaptureCore (macOS) | Plugin-isolated |
| Webcam access | Qt Multimedia / `QCamera` | For signature capture |
| Persistent storage | SQLite | For version history, recent files, settings |
| Crash reporting | Optional, off by default | If included, must be local-log-only or opt-in remote |

### 3.3 Build and packaging

- **Build system:** CMake.
- **CI:** GitHub Actions or equivalent, building all three platforms on every
  PR.
- **Installers:**
  - Windows: MSIX (preferred) and a fallback NSIS installer
  - macOS: signed and notarized `.dmg` containing a `.app` bundle
  - Linux: AppImage as the universal binary; Flatpak as the modern preferred
    distribution; `.deb` and `.rpm` if maintainers volunteer

### 3.4 Architecture

A document-centric architecture with the following layers:

```
┌─────────────────────────────────────────────┐
│          UI layer (Qt widgets/QML)          │
├─────────────────────────────────────────────┤
│   Application services (commands, history)  │
├─────────────────────────────────────────────┤
│  Document model     │  Tool model           │
│  (PDF / Image /     │  (Selection, Markup,  │
│   Multi-doc / 3D)   │   Form, Signature)    │
├─────────────────────────────────────────────┤
│           Format adapters (plugins)         │
│   PDF · PNG · JPEG · TIFF · HEIC · USD · …  │
├─────────────────────────────────────────────┤
│  File-system / scanner / camera / printer   │
└─────────────────────────────────────────────┘
```

Each format adapter exposes a uniform `IDocument` interface (read-only required;
read-write optional for image formats Trailer can edit; PDF is its own
specialised case).

---

## 4. Supported file formats

### 4.1 Read support

| Category | Format | Status | Notes |
|---|---|---|---|
| Vector / page | PDF | Shipped | First-class. Includes form fields, embedded fonts, annotations |
| Raster — common | PNG, JPEG, GIF (still and animated), BMP, TIFF, WebP | Shipped | First-class; in the file-open filter |
| Raster — Apple-popular | HEIC / HEIF | Shipped | Loaded via the platform's HEIC plugin; in the file-open filter |
| Raster — legacy | ICO, ICNS, PPM, PGM, PBM, TGA, SGI, XBM, PICT, PNTG | Partial | Loadable via `QImageReader` when the user picks "All files (\*)"; not in the documented open filter. Best-effort, may be read-only |
| Raster — wide gamut | OpenEXR, HDR (Radiance) | Planned (Phase 6) | Not in the open filter today. Tone-map for display; preserve on export |
| Raster — RAW | CR2, DNG, NEF, ARW, ORF, RAF, etc. | Planned (Phase 6) | Not implemented. Intended path: read-only via LibRaw; export to TIFF/PNG |
| Vector | SVG | Planned | Render to raster for editing; preserve on save-as-SVG when no edits applied |
| Vector | Adobe Illustrator (AI) | Planned | Only when PDF content is embedded (the common case) |
| 3D scene | USD, USDA, USDC | Stretch (Phase 7) | |
| 3D scene | Collada (.dae) | Stretch (Phase 7) | |
| 3D mesh | OBJ, STL | Stretch (Phase 7) | |

### 4.2 Write / export support

| Format | Status | Notes |
|---|---|---|
| PDF | Shipped | Including with annotations, optionally flattened, optionally encrypted |
| PNG, JPEG, TIFF, BMP, WebP | Shipped | In the Export As dialog (`MainWindow::onExportAs`) |
| JPEG-2000 | Planned | Not in the Export As dialog today |
| HEIC | Planned (Phase 6) | When libheif is present and licensed for the target platform; export path not implemented yet |
| OpenEXR | Planned (Phase 6) | Preserve high-dynamic-range data; not implemented yet |

### 4.3 Explicitly out of scope

- EPS / PostScript display. Prior-art Preview removed it; we don't add it back.
  Users with `.eps` / `.ps` should use Ghostscript.
- Microsoft Office formats (.docx, .xlsx, .pptx). Use a real Office app or
  LibreOffice.
- Video and audio. There are excellent dedicated viewers.

---

## 5. UI architecture

### 5.1 Window model

- **Multi-window:** Each open document gets its own window by default. A
  setting allows "open in new tab in last-used window" instead.
- **Tabs:** Standard tabbed-document support. Drag a tab off to detach it into
  its own window. Drag a window onto another's tab strip to merge.
- **Multi-document window:** Selecting "Open" with multiple files, or dragging
  multiple files onto an existing window, opens each file as a tab in a
  single window (image batches in particular share one window so the user
  can flip through them via the tab strip). A planned follow-up is a
  sidebar "document list" mode that replaces the tab strip for batch
  navigation — see `TODO.md` *Window / document model*.

### 5.2 Layout regions

```
┌───────────────────────────────────────────┐
│  Title bar      [share] [markup] [view]   │
├──────────┬────────────────────────────────┤
│          │                                │
│ Sidebar  │       Document canvas          │
│ (thumbs, │                                │
│  TOC,    │                                │
│  notes,  │                                │
│  bookmks)│                                │
│          │                                │
└──────────┴────────────────────────────────┘
```

- Sidebar is collapsible — **Hidden** is the default.
- Sidebar mode is selectable: **Hidden / Thumbnails / Search Results /
  Table of Contents / Highlights & Notes**. Earlier drafts of this doc
  named a *Contact Sheet*, *Bookmarks*, and standalone *Annotations*
  mode; the shipped set consolidated those (Table of Contents reads
  `QPdfBookmarkModel`; Highlights & Notes filters annotation types that
  carry text). See `TODO.md` item #16 in the 2026-04-30 HITL pass for
  the rationale.
- A second toolbar — the **Markup toolbar** — appears beneath the title bar
  when toggled (default shortcut `Ctrl/⌘+Shift+A`).

### 5.3 Theming

- Follows system light/dark mode by default.
- Manual override available in settings.
- Document background colour configurable independently (Preview's "window
  background colour" feature).
- A separate **"Use Dark Appearance for PDF"** toggle in the View menu inverts
  the rendering of light-coloured PDFs for night reading.

### 5.4 Platform conventions

| Concern | macOS | Windows | Linux |
|---|---|---|---|
| Menu bar | Top of screen | Inside window | Inside window (or top depending on DE) |
| Modifier key | ⌘ (Command) | Ctrl | Ctrl |
| Close shortcut | ⌘W | Ctrl+W | Ctrl+W |
| Quit shortcut | ⌘Q | Ctrl+Q (no exit-on-close) | Ctrl+Q |
| File dialogs | Native NSOpenPanel | Native common dialogs | XDG portal preferred over GTK/Qt fallback |
| App data | `~/Library/Application Support/Trailer/` | `%APPDATA%\Trailer\` | `${XDG_DATA_HOME:-$HOME/.local/share}/trailer/` |
| Settings | Same as app data | Same as app data | `${XDG_CONFIG_HOME:-$HOME/.config}/trailer/` |

---

## 6. Feature specifications

Each feature lists: **What it does · UI surface · Implementation notes ·
Acceptance criteria.**

### 6.1 Opening, viewing, navigating

#### 6.1.1 Open files

- **What:** Open one or more PDFs, images, or supported 3D files.
- **UI:** `File > Open` (`Ctrl/⌘+O`); `File > Open Recent` (last 20 files,
  configurable); native file dialog; drag-and-drop onto window or app icon;
  command-line invocation `trailer file1.pdf file2.png`.
- **Notes:** Multiple files opened together obey the "open in same window"
  setting (§5.1).
- **Acceptance:** Opens a 100 MB PDF and a 50 MP image without UI freeze
  (loading happens off the UI thread).

#### 6.1.2 View modes

- **What:** Single Page, Two Pages, Continuous Scroll. Thumbnails sidebar
  is shipped. Contact Sheet (grid of all pages) is **Planned** — not in
  the shipped sidebar modes (§5.2).
- **UI:** `View` menu and toolbar buttons.
- **Notes:** Continuous Scroll must virtualise — only render pages within and
  near the viewport.
- **Acceptance:** Continuous scroll through a 500-page PDF maintains 60 fps on
  a mid-range 2024 laptop.

#### 6.1.3 Zoom

- **What:** Zoom In, Zoom Out, Actual Size, Fit to Window, Zoom to Selection.
- **UI:** `View` menu, toolbar `[− 100% +]` widget, pinch gestures on
  trackpads, `Ctrl/⌘+Scroll` with mouse, keyboard shortcuts (§14).
- **Notes:** Zoom is per-window. Zoom level persists with auto-save metadata.

#### 6.1.4 Magnifier / loupe

- **What:** A circular magnifier follows the cursor and shows a zoomed view of
  the area beneath it.
- **UI:** `Tools > Show Magnifier` (` ` ` shortcut). Esc to dismiss.
- **Notes:** Different magnifier shape over PDFs (rectangular, content-aware
  size) than over images (circular). Pinch gesture or `+` / `-` resizes it.

#### 6.1.5 Page navigation

- **What:** Previous/Next page buttons, Go-To-Page dialog, scroll-wheel and
  keyboard navigation.
- **UI:** Toolbar arrows, `Go > Go to Page`, `Page Up/Down`, `Option/Alt+↑/↓`
  for prev/next page in continuous mode.
- **Notes:** Pressure-sensitive force scroll on Mac trackpads (where supported)
  to accelerate.

#### 6.1.6 Tabs and multi-window

- See §5.1.
- **Acceptance:** Tab drag-detach and drag-attach work cross-platform.

#### 6.1.7 Full-screen / presentation

- **Status:** **Planned** — not implemented on `main` today. No
  `Enter Full Screen` or `Slideshow` action is wired in
  `src/ui/MainWindow.cpp`; no `F11` handler exists.
- **What:** Distraction-free full-screen mode and a slideshow mode for PDFs.
- **UI (intended):** `View > Enter Full Screen` (`Ctrl/⌘+Ctrl+F` on Mac
  convention, `F11` on Windows/Linux). `View > Slideshow` for
  PDF-as-presentation with on-screen controls.

#### 6.1.8 HDR display

- **What:** Render HDR images with the wider luminance range when the OS and
  display support it; otherwise tone-map gracefully.
- **Notes:** Use the OS HDR APIs where available (Windows 10+ HDR APIs, macOS
  EDR, Linux/Wayland HDR-extension when Wayland's HDR support stabilises).

#### 6.1.9 Animated GIFs

- **What:** Play animated GIFs by default; expose individual frames in the
  thumbnails sidebar so users can scrub through them.
- **UI:** A play/pause button overlays animated images. Sidebar shows
  numbered frame thumbnails when `View > Frames` is selected.

### 6.2 Search and text intelligence

#### 6.2.1 Find in document

- **What:** Search for text in PDFs.
- **UI:** Toolbar search field; `Ctrl/⌘+F`. Results highlighted in document and
  listed in sidebar. Sort by Page Order or Match Rank. Toggle "Any Match"
  (any of the words) vs "Phrase" (default).
- **Acceptance:** Searches a 1000-page PDF in under 2 seconds on indexed reopen,
  under 10 seconds cold.

#### 6.2.2 Live Text on images (OCR)

- **What:** Detect and select text inside raster images. Right-click selection
  to Copy, Look Up (system dictionary), Open URL, Email, or Call (where the OS
  exposes such handlers).
- **UI:** `Tools > Detect Text in Image` triggers OCR; once detected, hovering
  the cursor shows a text-cursor over recognised text and the user can drag
  to select. A persistent "Text" badge appears in the toolbar when the image
  has detected text.
- **Notes:** Tesseract for the recognition. Cache results in the sidecar
  metadata store (§9). Languages selectable in settings; English bundled,
  other language packs downloadable on-demand with explicit consent.
- **Acceptance:** Recognises a screenshot of a typical web page in under 3
  seconds on first run, instantly on revisit.

#### 6.2.3 Embed text in PDF (PDF OCR)

- **What:** Convert a scanned PDF into a searchable PDF by running OCR and
  embedding an invisible text layer aligned with the rendered glyphs.
- **UI:** `Tools > Export As…` with an "Embed text (OCR)" checkbox
  (checkbox itself is Planned — not in the shipped dialog).
- **Notes:** Tesseract again. Preserve the visual fidelity of the original;
  the text layer is selectable but invisible.
- **Acceptance:** A 50-page scanned PDF becomes searchable; text-selection
  positions match visible glyphs within ±2 pixels at 100% zoom.

#### 6.2.4 Translation

- **Status:** Out of scope for v1 (would require either bundling a translation
  model or calling an online service; both have downsides).
- **Future:** Optional plug-in interface that lets a user wire in a local
  translation engine if they have one.

### 6.3 PDF editing

#### 6.3.1 Markup toolbar

- **What:** A second toolbar offering annotation tools.
- **UI:** Toggle with `Ctrl/⌘+Shift+A`.
- **Tools:**
  - **Text Selection** — drag to select; `Option/Alt`-drag for column selection.
  - **Rectangular Selection** — drag a rect; copy/delete/zoom-to.
  - **Redaction Selection** — paints over a region. Provisional during the
    session; **permanently flattened on save**. Clear UI warning before
    finalising.
  - **Sketch** — freehand stroke that auto-recognises common shapes (line,
    rectangle, circle, arrow). Floating toolbar lets the user revert to the
    raw sketch.
  - **Draw** — freehand stroke with pressure sensitivity (stylus / supported
    trackpads).
  - **Shapes** — line, arrow, rectangle, rounded rectangle, ellipse, polygon,
    star, speech bubble, thought bubble. Plus a **Highlight** shape (a
    semi-transparent rectangle for emphasis) and a **Zoom Lens** annotation
    that magnifies a region.
  - **Text** — drag to place a text box.
  - **Highlight Selection** — toggleable mode that auto-highlights any text
    you select. Colour and underline/strikethrough variants in the dropdown.
  - **Sign** — see §6.4.
  - **Note** — sticky note. Colour-codable. Author name from settings.
  - **Form Fill** — see §6.4.
  - **Crop** — see §6.3.6.
  - **Rotate Left / Rotate Right** — rotates the current page (or selected
    pages in the sidebar).

- **Style controls (right side of toolbar):**
  - **Shape Style** — line thickness, dash pattern, drop shadow.
  - **Border Color** / **Fill Color** — colour pickers with recents.
  - **Text Style** — font, size, weight, colour.

#### 6.3.2 Highlight, underline, strikethrough

- **What:** As above but accessible without the full markup toolbar.
- **UI:** Select text → context menu → Highlight / Underline / Strikethrough
  with colour submenu. Removable via Control-click → Remove.
- **Sidebar:** `View > Highlights & Notes` switches sidebar to the list of all
  highlights and notes in the document, click to jump to.

#### 6.3.3 Notes and speech bubbles

- **What:** Sticky notes (collapsible) and inline speech bubbles.
- **UI:** Note tool drops a note; click to expand/edit. Speech bubble via
  `Tools > Annotate > Speech Bubble`.
- **Notes:** Author name defaults to the OS username; configurable in settings;
  can be entirely suppressed.

#### 6.3.4 Combine PDFs

- **What:** Merge two or more PDFs into one.
- **UI:** Drag pages from the thumbnail sidebar of one PDF window into another;
  drag a PDF file from the OS file manager onto an open PDF's sidebar to merge
  it whole.
- **Notes:** Uses qpdf under the hood. Original files untouched unless saved.

#### 6.3.5 Add / delete / reorder pages

- **What:** Page-level operations on PDFs.
- **UI:** From Thumbnails or Contact Sheet:
  - Right-click → **Insert Blank Page** / **Insert Pages from File…** /
    **Insert Pages from Scanner…** / **Delete** / **Rotate Left / Right**.
  - Drag thumbnails to reorder.
  - Drag thumbnails between PDF windows to move pages.
  - Drag thumbnails out to the desktop / file manager to spawn a new PDF
    containing just those pages.
- **Notes:** Deleting a page also drops its annotations; warn the user the
  first time.

#### 6.3.6 Crop and rotate

- **What:** Page-level crop and rotation.
- **UI:** Crop tool in Markup toolbar. Crop Inspector shows live dimensions
  in user-selected units (mm / cm / in / px / pt).
- **Notes:** PDF cropping is a viewport / `MediaBox`/`CropBox` operation, not
  destructive; restore via Browse All Versions.

#### 6.3.7 Quartz-equivalent filters

- **What:** Apply colour or processing filters during export.
- **UI:** `Tools > Export As…` exposes a "Filter" dropdown with built-ins:
  Black & White, Greyscale, Sepia, Reduce File Size, Lighten, Blue Tone, Grey
  Tone, Custom… (loads a user-supplied LUT).
- **Notes:** Implemented as a filter pipeline. Users can author and load
  custom filters as `.toml` or `.json` definitions.

### 6.4 Forms and signatures

#### 6.4.1 Fill PDF forms

- **What:** Type into AcroForm and XFA-ish form fields in PDFs.
- **UI:** Click a field to focus, type. Tab moves between fields. Checkboxes
  toggle on click. Signature fields launch the Sign tool.
- **Form Filling Toolbar:** `View > Show Form Filling Toolbar` exposes a "Text
  Box" tool that lets the user place a text box for free-typing on PDFs that
  lack proper form fields.

#### 6.4.2 AutoFill from address book

- **What:** Pull name, address, email, phone from a single user-defined
  "My Card" record stored locally.
- **UI:** "AutoFill Form" button at top of any PDF with detected form fields.
  Field-level AutoFill via the field's context menu.
- **Notes:**
  - "My Card" lives in Trailer's local settings store. No OS contacts
    integration required (we explicitly do not want to read the system
    address book; that's a privacy boundary).
  - User can define multiple cards (e.g., personal / work) and pick which to
    use.
- **Acceptance:** A standard W-9 / equivalent form fills 80%+ of fields
  automatically with one click.

#### 6.4.3 Signatures

- **What:** Capture a handwritten signature once, drop it onto any PDF.
- **Capture methods:**
  - **Trackpad:** Sign with finger or stylus on a precision trackpad. Pressure
    sensitivity where supported.
  - **Camera (Planned):** Sign on white paper, hold up to webcam;
    OpenCV-based capture extracts the signature with transparency.
    Not implemented on `main` today — `SignatureCaptureDialog` ships
    only Draw and Import tabs.
  - **Tablet / stylus:** Any HID stylus device (Wacom, Surface Pen, XP-Pen,
    Huion). `QTabletEvent`.
  - **Image import:** Drop in a PNG/JPEG of a signature; auto-extract dark
    ink against light background.
- **Storage:** Local only. `~/.../trailer/signatures/`. Each signature is a
  PNG with alpha plus a small JSON metadata file (label, created date,
  optional text description for screen readers).
- **No sync.** Explicit non-goal. If the user wants their signatures on
  multiple machines, they copy the folder.
- **Use:** From the Sign tool, pick a saved signature, drag to position,
  resize via handles. Signatures are flattened on save.

#### 6.4.4 Reply-with-PDF integration

- **What:** After filling/signing, hand off the PDF to the OS's default
  mail client.
- **UI:** `File > Email Document…`. Spawns a `mailto:` with the PDF as an
  attachment via the OS handler.
- **Out of scope:** Direct integration with specific mail apps. We rely on
  the OS-default mail handler.

### 6.5 Image editing

#### 6.5.1 Crop / resize / rotate / flip

- **What:** Standard raster operations.
- **UI:**
  - **Crop:** Markup toolbar Rectangular Selection + Crop button. Crop
    Inspector shows live dimensions in user units.
  - **Resize:** `Tools > Adjust Size…` (`Ctrl/⌘+Option/Alt+I`). Width / Height
    / "Fit into" presets / Percent / "Scale Proportionally" toggle / "Resample
    Image" toggle. Resampling off lets the user change DPI without losing
    pixels.
  - **Rotate:** Toolbar Rotate Left / Right. Option/Alt to reverse direction.
    Multi-thumbnail selection rotates all selected.
  - **Flip:** `Tools > Flip Horizontal` / `Vertical`.
- **Acceptance:** All operations work on a 100 MP image within 2 seconds on a
  mid-range 2024 laptop.

#### 6.5.2 Adjust colour

- **What:** Live histogram + sliders for exposure, contrast, highlights,
  shadows, saturation, temperature, tint, sepia, sharpness.
- **UI:** `Tools > Adjust Color…` (`Ctrl/⌘+Option/Alt+C`). Auto Levels button.
  Reset All button.
- **Notes:** Non-destructive while panel is open; bakes on close. (For truly
  non-destructive editing, the user wants a real raster editor.)

#### 6.5.3 Selection tools

- **Rectangular**, **Elliptical**, **Lasso**, **Smart Lasso** (edge-snapping;
  uses GrabCut or a similar algorithm), **Instant Alpha** (paint to select
  colour-similar regions; useful for solid-background removal).
- **Acceptance:** Smart Lasso traces a clear edge with under 5% pixel error
  on typical product photos.

#### 6.5.4 Background removal (one-click)

- **What:** Single button that removes the background using a learned model,
  preserving foreground subjects.
- **UI:** Toolbar button `Remove Background` (`Shift+Ctrl/⌘+K`). Prompts to
  convert non-PNG images to PNG to preserve alpha.
- **Implementation:** ONNX Runtime + a U2-Net or similar segmentation model.
  Model is bundled if licence permits, or downloaded on first use with explicit
  user consent (since we are local-first by default).
- **Settings:** Option to choose model quality / size tradeoff (small=fast,
  large=better edges).
- **Acceptance:** First-run completes within 10 seconds for an HD image on
  CPU; under 2 seconds with GPU acceleration where available.

#### 6.5.5 Extract / cutout

- **What:** Use Smart Lasso to outline an object, then `Edit > Copy` to copy
  it with transparency.
- **UI:** Smart Lasso → trace → Copy. Cutout becomes a PNG-with-alpha on the
  clipboard.

#### 6.5.6 Image description / alt text

- **What:** A field where the user can write an alt-text description of the
  image, embedded in the file's metadata.
- **UI:** Markup toolbar "Image Description" tool, or `Tools > Image
  Description…`.
- **Notes:** Stored as XMP / EXIF metadata where the format supports it. For
  formats that don't, store in a sidecar.

#### 6.5.7 Take a screenshot

- **What:** Trailer can capture a screenshot directly into a new untitled
  document.
- **UI:** `File > Take Screenshot >` `Entire Screen` / `Selection` / `Window`.
- **Implementation:**
  - macOS: `CGWindowListCreateImage` and `CGDisplayCreateImage`.
  - Windows: GDI / `BitBlt` for the baseline; `Windows.Graphics.Capture` for
    per-window capture on Windows 10+.
  - Linux X11: `XShmGetImage` / `xcb_shm_get_image`.
  - Linux Wayland: portal-based capture via `xdg-desktop-portal`'s
    `Screenshot` interface (must work without breaking Wayland's security model).

#### 6.5.8 Convert image file types

- **What:** Re-save in another format.
- **UI:** `Tools > Export As…` exposes the Format dropdown. Default list shows the
  big eight (HEIC, JPEG, JPEG-2000, OpenEXR, PDF, PNG, TIFF, WebP); hold
  `Option/Alt` to expose specialised / older formats.
- **Per-format options:**
  - JPEG: quality slider, progressive toggle, chroma subsampling.
  - PNG: compression level, interlace toggle.
  - TIFF: compression (LZW, ZIP, none), bit depth.
  - WebP: quality, lossless toggle.
  - PDF: filter, encryption, embedded text option.

### 6.6 Colour management

#### 6.6.1 Assign profile

- **What:** Permanently tag an image with an ICC profile.
- **UI:** `Tools > Assign Profile…` lists installed ICC profiles.

#### 6.6.2 Soft-proof / preview on another device

- **What:** Temporarily render the image as it would look on a target device
  (printer, sRGB display, P3 display) without modifying the file.
- **UI:** `View > Soft Proof with Profile…`.
- **Notes:** All implemented with Little CMS. Trailer ships with sRGB,
  Adobe RGB, Display P3, and a generic CMYK profile bundled. Users can
  install custom `.icc` files into their ICC profile directory.

### 6.7 Importing from devices

#### 6.7.1 Camera / phone import

- **What:** Pull photos from a connected USB camera or phone.
- **UI:** `File > Import from Camera…` lists detected devices.
- **Implementation:**
  - libgphoto2 on Linux and macOS for SLR / mirrorless.
  - Windows: WIA + WPD for cameras and phones in MTP mode.
  - macOS: ImageCaptureCore.
- **Out of scope (no equivalent without cloud):** "Take a photo on my phone
  and have it appear in this app instantly." That's a Continuity-class
  feature that requires either cloud or pairing. Defer.

#### 6.7.2 Scanner import

- **What:** Drive a connected scanner.
- **UI:** `File > Import from Scanner > [scanner]`. Dialog offers Scan Mode
  (Flatbed / Document Feeder), Kind (Text / Greyscale / Colour), Resolution,
  Duplex toggle, page Size, basic colour/exposure adjustments, Scan-To
  destination.
- **Scan-to-PDF:** Direct multi-page capture into a single PDF document with
  optional OCR pass.
- **Implementation:**
  - Linux: SANE (`libsane`).
  - Windows: WIA.
  - macOS: ImageCaptureCore.
- **Acceptance:** Detects all SANE-supported scanners on a Linux test box; a
  reference Windows test scanner; an AirPrint-discoverable scanner on macOS.

#### 6.7.3 Photo location

- **What:** Show GPS metadata as a pin on a map.
- **UI:** `Tools > Show Location Info`. World map view + a "Show in Maps"
  button that opens the location in the user's default map application
  (`https://www.openstreetmap.org/?mlat=…` or platform handler).
- **Notes:** Map tiles served from OpenStreetMap by default; users can swap
  the tile URL in settings (since some users will want to keep even map-tile
  loads off the network — provide an offline tile bundle as an optional
  download in v2).

### 6.8 3D file viewing (Phase 7 stretch)

#### 6.8.1 USD viewer

- Pan, orbit, zoom. Camera presets where embedded. Animation playback if
  present. Scene hierarchy in sidebar.
- Export to PDF (snapshot of current view) and to image formats.

#### 6.8.2 Collada / OBJ / STL viewer

- Same controls; simpler scene model. Multiple lighting options.

### 6.9 Inspector

- **What:** Metadata and structure inspector pane.
- **UI:** `Tools > Show Inspector` (`Ctrl/⌘+I`). Tabs:
  - **General:** filename, type, dimensions, file size, colour space, DPI,
    creation/modification date, author, page count.
  - **More info:** EXIF, IPTC, XMP. Camera make/model, lens, ISO, shutter,
    aperture for photos.
  - **Keywords:** add/remove searchable keywords. Stored in XMP. Indexed by
    Trailer's local search (§9.4).
  - **Encryption:** for password-protected PDFs, shows permission flags.
  - **Crop:** live dimensions of the current selection in user units.
  - **Annotations:** flat list of annotations in the document; click to
    jump to.

### 6.10 Save, version history, lifecycle

#### 6.10.1 Auto-save

- **What:** Trailer continuously saves edits, exactly like Preview does.
- **Behaviour:**
  - Save on every meaningful edit (annotation added, page reordered, etc.).
  - Debounced to coalesce bursts.
  - Snapshot to the local version store at least hourly during active editing
    and on every open / save / duplicate / lock / rename / revert.
- **Setting:** Auto-save can be **disabled** entirely, in which case Trailer
  behaves like a traditional "you must press Save" app. (This is the right
  default for a chunk of users; we should not impose Apple's choice on
  everyone.)

#### 6.10.2 Versions

- **What:** Local version history per file.
- **Storage:** Versions stored alongside Trailer's app data, not in the
  user's file. SQLite index of version metadata; version blobs stored as
  individual files in a versions directory keyed by document hash.
- **UI:** `File > Revert To >` `Last Opened [date]` / `Last Saved [date]` /
  `Browse All Versions…`. The Browse view is a Time-Machine-like timeline
  with the current document on the left and the historical versions
  cascading on the right.
- **Restore:** "Restore" replaces the current document; "Restore a Copy"
  (Option/Alt-click) creates a new file from that version.
- **Retention:** Configurable (default: keep one version per hour for the
  last day, one per day for the last month, one per week thereafter, indefinitely).
- **Privacy:** Version store can be wiped from the settings pane.

#### 6.10.3 Save / Save As / Duplicate

- `File > Save` (`Ctrl/⌘+S`).
- `File > Save As…` (`Ctrl/⌘+Shift+S`); also reachable by Option/Alt-clicking
  the File menu when auto-save is on (shows "Save As…" instead of "Duplicate").
- `File > Duplicate` creates an unsaved copy in memory.

#### 6.10.4 Lock

- **What:** Mark a file as locked so accidental edits are prevented.
- **UI:** Title-bar dropdown next to the document name → Locked checkbox.
- **Implementation:** Sets the OS-level read-only flag on the file when
  possible; also tracks Trailer's own lock state in the version store.

### 6.11 Export, share, print, secure

#### 6.11.1 Export

- See §6.5.8 and §6.3.7.

#### 6.11.2 Share

- **What:** Hand off the document to another app.
- **UI:** Toolbar Share button → list of OS-default share targets where the
  OS provides an API for them; otherwise a fallback list of "Open with…"
  applications and a "Save copy to…" option.
- **Per platform:**
  - macOS: `NSSharingService`.
  - Windows 10+: `Windows.ApplicationModel.DataTransfer.DataTransferManager`
    for the system share sheet.
  - Linux: XDG portal share interface where available; fallback to "Open
    with…" via the user's MIME associations.

#### 6.11.3 Print

- **What:** Standard print dialog with PDF/image-aware options.
- **UI:** `File > Print…` (`Ctrl/⌘+P`).
- **Options:** Selection-only, Auto Rotate to fill page, Show Notes (include
  PDF note annotations on the printout), Scale (percent or fit), Copies per
  Page (multiple impressions per sheet), Pages per Sheet (multiple PDF pages
  per sheet via Layout pane).

#### 6.11.4 Password-protect a PDF

- **What:** Encrypt a PDF with passwords.
- **UI:** `File > Export as Password-Protected PDF…` (a dedicated menu
  entry, not the general Export As dialog). Two layers:
  1. **Open password** — required to open the document.
  2. **Owner password** — required to bypass per-feature restrictions.
- **Granular permissions** (each a checkbox):
  - Allow printing
  - Allow copying text
  - Allow adding annotations
  - Allow filling forms
  - Allow extracting pages
- **After-the-fact management:** `File > Edit Permissions…` lets the owner
  modify or strip protection.
- **Implementation:** qpdf, AES-256.

#### 6.11.5 Reduce PDF size

- **What:** Re-encode a PDF to be smaller, typically by recompressing
  embedded images.
- **UI:** `File > Reduce File Size…` (a dedicated menu entry, not a
  filter on the general Export As dialog). Detail options:
  - Create Linearised PDF (fast web view).
  - Optimise images for screen (downsample to display DPI).
  - Re-encode images as JPEG (with quality slider).
  - Subset embedded fonts.
  - Strip metadata (with a confirmation, since this is destructive).

#### 6.11.6 Redaction

- **What:** Permanently remove content from a PDF.
- **UI:** Markup toolbar Redaction Selection. Provisional during the session
  (rendered as a black rectangle); a "Apply Redactions" command finalises by
  rasterising the affected page region and rewriting the PDF without the
  underlying glyphs.
- **Warnings:** A clear modal on first use explains what redaction does and
  doesn't guarantee. Trailer is not a defence-grade redaction tool — for
  high-stakes redaction, use a tool that can scrub object streams.

### 6.12 Accessibility

- **Screen reader support** via each platform's accessibility API (UIA on
  Windows, NSAccessibility on macOS, AT-SPI on Linux).
- **Keyboard navigation** for every UI element. No mouse-only paths.
- **Read-aloud** for selected text (TTS via the OS speech engine).
- **Image descriptions** (§6.5.6) round-trip with screen readers.
- **High-contrast theme** independent of system theme.
- **Configurable text size** in the UI.
- **Reduce motion** setting suppresses non-essential animations.

### 6.13 Settings

A single settings window with the following panes:

| Pane | Settings |
|---|---|
| **General** | Theme (System / Light / Dark / High Contrast); window background colour; "Open files in" (new window / same window / new tab); confirm-before-close-with-changes |
| **Files** | Auto-save toggle; version-retention policy; default save location; default export format |
| **PDF** | Default view (Continuous / Single / Two Pages); reopen at last viewed page; Roman numeral page numbers; default annotation author name; "Add author name to annotations" toggle |
| **Images** | 100% scale definition (1px = 1 screen point / size on screen = size on print); default colour profile assumption for untagged images |
| **OCR** | Installed languages; download additional languages; default language |
| **Background removal** | Model size (small / large); GPU acceleration toggle; download model on first use |
| **Forms** | Manage saved AutoFill cards |
| **Signatures** | List, rename, delete saved signatures; export / import signatures (file-based, no sync) |
| **Maps** | Tile provider URL; offline tile bundle (when available) |
| **Privacy** | Wipe recent files; wipe version history; wipe signatures; wipe OCR cache; one-click "wipe everything" |
| **Advanced** | Custom Quartz-equivalent filters folder; ICC profiles folder; plugin directory |
| **Shortcuts** | View / customise keyboard shortcuts |

---

## 7. Keyboard shortcuts

Cross-platform table; use `Ctrl` on Windows/Linux and `⌘` on macOS unless
noted otherwise. Source of truth is `src/ui/MainWindow.cpp` — every row
here is reconciled against the live `setShortcut` calls. Rows tagged
*Planned* are intended bindings whose underlying action either isn't
implemented yet or hasn't been wired to a shortcut.

**File**

| Action | Shortcut |
|---|---|
| Open | `Ctrl/⌘+O` |
| Save | `Ctrl/⌘+S` |
| Save As | `Ctrl/⌘+Shift+S` |
| Export As | *Planned* — `Ctrl/⌘+Shift+E` intended; action wired (`Tools > Export As…`) but no shortcut bound yet |
| Close window | `Ctrl/⌘+W` |
| Quit | `Ctrl+Q` (Linux/Win) / `⌘Q` (Mac) |
| Print | `Ctrl/⌘+P` |

**Edit**

| Action | Shortcut |
|---|---|
| Undo / Redo | `Ctrl/⌘+Z` / `Ctrl/⌘+Shift+Z` |
| Select All | `Ctrl/⌘+A` |
| Find | `Ctrl/⌘+F` |
| Find Next / Previous | `Ctrl/⌘+G` / `Ctrl/⌘+Shift+G` |

(Copy / Cut / Paste have no app-level menu action; native Qt widgets handle
the standard shortcuts inside text inputs.)

**View**

| Action | Shortcut |
|---|---|
| Show / hide sidebar | `Ctrl/⌘+Shift+D` |
| Show / hide markup toolbar | `Ctrl/⌘+Shift+A` |
| Show / hide form toolbar | `Ctrl/⌘+Shift+B` |
| Show / hide inspector | `Ctrl/⌘+I` |
| Magnifier | `` ` `` (backtick) |
| Full screen | *Planned* — `F11` (Win/Linux) / `Ctrl+⌘+F` (Mac) intended; feature not implemented, see §6.1.7 |
| Zoom in | `Ctrl/⌘+=` (and the platform `QKeySequence::ZoomIn`) |
| Zoom out | `Ctrl/⌘+-` (the platform `QKeySequence::ZoomOut`) |
| Fit page | `Ctrl/⌘+0` |
| Actual size | `Ctrl/⌘+1` |
| Fit width | `Ctrl/⌘+2` |

**Go (page navigation)**

| Action | Shortcut |
|---|---|
| First page | `Ctrl/⌘+Home` |
| Previous page (Go menu) | `Ctrl/⌘+Left` |
| Next page (Go menu) | `Ctrl/⌘+Right` |
| Last page | `Ctrl/⌘+End` |
| Previous page (viewer) | `PageUp` |
| Next page (viewer) | `PageDown` |
| Go to page… | `Ctrl/⌘+Alt/Option+G` |

**Tools / image**

| Action | Shortcut |
|---|---|
| Rotate left | `Ctrl/⌘+L` |
| Rotate right | `Ctrl/⌘+R` |
| Fill forms | `Ctrl/⌘+Shift+F` |
| Take screenshot | `Ctrl/⌘+Shift+3` |
| Adjust size | *Planned* — `Ctrl/⌘+Alt/Option+I` intended; action wired (`Tools > Adjust Size…`) but no shortcut bound yet |
| Adjust colour | *Planned* — `Ctrl/⌘+Alt/Option+C` intended; action wired but no shortcut bound yet |
| Remove background | *Planned* — `Ctrl/⌘+Shift+K` intended; action wired (`Tools > Remove Background`) but no shortcut bound yet |

**Window**

| Action | Shortcut |
|---|---|
| Minimize | `Ctrl/⌘+M` |
| Next tab / previous tab | *Planned* — `Ctrl+Tab` / `Ctrl+Shift+Tab` intended; no application-level shortcut bound (Qt's QTabWidget handles internal navigation but not via these keys) |
| Next / previous document in window | *Planned* — `Alt/Option+PageDown` / `Alt/Option+PageUp` intended; not bound |

All shipped shortcuts are reassignable in Settings → Shortcuts.

---

## 8. Differences from the prior art (deliberate scope reductions)

The agent should resist any temptation to "add back" the following Apple
features, which are intentionally excluded:

| Excluded | Why |
|---|---|
| Cloud sync of any kind (documents, signatures, settings) | Local-first principle |
| Continuity Camera / Continuity Markup | Requires Apple's pairing infrastructure |
| AirDrop / direct device-to-device transfer | Out of scope; rely on OS share sheet |
| Apple Intelligence Writing Tools (proofread / rewrite / summarise) | Generative AI feature; out of scope for v1 |
| Spotlight indexing of Trailer keywords | Local search built in instead (§9.4) |
| EPS / PostScript display | Removed by Preview itself in 2022; we don't bring it back |
| Reading the system address book for AutoFill | Privacy boundary; local "My Card" instead |
| Mail-app integration that goes beyond `mailto:` | OS-default handler is the limit |
| iPhone Mirroring, Live Activities | Belongs in OS, not in a document app |
| Translation that requires online services | Out of scope; plug-in interface in v2 |

---

## 9. Cross-cutting subsystems

### 9.1 Document model

- `IDocument` interface (`src/document/IDocument.h`) with concrete
  implementations `PdfDocument`, `ImageDocument`, `StubDocument`. Each
  is paired with an `IFormatAdapter` subclass registered into
  `DocumentRegistry` at startup; the registry dispatches file opens
  by extension. Capability methods (`supportsZoom`, `supportsEditing`,
  …) gate UI on a per-format basis. (See
  [`docs/CONVENTIONS.md`](../docs/CONVENTIONS.md) §1 for the recipe
  to add a new document type.)
- Mutations split between two undo mechanisms by category:
  **`PdfCommand` subclasses** (`src/document/PdfCommands.h`) for
  qpdf-level page operations — rotate today, delete / move / insert
  / crop drafted on the in-flight branch — with symmetric
  `apply` / `revert`; and **`AnnotationStore` snapshot undo**
  (`src/annotation/AnnotationStore.h`) for annotation create /
  modify / delete via whole-store snapshots. The two stacks share a
  cross-routing heuristic (`MainWindow::m_lastUndoSource`); a future
  unified chronological log is roadmap-tracked.
- Documents expose Qt signals (`IDocument::stateChanged` and
  format-specific signals on the concrete subclasses) for UI to
  observe.

### 9.2 Storage layout

All paths below are computed by `src/settings/AppPaths.cpp` and
correspond to one-line accessors there. Directories listed as
*Reserved* exist in `AppPaths` and are created at need, but no
shipped feature populates them yet — they're the on-disk seats
that the planned subsystems below will plug into.

```
${app_data}/
  trailer/
    settings.toml            # User settings
    recent.json              # Recent files (last 50)
    cards.toml               # AutoFill cards (My Card and others)
    signatures/
      sig_<YYYYMMDDhhmmsszzz>_<NNN>.png   # Signature image (PNG with alpha)
      sig_<YYYYMMDDhhmmsszzz>_<NNN>.json  # Metadata (label, created, description)
    models/                  # ONNX model weights downloaded on first use
                             # (U²-Net, MobileSAM, PP-OCRv3)
    autofill/                # Reserved — additional autofill data
                             # beyond cards.toml
    versions/                # Reserved — per-document version blobs
                             # (Phase 7 auto-save / browse versions)
    ocr_cache/               # Reserved — disk-persisted OCR results
                             # (the in-flight branch uses an in-memory
                             # SelectableTextStore per doc; persistence
                             # is the follow-up)
    icc/                     # Reserved — user-supplied ICC profiles
                             # (lcms2 colour-management Phase 6)
    filters/                 # Reserved — user-supplied colour filters
    plugins/                 # Reserved — user-installed format adapter
                             # plugins (Phase 8+; §9.3 below)
    logs/
      trailer_<date>.log     # Local logs only
```

### 9.3 Plugin interface

**Status:** Planned (Phase 8+). The `plugins/` directory is reserved
on disk and `IFormatAdapter` is the right shape for third-party
adapters, but no plugin-loading code is shipped today — adapters
are registered in-process by `Application` startup. The design
below is the intended contract for the eventual loader.

A plugin is a shared library (`.dll` / `.so` / `.dylib`) that registers one
or more `IFormatAdapter` implementations at startup. Format adapters declare:

- MIME types and extensions handled
- Capabilities (read, write, partial read, thumbnail-only)
- A factory that constructs `IDocument` instances from a path or byte stream

Plugins must be sandboxed where the OS supports it (macOS App Sandbox,
Windows AppContainer where applicable, Linux seccomp / bubblewrap on Flatpak
builds).

### 9.4 Local search

**Status:** Planned. No SQLite FTS index, no `Ctrl/⌘+K` command
palette, no `search.sqlite` on disk today. In-document Find
(`Ctrl/⌘+F`, §6.2.1) and the OCR pump's `SelectableTextStore` are
the shipped pieces. The cross-document index below is the intended
shape when it lands.

- SQLite full-text search index over: filename, document title metadata,
  user-assigned keywords, OCR-recognised text, PDF text content of opened
  documents.
- Surfaced in a "Search Trailer" command palette (`Ctrl/⌘+K`).
- Index lives at `${app_data}/trailer/search.sqlite`.
- Indexing is opt-in. When enabled, only files the user has opened in Trailer
  are indexed (we do not crawl the user's filesystem).

### 9.5 Logging and diagnostics

- All logs local. No remote log shipping.
- Verbose logging togglable in settings.
- Crash reports written locally; "Open Crash Reports Folder…" button in
  settings. No upload.

---

## 10. Implementation phases

Phases are sequential; each ends with a shippable build. The
original spec carried "Weeks N–M" estimates; those have been
dropped as fiction (real-time elapsed has diverged enough that
keeping them invites the wrong inferences). The live phase
position is in [`AGENTS.md`](AGENTS.md) §*Phase status*; the per-
phase status markers below summarise it inline.

### Phase 0 — Foundations *(Shipped)*

- Project scaffold, CMake build, CI on all three platforms.
- Window / tab / sidebar shell.
- Settings system and storage layout.
- File-open pipeline; open with command-line argument; recent files.
- Stub `IDocument` interface and a stub adapter that displays "Unsupported".

### Phase 1 — MVP viewer *(Shipped, with deltas)*

- PDF rendering: Qt PDF (`QPdfView` + `QPdfDocument`), not Poppler /
  MuPDF as the spec originally suggested.
- Image rendering for PNG, JPEG, GIF (still + animated), BMP,
  TIFF, WebP, HEIC.
- Thumbnails sidebar — shipped. Contact Sheet — Planned (§6.1.2).
- View modes (Single, Two, Continuous).
- Zoom, pan, fit, actual size, magnifier.
- Find in PDF.
- Print.
- Save (read-only docs output via Print-to-PDF).
- Light/dark theme.
- Animated GIF playback and frame inspection.

### Phase 2 — PDF page operations *(Shipped)*

- Combine PDFs (drag between sidebars).
- Insert / delete / reorder / rotate pages.
- Crop pages.
- Drag thumbnails to the desktop to spawn a new PDF.
- Per-page operations apply to multi-selection.
- Save / Save As / Duplicate.

### Phase 3 — Image editing *(Shipped)*

- Crop, resize, rotate, flip.
- Adjust Colour panel.
- Selection tools (rectangular, elliptical, lasso).
- Convert / export to other image formats.
- Take screenshot (per-platform implementations).

### Phase 4 — Annotation / Markup *(Shipped)*

- Markup toolbar.
- Sketch, Draw, Shapes (incl. Highlight and Zoom Lens annotations), Text,
  Note, Speech Bubble.
- Highlight / Underline / Strikethrough on PDF text.
- Style controls (Shape, Border Color, Fill Color, Text Style).
- Inspector pane (General, More Info, Annotations).
- Highlights & Notes sidebar.

### Phase 5 — Forms, signatures, security *(Shipped, with deltas)*

- PDF form filling.
- AutoFill cards (My Card).
- Signature capture: trackpad and image import shipped; tablet (HID
  stylus) shipped; **Camera** (webcam capture) Planned (§6.4.3).
- Sign tool.
- Form Filling Toolbar.
- Password-protect / edit permissions.
- Reduce file size.
- Quartz-equivalent filters.
- Redaction (with warning copy).

### Phase 6 — Advanced raster + OCR *(In flight — ML core shipped, format/colour half unstarted)*

- Smart Lasso and Instant Alpha — shipped (ONNX Runtime + MobileSAM
  downloaded on first use). The in-flight `mystifying-proskuriakova`
  branch moves these from a modal dialog to direct in-document tool
  modes on `AnnotationOverlay`.
- Background removal — shipped via U²-Net (ONNX Runtime). In-flight
  branch routes it through MlScheduler with a sparkle "looks like a
  candidate" badge.
- Live Text on images — shipped via **PP-OCRv3** (PaddleOCR), not
  Tesseract as originally specced. The in-flight branch makes OCR
  results selectable directly on the document via SelectableTextLayer
  instead of dumped to a dialog.
- Embed Text on PDF export — Planned (the checkbox isn't in the
  Export As dialog yet).
- HEIC read support — shipped (loaded via the platform HEIC plugin).
- **OpenEXR, HDR (Radiance), RAW read support** — Planned, not
  implemented today (see §4.1).
- Image Description / alt text — Planned.
- Colour management (Assign Profile, Soft Proof, lcms2) — Planned.

### Phase 7 — Stretch *(Partial — partially scoped, not started)*

- 3D viewing (USD, Collada, OBJ, STL).
- Scanner support (SANE / WIA / ImageCaptureCore).
- Camera import (libgphoto2 / WIA / ImageCaptureCore).
- Photo location / map view.
- Local search index and command palette (see §9.4).

### Phase 8 — Polish, accessibility, distribution *(Partial)*

- Full keyboard shortcut audit (DESIGN.md §7 has been reconciled
  against `MainWindow.cpp`; rebinding the *Planned* shortcuts is
  the remaining piece — see TODO.md ## UI).
- Screen reader audit — not started.
- Localisation framework + at least English; community translations
  welcome — not started (the in-flight OCR work surfaces multi-
  language selection, raising the priority somewhat).
- Installers and signing — Linux DEB pipeline exists; macOS
  notarised `.dmg` and Windows MSIX/NSIS Planned (see
  [docs/cross-platform-sprint.md](docs/cross-platform-sprint.md)
  and [TODO-packaging.md](TODO-packaging.md) for the gating
  Apple Developer Program decision).
- User-facing documentation site — not started.

---

## 11. Testing strategy

### 11.1 Unit tests

- Every command in the command pattern has unit tests covering apply / undo /
  re-apply.
- Document model invariants tested under random edit sequences (property-based
  testing).

### 11.2 Format conformance

- A corpus of test files for each supported format (the public test corpora
  for PDF — e.g., the PDF Association's test files — plus a hand-curated
  image set covering colour spaces, alpha, animation, EXIF, ICC).
- Round-trip tests: open → save → reopen → byte-compare metadata where
  appropriate.

### 11.3 UI / integration tests

- Cross-platform UI tests via Qt Test or Squish (or equivalent open-source
  tooling).
- Smoke test on each platform on every CI run: open ten reference files,
  navigate, mark up, save, close.

### 11.4 Performance benchmarks

- Tracked baselines for: cold-start time, PDF first-page render, 100 MP image
  load, continuous-scroll FPS over a 500-page PDF, OCR throughput.
- Regression alerts when any metric worsens by more than 10%.

### 11.5 Accessibility

- Automated axe-style audits run in CI against every screen.
- Manual screen reader walkthrough each release.

---

## 12. Distribution and release engineering

- **Versioning:** SemVer. Breaking changes only at major bumps.
- **Release cadence:** Roughly monthly point releases during the first year,
  quarterly thereafter.
- **Update mechanism:**
  - macOS: Sparkle.
  - Windows: built-in updater (MSIX takes care of this) or Squirrel.
  - Linux: leave to the distribution; AppImage uses AppImageUpdate.
- **Code signing:** Required on Mac and Windows. Build infrastructure must
  support reproducible-from-source signed builds.
- **Telemetry:** **None by default.** If a maintainer ever wants to add it,
  it must be opt-in, transparent about exactly what is sent, and disable-able
  in one click.

---

## 13. Open questions for the implementing agent

The following should be resolved early in Phase 0 and the resolutions should
update this document.

1. **Qt 6 with C++ vs. PySide6.** Pick one and commit. (Recommendation: C++
   for performance; PySide6 if the team is small and time-constrained.)
2. **Poppler vs. MuPDF for PDF rendering.** Both work. Poppler is GPL/LGPL;
   MuPDF is AGPL/commercial. Licence implications drive the choice.
3. **Background-removal model.** Bundle vs. on-demand download. Bundling
   costs ~50–200 MB on disk; downloading requires explicit consent on first
   use. The latter aligns better with our local-first principle.
4. **Map tile provider.** OpenStreetMap public tiles have a usage policy that
   may not suit a desktop app shipping at scale. Alternatives: bundle an
   offline tile pack (large), or self-host a tile server (operational
   burden), or punt and just open a URL in the browser.
5. **3D viewer.** Building this in-app vs. embedding an existing viewer.
   USD's reference Hydra renderer is an option; a simpler scenegraph using
   Open3D may suffice.
6. **Stylus / pressure on Linux.** `QTabletEvent` works on X11 with
   xf86-input-wacom; Wayland support is improving but uneven. Plan for
   graceful degradation.

---

## 14. Glossary

- **Annotation:** A non-destructive overlay added by the user to a document
  (a note, highlight, drawn shape, signature). Stored in the PDF's annotation
  dictionary; preserved across save/load unless the user explicitly flattens
  the document.
- **Auto-save:** Continuous, debounced persistence of document state to disk.
- **Browse All Versions:** Time-machine-style UI for picking a historical
  version of a document to restore.
- **Document hash:** A stable identifier for a file, computed from its bytes
  at the time Trailer first opens it. Used as a key in the version store.
- **Flatten:** Bake annotations into the page content so they cannot be
  removed without losing pixels too.
- **Inspector:** A side-panel that shows metadata and structure for the
  current document.
- **Markup:** The set of annotation tools and the toolbar that exposes them.
- **My Card:** A user-defined record of personal information (name, address,
  etc.) used to AutoFill PDF forms.
- **Quartz-equivalent filter:** Trailer's term for a colour / processing
  filter applied at export time, modelled on the prior art's Quartz Filters.
- **Soft proof:** Display an image with the colour transformation a target
  device would apply, without modifying the image file.
- **Version:** A snapshot of a document at a point in time, retained in the
  local version store and accessible via Browse All Versions.

---

*End of design document.*
