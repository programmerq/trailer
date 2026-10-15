# PRODUCT.md: what Trailer is

What Trailer is, who it is for, and the rules that decide what it does. How to work on the
code: [`AGENTS.md`](AGENTS.md). Open work: [`TODO.md`](TODO.md).

## What it is

Trailer is macOS Preview for macOS, Windows and Linux. It opens PDFs and images instantly and
lets you read, search, mark up, sign, fix and lightly edit them, with no setup. The bar: on
every OS it is as good as Preview is on a Mac, so one set of habits works everywhere.

- **Who:** today the owner (the maintainer), daily, on all three OSes. Later a wide audience who
  must not go "oof", so the defaults are right with zero configuration.
- **Hardware yardstick:** the owner's ~12-year-old Intel Mac (a Core i3, 2014-era). The
  performance rules below must hold there. Official macOS builds can't run on a Mac that old
  today (arm64 only, macOS ≥ 14); see Open decisions.
- **AI is progressive enhancement:** OCR, background removal and Smart Lasso make Trailer better
  when present; nothing else depends on them.
- **Not** a Photoshop or Acrobat replacement, a cloud service, a collaboration tool, or a mobile
  or web app.

## Priorities, first to last

### First: files are safe, and the basics work

The failures to stamp out, from daily use: files saved when they shouldn't be (or not when they
should), undo that doesn't undo, marks and selections that land somewhere other than where you
pointed, icons jumping, status-bar clutter, and unavailable options explained by popups.
Silent damage to a file comes first, because it looks fine inside Trailer until another app
opens the file.

- **Save** writes exactly what you see, once, only when you ask, and never leaves the file
  missing or half-written. Saving an unedited document changes nothing on disk.
- **Undo** keeps one history per document, and Save doesn't erase it. ⌘Z reverts the most recent
  operation of any kind (markup, page operation, pixel edit); Redo re-applies it. Undoing back to
  the saved state makes the document clean again.
- **What you point at is what you get.** A drag selects the text under it; a mark is saved where
  it was drawn, in every view mode, at every zoom, on every OS.
- **The furniture doesn't move.** No control changes position because unrelated state changed.
  Toolbars don't reflow when a sibling hides, and menus never reorder.
- **The document is the subject.** No permanent chrome repeats what the page already shows: no
  zoom % readout, no "saved" toast, no panel labelled with its own name, no permanent status
  bar. Chrome that appears only while its state is true is fine. So is a layout change the user
  caused directly.
- **No lying controls.** A control that can't act now is dimmed, not removed, with a tooltip
  saying why, and where to go when there's somewhere to go. Standard edit commands (Undo, Redo,
  Cut, Copy, Paste) just dim, as in every app. Menus keep every item in place and dim what
  doesn't apply to the document showing, as Preview does. The one exception: a toolbar may leave
  out a tool that never applies to its document type, as long as no other control moves, also
  when the user switches between a PDF tab and an image tab. Never quietly do something other
  than what was asked; dropping a failed result the user can retry is fine. No disabled
  placeholders for features that don't exist.
- **Popups only for decisions:** picking a file or page range, confirming irreversible loss,
  one-time consent (a model download), and errors the UI can't make obvious. Never use one to
  narrate what the user just did, or to say why something is unavailable.
- **Document over dialog.** Results land on the page (OCR text is a selectable layer).
  Parameters are set on the content (drag to crop) before reaching for a modal.
- **Settings apply immediately**, with no restart. **Error messages say what to do next.**

### Then: performance by construction

Page one should appear as soon as the file allows: the same for 1 page as for 100,000. Open,
parse and display overlap instead of running in sequence. Stated so a test can check it, for any
document size N:

1. Before the first frame, only the visible pages and a fixed prefetch (its size doesn't depend
   on N) are touched.
2. The GUI thread never calls the PDF engine or reads files: not on open, paint, scroll, zoom,
   resize or a mode switch, and it never waits on a worker.
3. Each scroll, zoom or resize handler does O(visible pages + log N) work.
4. Bytes read before the first frame are the 1-page file's bytes plus what the file format
   forces the engine to read (the cross-reference table) plus a constant.

What this can't promise: a non-linearized PDF keeps its cross-reference table at the end of the
file, and the engine parses all of it before page one, so a 100,000-page file costs a little
more than a 1-page one (about 0.15 s of CPU with QtPdf). A damaged file needs a full scan. The
GUI thread may also keep a small record per page (page sizes for scrolling), built in a few
instructions each (TODO.md T-24 sets the bound). None of this may block the GUI thread.

- Everything beyond page one runs lazily near the visible page, off the GUI thread, and can be
  cancelled.
- Idle means idle: no polling, busy loops or background work the user didn't cause. Background
  work pauses on battery unless the user allows it.
- Binary size and memory stay frugal: no bundled asset or dependency without a reason in its PR.
  The ML runtime and its model weights are the one exception.
- Compare against "the same as a 1-page file" and, on a Mac, Preview on the same machine; never
  against invented millisecond budgets.

### Then: platform-native shape

- **macOS:** the global menu bar holds every command, and its set of menus never changes.
  Unavailable items dim. Labels are title case, with "…" only when more input follows. With no
  document there is no window (Dock and menu bar only), and no Open panel appears by itself.
  Closing the last window doesn't quit.
- **Windows and Linux:** in-window menu bar. With no document, an empty window shows Open, a
  Recent list and a centred drop target. Its menus and toolbars stay, with their controls
  disabled. Closing the last window quits.
- **Same features everywhere.** An OS may change a feature's shape, never remove it.
- **Shortcuts:** one table, ⌘ on macOS = Ctrl elsewhere; standard verbs keep standard keys.
  Cancel is ⌘. / Esc. ⌘N New from Clipboard, ⌥⌘G Go to Page, ⌘0 Actual Size, ⌘9 Fit Page,
  ⌘1/2/3 Continuous / Single Page / Two Pages, ⌥⌘Q Quit and Keep Windows.
- One window per document by default. Several images opened together share a window as tabs,
  and with "Open files in: New Tab" any mix of documents shares one window.

## What it does, ranked

### Tier 1: the Preview core (must be excellent)
1. **View:** PDF, PNG, JPEG, GIF (including animated), TIFF, WebP, BMP and HEIC (HEIC doesn't
   open yet). Continuous, Single Page and Two Pages layouts, each with full text selection,
   search and markup; fit page, fit width, actual size and pixel-exact zoom; thumbnails, table
   of contents and go to page. Annotations made by other apps are shown. Trailer remembers your
   place in each file and can be the default viewer on all three OSes.
2. **Text:** select, copy and search text in every view mode. Native PDF text and OCR'd scans or
   images behave the same.
3. **Markup:** rectangle, ellipse, line, arrow, ink, text box, note, and highlight / underline /
   strikeout. Restyle a mark in the Inspector. Marks are saved as real, still-editable PDF
   annotations, and copying or printing a page includes them.
4. **Pages:** rotate, delete, reorder, insert from file, combine, and crop by dragging on the page.
5. **Image edits:** rotate and flip baked into the pixels, crop, resize, colour adjust, export
   and convert. Saving keeps the photo's metadata (orientation reset) and never re-encodes below
   the source quality.
6. **Screenshots** (core; must just work): whole screen, window and selection on every OS, plus
   New from Clipboard. Off macOS, only whole-screen capture works today.
7. **Files:** Save, Save As, Export, Print, open and edit password-protected PDFs,
   password-protect, reduce size.
8. **Forms and signatures:** fill AcroForms, type anywhere on flat PDFs, capture and place
   signatures, and redact. Filled values show in other viewers. Redaction removes the content
   under the box on every page, rotated or cropped, and says so before it does. Editing a
   digitally signed PDF warns that the signature will break before anything is written.

### Tier 2: progressive enhancement (ML)
OCR (PP-OCRv3) comes first. Users expect to select text in any scan or screenshot, so OCR'd text
must behave exactly like native text, with no command to run. Then background removal (U²-Net)
and Smart Lasso / Instant Alpha (MobileSAM). All run locally. Today each model downloads once,
after consent, the first time its feature is used; its controls are disabled with a tooltip only
when that model is set to Never Download (see Open decisions for bundling the OCR model). Work
stays near the visible page, uses one low-priority worker, can be cancelled, pauses on battery,
and never substitutes a different result. Whole-document OCR runs only when the user asks for
it: Recognize Text, or a search that needs it. Target: Trailer builds and is fully usable without
ONNX Runtime.

### Tier 3: stretch
Read-only 3D viewing of OBJ and STL files, only after Tier 1 is done, and only if it opens as
fast as everything else.

### Not doing (now)
AutoFill / My Card; scanner and camera import; colour management and soft-proofing; HDR,
OpenEXR and RAW; a version browser, plugins, translation; USD / Collada; Office formats,
EPS / PostScript, audio and video; accounts, sync and collaboration. "We don't do that" is a
complete answer: a feature earns its place through a real workflow, not because Preview has
it. The UX session recorder is a developer tool, compiled out of every release build.

## Your files are safe: the rules

- Only an explicit Save or Save As writes your file. Every 30 s, unsaved edits go to a private
  recovery copy, and reopening the file restores them. Untitled documents (screenshots,
  clipboard) get no recovery copy yet (T-16).
- **Closing:** unsaved changes prompt Save / Discard / Cancel; a clean document never asks. ⌘Q
  asks once per unsaved document. ⌥⌘Q (Quit and Keep Windows) asks nothing and restores the
  unsaved work at the next launch; one case still asks (T-61).
- **File changed on disk:** a clean document reloads silently. With unsaved edits, a banner offers
  Reload or Keep mine. A save never overwrites a file that changed underneath it. If the file is
  deleted, your buffer stays and Save recreates the file.
- **PDF round trip:** Trailer edits nine annotation types (Square, Circle, Line, Ink, FreeText,
  Text, Highlight, Underline, StrikeOut) and keeps them editable. Annotations from other apps
  (stamps, polygons, links, popups, replies) stay in the file untouched, with their authors,
  dates and threads. Signatures and redactions are flattened into the page.
- **Known violations:** the rules above are what Trailer must do. Today it breaks several of them
  (duplicated marks, marks on the wrong page or spot, lost undo, unsafe in-place saves,
  redaction leaks, password-protected files going blank). The worst are P0 entries in TODO.md,
  most with a failing-first test, and they come before new features.

## Hard constraints
- No ads. No telemetry: no analytics, crash upload or usage statistics. No accounts, cloud sync,
  premium tier, DRM or real-time collaboration.
- No training on user content. All ML runs locally.
- Local-first: everything works offline except two opt-in paths. One is the model download,
  after consent. The other is the signed update check, off by default.
- MIT licence, never relicensed. Qt is linked dynamically (LGPL).
- Distribution stays cheap and independent. There is no Apple Developer Program membership, so
  macOS builds are not notarized, and updates are verified with Trailer's own ed25519 key.
- Every hand-tuned constant carries a comment: what it is, which range was tried, and what
  symptom would justify changing it.

## Distribution
- **macOS:** arm64 DMG for macOS ≥ 14.0 (ONNX Runtime's floor). It is not notarized. On first
  launch macOS refuses to open it until the user clicks Open Anyway in System Settings →
  Privacy & Security, or runs `xattr -dr com.apple.quarantine /Applications/Trailer.app`. That
  first-run friction is the price of the no-Apple-Developer-Program policy, not a bug, and the
  download page must say so.
- **Windows:** an MSI that registers file associations, and a zip.
- **Linux:** DEB and RPM, both with .desktop and MIME types, and a tarball (no .desktop yet). The
  DEB and RPM steps haven't run in a release yet (T-126).
- **Updates:** Check for Updates reads a signed nightly feed. macOS installs the update and
  relaunches; Windows and Linux don't install it yet (T-53). Tagged releases have no update
  channel yet (T-69). See RELEASING.md.

## When it's done: seven jobs
Trailer is the owner's default PDF and image app on macOS, Windows and Linux, and the owner
finishes each job below without wishing for Preview. Each job is checked on all three OSes at
dpr 1 and 2.

1. **Read a long PDF.** Opened from the OS (double-click or the default-app path), a 100+ page
   PDF shows page one as soon as the file allows (performance rules above). Continuous reading
   takes one key press or scroll gesture per page; Single Page and Continuous toggle from the
   keyboard; fit width, fit page, actual size and zoom in/out render sharply at dpr 2. Reopening
   the file, also after quitting Trailer, returns to the same page, zoom and scroll offset.
2. **Search a PDF.** One shortcut; matches highlighted with an accurate "N of M"; next and
   previous cycle through every hit and wrap; an absent phrase says "no matches". Text that
   exists only on an OCR'd scanned page is found too.
3. **Mark up a screenshot.** Add an arrow, a box, a text label and a highlight; restyle one in
   the Inspector; drag a line's endpoint. Copy the page, paste it into another app, and every
   mark appears as drawn. Reopening the saved file shows the same.
4. **Fix a photo's rotation.** Rotate a sideways photo and save. Another app shows it upright,
   and the round trip neither visibly degrades the image nor strips its camera metadata.
5. **Select text in a scan or screenshot.** Open it and, without running any command, drag
   across a line and paste the right characters into another app.
6. **Fill and sign a form.** Fill a PDF with real fields (including radio buttons and a
   multiline comment); type answers anywhere on a PDF with no fields; sign. Both files show every
   answer and the signature, correctly placed, in a browser's PDF viewer, Acrobat or a phone,
   and when printed.
7. **Combine and reorder pages.** Drag a second PDF onto the sidebar to merge it; reorder pages,
   two at once, by dragging; delete several; rotate a page that isn't current; save. After
   reopening, the page order matches the sidebar and every mark is on the page it was drawn on.

Before 1.0, also: Preferences covers every setting and each survives a restart; every command
works from the keyboard; every control has a screen-reader label.

## Open decisions (owner)
1. **Save model.** Keep the model above (explicit Save, a prompt on close, a recovery copy), or
   adopt Preview's: autosave into the file, no prompt on close, and Revert To → Last Opened
   backed by a guaranteed pre-edit copy. The current model stands until decided, and the P0
   save fixes come first either way.
2. **Old Intel Mac.** Official macOS builds can't run on it (Qt 6.10+ needs macOS 13, ONNX
   Runtime 1.25 needs 14, builds are arm64 only). Options: use it as the performance yardstick
   under Linux or Windows; or ship an Intel / older-macOS build with an older Qt and no ML.
3. **OCR model.** Bundle the Latin OCR weights (about 12 MB) so text in scans is selectable with
   no download and no consent prompt, or keep download-on-first-use for every model. Job 5 can't
   pass on a fresh install without bundling.
