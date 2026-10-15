# TODO

Trailer's one tracker: every entry is open work, and nothing else in the repo tracks work. What
Trailer won't do is in PRODUCT.md ("Not doing"), not here.

**Entries**
- Each entry has an id, a one-line title, tags, **Done when** (the pass/fail bar) and **Src**
  (the evidence). Some also have **Test**: a known-bug test that fails today (below).
- **Done when** names a state a reviewer can reach and observe. Where it says "at dpr 1 and 2" or
  lists view modes, every combination must pass.

**Closing and adding**
- To close an entry, the PR that meets its bar deletes it and names the id in the commit subject,
  e.g. `fix(T-07): …`. There is no status field and no done list.
- To add an entry, take the next unused id (ids are never reused; the highest so far is T-127)
  and put it under its priority. Land a failing test in the same PR when you can.
- A code comment that points at open work says `// T-NN: …` and nothing more.

**Known-bug tests**
- `tests/test_known_bugs_*.cpp` (ctest label `known-bug`, run by PR CI on Linux) assert the
  correct behaviour and mark today's failure with `QEXPECT_FAIL("", "T-NN: …", Abort)`.
- A fix makes that check pass unexpectedly, which fails the run. The fixing PR deletes the
  `QEXPECT_FAIL` line and this entry together, so the test becomes the regression test.

**Priorities**
- **P0**: data loss, a wrong save or undo, or a file that leaks what the user removed.
- **P1**: the named friction: controls that move, popups that say no, status-bar clutter, broken
  select/undo/zoom basics, text you can't select, HiDPI blur, slow open.
- **P2**: Preview parity and polish.
- **P3**: later, or stretch.

**Tags:** `open` (open, render, perf), `nav` (navigation, zoom), `text` (selection, text),
`annotations` (also page edits), `undo`, `save` (save, autosave, recovery), `chrome` (layout
stability), `dialogs`, `ocr` (OCR and ML), `screenshot`, `print`, `platform`, `packaging`
(packaging, release, CI), `infra` (tests and infrastructure), `docs`, `security`, and
`data-loss` where work can be lost.

**Sources (Src)**
- `file:line` refers to `09bc1e3` (origin/main, 2026-08-07). Removed files (the old TODO.md,
  ROADMAP.md, `docs/backlog/`, decision records) can still be read with `git show 09bc1e3:<path>`.
- Short ids such as MAP-1, SAV-3 or C7 name findings from the October 2026 audit, which lives
  outside the repo. Each entry repeats what you need from it.

## P0: data loss, wrong save or undo, leaks

- **T-01 PDF Save re-appends every annotation already in the file.** `save` `data-loss`
  - Done when:
    - three Saves, each made after adding one mark, leave exactly 3 annotations in the file,
      counted with a fresh `PdfEditor::readAnnotations()`;
    - a file that arrives with 2 annotations still holds 2 after a clean Save, and after
      Rotate + Save; Save As behaves the same;
    - a file carrying other apps' annotations (a Highlight with /Popup, a reply with /IRT, /T
      author, /NM, /RC rich text, /CA opacity, a Link) keeps its annotation count and those
      keys, through three clean saves and one unrelated edit.
  - The fix needs annotation identity: each annotation keeps its source object, and Save
    rewrites only new or changed ones and deletes only removed ones. Clearing all managed
    annotations and writing them again (what `writeRecoverySnapshot` does today) strips other
    apps' metadata, so it is not a fix; the recovery sidecar has the same loss today.
  - Test: `test_known_bugs_pdf_save` (checks tagged T-01).
  - Src: SAV-1, C6. Each Save writes the store on top of what the file holds: three mark-and-save
    rounds leave 1, 3, then 7 marks; a clean ⌘S doubles them. Other apps' annotations keep their
    keys, but Save appends stripped copies of them (6 → 10 on the test file).
    `PdfAdapter.cpp:2596-2604`; `PdfEditor.cpp:1030-1110`; `Annotation.h:99-113` (no source object
    id, author or dates).
- **T-02 Deleting, moving or inserting pages leaves marks on the wrong page, and Save then drops
  or misplaces them.** `annotations` `data-loss`
  - Done when: take a 3-page PDF with marks on pages 1 and 3.
    - Deleting page 1 removes page 1's mark and shows the page-3 mark on the new page 2: in the
      view, in the annotation store and in the saved file.
    - Moving page 1 to the end carries its mark with it.
    - Inserting a page at the front shifts both marks by one page.
    - ⌘Z after each operation restores the previous placement.
  - Test: `test_known_bugs_pdf_save` (T-02).
  - Src: page operations never touch the annotation store (`PdfAdapter.cpp:2259-2276`,
    `:2324-2343`, `:2345-2362`). After deleting page 1 and saving, page 1's mark lands on the new
    page 1, page 3's mark is dropped from the file, and the store keeps it on a page that no
    longer exists.
- **T-03 Marks on rotated pages are saved away from where they were drawn.** `save` `data-loss`
  - Done when: a rectangle drawn at the visible top-left of a page rotated 90°, 180° and 270°
    shows at the same visible spot when pdfium renders the saved file with annotations on, and
    Trailer re-reads it at that spot. The same holds on a cropped page.
  - Test: `test_known_bugs_pdf_save` (T-03).
  - Src: drawn at (20,20, 100×60), pdfium paints (762,20, 60×100). `PdfEditor.cpp:1030-1068`
    ignores /Rotate and the CropBox.
- **T-04 Zoom Lens marks vanish when a PDF is saved.** `save` `data-loss`
  - Done when: a Zoom Lens drawn on a PDF is still present and editable after Save and reopen.
    Until that works, the tool is disabled on PDFs with a tooltip saying it works on images.
  - Test: `test_known_bugs_pdf_save` (T-04).
  - Src: the store goes 1 → 0 on Save. `PdfEditor.cpp:674-678`.
- **T-05 ⌘Z of a page operation after Save does nothing, marks the document edited, and can
  make it unsaveable.** `undo` `data-loss`
  - Done when: for delete, move, rotate, insert and crop, the sequence op → Save → ⌘Z restores
    the pages exactly as before the op, and ⌘⇧Z re-applies it. `undo()` returns false whenever
    nothing was reverted. Save never fails after an undo. A symmetry test over every PdfCommand,
    run across a Save, proves this.
  - Test: `test_known_bugs_pdf_save` (T-05; delete and move).
  - Src: UND-1, UND-2. After Delete → Save → ⌘Z, `undo()` returns true, nothing changes, qpdf
    warns "operation for dictionary attempted on object of type destroyed", and Save, Save As and
    the recovery snapshot all fail from then on. After Move → Save → ⌘Z, ⌘⇧Z moves the page
    again (a third order) and Save As writes it. Rotate, insert and crop undo correctly across a
    Save today; delete and move don't.
    - `PdfAdapter.cpp:2206` ignores `revert()`'s result.
    - The commands hold objects of the editor that `saveCommitOnUi` replaces
      (`PdfCommands.h:66-78`; `PdfAdapter.cpp:2634-2735`).
- **T-06 On an image, ⌘Z after Save can never remove a mark.** `undo` `data-loss`
  - Done when: on a PNG, the sequence draw a rectangle → Save → ⌘Z removes the rectangle from the
    displayed pixels, and ⌘⇧Z restores it. A Save after the ⌘Z writes the file without it. After
    Save, marks are still selectable and editable.
  - Test: `test_known_bugs_image` (T-06).
  - Src: UND-5. Save burns the mark into the pixels and clears it from the model
    (`ImageAdapter.cpp:1383-1391`). ⌘Z re-adds the mark over the burned-in pixels; a second ⌘Z
    leaves the pixels red with nothing left to undo.
- **T-07 After a tab is dragged, commands act on the other document.** `save` `data-loss`
  - Done when: in a two-tab image window, after dragging the first tab to second place, Save,
    Rotate, ⌘Z, Select All, Close and Export each act on the selected tab's document. Test: after
    `tabBar()->moveTab()`, `currentDocument()` matches the selected tab at every index.
  - Test: `test_known_bugs_window` (T-07).
  - Src: UND-9. `DocumentView.cpp:7` calls `setMovable(true)` and nothing handles `tabMoved`.
    `DocumentView.cpp:27-33` looks documents up by index. Opening several images at once makes a
    tabbed window, so this is reachable by default.
- **T-08 A failed or cancelled PDF Save leaves the open document half-saved, and Cancel doesn't
  stop Save As.** `save` `data-loss`
  - Done when: an injected write failure, and Cancel pressed mid-save while that button exists,
    each leave the open document with the same marks, page count and undo history as before. No
    file appears at a Save As target. The next good Save writes each mark once.
  - Src: `MainWindow.cpp:3247-3268` returns on `wasCanceled`, but the `QtConcurrent::run` worker
    keeps going. `PdfAdapter.cpp:2596-2604` mutates the live editor; `:2626-2630` writes the
    Save As target directly. Inferred from the code, not reproduced.
- **T-09 Fit Width plus navigation, view-mode toggle or resize can freeze the UI at 100% CPU.**
  `nav` `data-loss`
  - Done when: these seeds of the random UI walk pass its per-step quiescence check at dpr 1 and
    2, and so do 200 fresh seeds:
    - the recorded seeds 720458898, 84349732 and 269782153;
    - the random-walk seeds 3116271505, 516731256, 1970968076, 1628323449 and 2603950034.
  - Src: the walk and its oracle are on branch `claude/trailer-coordinator-elk85x` (commits
    `04429e4`, `1ba553a`, `0da986d`). gdb shows the GUI thread looping in
    `QAbstractScrollAreaPrivate::layoutChildren_helper → postEvent`. Force-quitting loses
    unsaved work.
- **T-90 Redaction leaves the text readable on rotated and cropped pages.** `security` `data-loss`
  - Done when: red text under a redaction box leaves 0 red pixels in the saved file at /Rotate
    0, 90, 180 and 270 and on a page with a CropBox; the page keeps its orientation; and a page
    that can't be rendered makes Save fail loudly instead of being skipped.
  - Test: `test_known_bugs_pdf_files` (T-90).
  - Src: C1. Red pixels before → after: unrotated 1,806 → 0; /Rotate 90 1,798 → 1,747 (readable,
    page upside down, box on blank paper); cropped 1,806 → 2,843; /Rotate 180 covered but the
    rotation is lost. The redacted page's text layer is gone, so searching the saved file won't
    reveal the leak. `PdfEditor.cpp:965-1016`: raster size from the unrotated MediaBox, box
    painted at displayed coordinates, a failed render silently skips the page.
- **T-91 Password-protected PDFs go blank after any page operation, and Save misreports.**
  `save` `data-loss`
  - Done when: with a 3-page AES-256 PDF opened with its user password, Rotate keeps
    pageCount 3 and the document dirty; Save returns true; reopening with the password shows the
    rotation; the recovery snapshot succeeds; redaction works.
  - Test: `test_known_bugs_pdf_files` (T-91).
  - Src: C2. After Rotate, pageCount becomes 0 (blank view) and Save returns false even though it
    has already rewritten the file. `PdfAdapter.cpp:2122-2125` (the viewer reload drops the
    password), `:2690-2700` (the post-save editor reloads without unlocking);
    `PdfEditor.cpp:933`. Owner-password-only files work.
- **T-92 In-place Save deletes the file before replacing it, breaks symlinks, and resets
  permissions.** `save` `data-loss`
  - Done when:
    - killing the process at every step of a save (fault injection at each file operation)
      always leaves either the complete old file or the complete new one at the path;
    - saving through a symlink updates the target and leaves the link a link;
    - mode 0600 stays 0600; extended attributes and hard links are preserved;
    - image saves get the same treatment (write a temp file in the same directory, then rename).
  - Test: `test_known_bugs_pdf_files` (T-92; symlink and mode).
  - Src: C3, SAV-4. The order is `unlink(target)` → `link(temp, target)` → `unlink(temp)`, so the
    path is empty in between; with TMPDIR on another filesystem the bytes are copied while it
    is empty. A 0600 file becomes 0644. If the step after the remove fails, `restoreOnFailure`
    reloads the deleted path and the document vanishes. `PdfAdapter.cpp:2609-2610`, `:2663-2682`;
    `TempPath.h:24-25`; `ImageAdapter.cpp:1384-1391` writes in place.
- **T-96 Save rewrites unedited files, and JPEG Save re-encodes and drops EXIF.** `save` `data-loss`
  - Done when: ⌘S on an unedited PDF or image leaves the file's bytes and mtime unchanged. Saving
    an edited JPEG keeps its EXIF (orientation reset to 1), ICC profile and resolution, and does
    not re-encode below the source quality.
  - Test: `test_known_bugs_window` and `test_known_bugs_image` (T-96).
  - Src: SAV-3, C7. `onSave` has no dirty gate (`MainWindow.cpp:3153-3171`). A JPEG is
    re-encoded at Qt's default quality on every save (55,412 → ~12,400 bytes; error grows with
    each save), and EXIF goes from 5 tags to 0 (`ImageAdapter.cpp:1385-1387`).
- **T-102 Marks and text selection land on different content than the user pointed at.**
  `text` `annotations` `data-loss`
  - Done when, through MainWindow with real mouse events at positions found from rendered pixels,
    for {Fit Page, Fit Width, 100 %, 50 %, 400 %} × {Single, Continuous, Two Pages} × logical
    DPI {72, 96} × dpr {1, 2} × {document shorter and taller than the viewport}:
    - a drag with the default tool from 2 px inside a word's left edge to 2 px inside its right
      edge puts exactly that word on the clipboard;
    - a rectangle drawn 6 px outside the word, then saved, is stored containing the word's
      bounds with ≤ 2 pt slack per side.
  - Test: `test_known_bugs_window` (T-102; Fit Page at DPI 96 and 72).
  - Src: MAP-1..3. In the default view (Fit Page) a drag across "INVOICE" copies "VOICE", and a
    box drawn around it is saved 31 pt right and 25 pt up. On Windows and Linux every zoom is
    wrong, by about 750 px on page 3. `PdfAdapter.cpp:858-923`: page points are mapped with
    `zoomFactor()`, which stays 1.0 in fit modes; QPdfView also scales by
    `logicalDotsPerInch / 72`, which `src/` never reads; and an `extraY` term assumes vertical
    centring that QPdfView doesn't do.

## P1: the friction that was named

- **T-10 Every PDF Save opens a window-modal "Saving…" dialog.** `dialogs`
  - Done when: saving a PDF creates no dialog (test: no top-level QDialog appears between Save and
    completion). The document stays scrollable while it writes, and the edited indicator clears
    when the write lands. A save that takes over a second shows progress without a modal.
  - Src: NO-7. `MainWindow.cpp:3232-3236`: QProgressDialog, WindowModal, `setMinimumDuration(0)`.
- **T-11 The status bar is always shown, with success toasts and reserved slots that jump.**
  `chrome`
  - Done when: `statusBar()` is not visible in the empty, PDF, image and markup states on any OS,
    and no text appears after a successful action ("✓ Saved.", "Page cropped.", "Page copied…",
    "Trailer reset complete."). Anything that must still be shown appears in context and only
    while its state is true (errors: T-111).
  - Test: `test_known_bugs_window` (T-11).
  - Src: SB-1, SB-2. Four fixed-width reserved slots take 1,045 px of a 1,280 px window, empty at
    rest (`MainWindow.cpp:176-185`, `:636`, `:778`, `:885`, `:898`), and jump 1–4 px vertically
    when one fills. `flashSuccess` at `:2293`, `:3230`, `:3302`, `:3585`, `:3685`, `:5495`.
- **T-12 Find moves the toolbar, opens the sidebar, and leaves it open.** `chrome`
  - Done when: ⌘F → type → Esc changes no main-toolbar control's or the document view's
    `geometry()` at any step; the sidebar is visible after Esc iff it was before ⌘F; at window
    widths ≥ 900 px the search field is visible at rest. Asserted in Continuous, Single and Two
    Pages at dpr 1 and 2.
  - Test: `test_known_bugs_window` (T-12).
  - Src: JMP-3, JMP-5. The toolbar grows 33 → 39 px, all icons shift, the search control moves
    1250 → 889 px, and the sidebar opens before anything is typed (document +266 px).
    `MainWindow.cpp:1616-1660`; `:1626-1631` hides the widget but not its QWidgetAction.
- **T-13 Tools ▸ Take Screenshot is a modal chooser with greyed options, duplicating File ▸
  Screenshot.** `dialogs` `screenshot`
  - Done when: the only entry point is File ▸ Take Screenshot ▸ From Selection… / From Window… /
    From Entire Screen, with Preview's names. Each item starts its capture with no intermediate
    dialog. Tools ▸ Take Screenshot and its chooser are gone.
  - Src: NO-1. `MainWindow.cpp:2188` and `:3083-3151` (radio buttons disabled off macOS);
    `Application.cpp:1326-1395`.
- **T-14 The UI shows disabled placeholders.** `dialogs`
  - Done when: the File menu has no Scanner or Camera items; the file-changed banner has no
    disabled Compare button; Preferences offers no disabled Stable channel; Share… is absent
    where it can't work.
  - Src: NO-3. `Application.cpp:1391-1397`; `FileChangeBanner.cpp:36` and `:57-64`;
    `PreferencesDialog.cpp:329-345`.
- **T-15 Window and selection screenshots don't work on Windows or Linux.** `screenshot` `platform`
  - Done when: on Windows 11 and on Linux (GNOME Wayland through the screenshot portal, and X11),
    From Window… and From Selection… capture into a new untitled document at the screen's native
    pixels. This is checked on a real desktop at dpr 1 and 2. The "isn't available on this
    platform yet" tooltips are gone.
  - Src: `Application.cpp:1361-1368`.
- **T-16 Untitled screenshots and clipboard documents are lost on a crash.** `save` `data-loss`
  - Done when: take a screenshot document and a New from Clipboard document, each with one mark,
    and kill Trailer with SIGKILL 35 s after the edit. At the next launch both come back with the
    image and the mark, still untitled.
  - Src: `MainWindow.cpp:1049`, where auto-save skips untitled documents.
- **T-17 ⌘A selects annotations instead of text, and can act on a background tab.** `text`
  - Done when:
    - With focus on the page, ⌘A selects all text in the document and ⌘C copies it. This holds
      for native and OCR text, in Continuous, Single and Two Pages (decided: match Preview).
    - With focus in the thumbnails, ⌘A selects all pages.
    - In a two-tab window, ⌘A acts only on the visible tab.
  - Src: `MainWindow.cpp:1570-1575` calls `findChild<AnnotationOverlay*>()->selectAll()`.
- **T-18 Copy Page as Image leaves out the page's marks.** `text`
  - Done when: Edit ▸ Copy Page as Image on a page with a rectangle and a text box puts an image
    on the clipboard that shows both marks, at page size × screen dpr. Checked at dpr 1 and 2.
  - Src: `MainWindow.cpp:3672-3685` uses `renderThumbnail`, which paints the raw page.
- **T-19 Scanned PDFs are never OCR'd automatically.** `ocr`
  - Done when:
    - opening an image-only PDF, with the OCR model installed and background recognition on,
      makes the visible page's text selectable with no user action;
    - a born-digital PDF triggers no OCR; a mixed PDF OCRs only its image-only pages;
    - the missing-model hint follows the same per-page rule;
    - Recognize Text… no longer opens a page-range modal.
  - Src: OCR-3, NO-6. `hasTextLayer()` returns `m_valid` (`PdfAdapter.h:139`), so the gates at
    `OcrController.cpp:76` and `:280-284` stop every PDF. A per-page `pageHasText()` exists at
    `PdfAdapter.cpp:1621` but runs on the GUI thread. `MainWindow.cpp:2818-2841`.
- **T-20 OCR'd text selects whole blocks, not words.** `ocr` `text`
  - Done when: dragging across two words of an OCR'd line selects and copies exactly those two
    words, as native text does at the same spot. Checked in Continuous, Single and Two Pages at
    dpr 1 and 2.
  - Src: `SelectableTextLayer.cpp` does no word splitting.
- **T-21 Two Pages is read-only (no markup, text selection or Find) and hides the markup
  toolbar.** `text` `annotations`
  - Done when, in Two Pages at dpr 1 and 2:
    - every markup tool enabled in Single is enabled, and each mark lands on the correct page of
      the spread (round-trip checked in Single);
    - selecting text on either page copies only that page's text;
    - Find highlights each match on its page and scrolls that spread into view;
    - switching among Single, Continuous and Two Pages never changes any toolbar's visibility or
      the document view's y-position;
    - the Read-only badge and the "switch to Single" tooltip are gone.
  - Test: `test_known_bugs_window` (T-21; toolbar visibility).
  - Src: JMP-4, SEL-7. `MainWindow.cpp:862-885`, `:4655-4693`; `TwoPageView` has no mouse or key
    handlers. Decided: keep Two Pages and fix it on the new page surface rather than remove it.
- **T-22 Changing zoom mode loses your page.** `nav`
  - Done when: open a 200-page A4 PDF in a 900×700 window, choose Continuous and Fit Page, go to
    page 140, then choose Fit Width. `currentPage()` is 140, and the scroll position equals a
    settled go-to-page-140 under Fit Width. The same holds for every zoom-mode change and every
    switch among Continuous, Single and Two Pages, at dpr 1 and 2.
  - Src: was `docs/backlog/2026-08-05-zoom-mode-change-drops-the-current-page.md`.
- **T-23 The window waits for the whole PDF to load before its first paint.** `open`
  - Done when: with every file read blocked, `openFiles()` returns, a posted event runs, and the
    "Loading…" placeholder paints before the read is released. `addDocument` makes no
    `pageCount()` call, not even inside recorder arguments (T-100).
  - Src: `MainWindow.cpp:5250-5253`; `PdfAdapter.cpp:781-786`; `src/uxrecord/UxRecord.h:53`.
- **T-24 GUI-thread work grows with page count: layout, sidebar and thumbnails.** `open`
  - Done when these counting tests (no clocks) pass in Continuous, Single and Two Pages:
    - the pages touched before the first frame are the same set for 1 and for 20,000 pages;
    - the GUI thread makes 0 engine calls across open, 3 scroll steps, 1 zoom, 1 resize and a
      mode switch;
    - page-record visits per scroll, zoom or resize event are at most visits(N=1) + 4·⌈log2 N⌉;
    - Two Pages relayout makes 0 `pagePointSize()` calls;
    - the GUI thread's instruction count to the first frame grows by less than about 100
      instructions per page between 20,000 and 40,000 pages (callgrind, `--separate-threads`).
  - Src: QPdfView's Continuous layout walks every page on open, zoom and resize; Trailer adds
    eight `pagePointSize` walks per page. `PdfAdapter.cpp:815-1471`; `TwoPageView.cpp:134-162`;
    `Sidebar.cpp:316`; `ThumbnailModel.cpp:190-218`. `tests/test_perf_page_geometry.cpp:103`
    asserts the O(pages) behaviour and must be inverted.
- **T-25 Native text ingest re-parses the page once per line, on the GUI thread.** `open`
  - Done when: on open, native-text ingest runs off the GUI thread with one `getAllText()` per
    page; per-line queries remain only as a fallback; and every line's rectangle is within 1 pt
    of the exact per-line extraction on a corpus with superscripts and subscripts, two columns,
    rotated text, right-to-left text, ligatures and an OCR'd scan.
  - Src: `PdfAdapter.cpp:1631-1690` (`:1672` runs once per line); about 0.3 s on a 20-page file.
    A prototype that takes one `getAllText()` per page assigns wrong rectangles to lines with
    superscripts, which is the T-102 class of bug, so it needs the corpus gate above.
- **T-26 Opening a large image or animated GIF blocks the GUI for the full decode.** `open`
  - Done when: with the image decode gated, `openFiles()` returns and the GUI thread has not run
    `ensureDecoded()` before the gate is released. The badge scorer runs after the first paint,
    off the GUI thread. Animated GIF frames decode off the GUI thread.
  - Src: `MainWindow.cpp:3791-3822` and `:4297`; `ImageAdapter.cpp:302-311`.
- **T-27 Images too large for Qt's default 256 MB decode limit don't open.** `open`
  - Done when: a 96 MP JPEG and a 20,000 × 20,000 PNG open and display, decoding scaled when
    needed. The allocation limit is set explicitly, with a rationale comment.
  - Src: `ImageAdapter.cpp:348-352`.
- **T-28 Perf tests skip or race instead of counting.** `infra`
  - Done when:
    - the counting tests in T-23, T-24 and T-26 run in PR CI without the `uat` label, each
      marked QEXPECT_FAIL until its fix lands;
    - the QSKIP at `test_perf_render_ordering.cpp:114` is gone;
    - `test_perf_lazy_open` passes 50 of 50 runs alongside a parallel build.
  - Src: `test_perf_lazy_open` failed 2 of 45 runs under load.
- **T-29 Saving a PDF wipes its markup undo history.** `undo`
  - Done when: add a mark, rotate a page, Save, then ⌘Z twice: the rotate is reverted, then the
    mark, and the document is marked edited; ⌘⇧Z restores both. The same holds after Save As.
  - Test: `test_known_bugs_pdf_save` (T-29).
  - Src: UND-3. `PdfAdapter.cpp:2725` calls `clearHistory()` for annotations but keeps page-op
    history, so ⌘Z after Save skips the newest mark and undoes an older rotate.
- **T-30 No Trailer build runs on the reference Intel Mac.** `platform`
  - Done when: the owner's choice (PRODUCT.md, open decision 2) is in place. Either a release DMG
    launches and opens a PDF on the reference Intel Mac (x86_64, ML off, which needs T-49), or
    the counting tests (T-24) and the cold-launch measurement (T-124) run on that machine under
    Linux or Windows with the results recorded.
  - Src: Qt 6.10+ needs macOS 13 and ONNX Runtime 1.25 needs 14, while 2014 Macs stop at
    macOS 11 or 12. `cmake/OnnxRuntime.cmake:37-38` is FATAL on macOS x86_64.
    `scripts/build-macos.sh:2-17` is arm64 only, and `:68` sets the floor to macOS 14.0.
- **T-58 CI tests Windows only under Wine, and most of CI guards process, not the product.**
  `packaging` `infra`
  - Done when:
    - every PR builds and runs the unit tests natively on Linux, Windows (MSVC) and macOS, and
      UAT on Linux; release UAT runs on all three;
    - the Wine and mingw lanes, the unused runner image, `dev-build.yml`, the Docker UAT, the UAT
      ratchet and the release-script self-tests are gone, along with their workarounds
      (`kWineSkip`, the configure-time font directory);
    - the gating lanes build the shipped configuration (recorder off);
    - main stays green on 10 consecutive pushes, and a re-run never flips a result.
  - Src: about 4,400 lines of workflow YAML for about 2,300 lines of logic. The disabled MSVC job
    (`ci.yml:506-596`) passed 36 of 37 runs before it was turned off; the Wine lane fails about 1
    run in 3 and Wine UAT passes about half its cases. Main's last CI run is red from a Wine
    flake. `tests/test_ml_callback_lifetime.cpp:271`; `tests/CMakeLists.txt:25`;
    `tests/uat/CMakeLists.txt:19`.
- **T-94 Annotations made by other apps are invisible in Trailer.** `annotations`
  - Done when: a /Stamp and a /Polygon with appearance streams show in the view, thumbnails, Copy
    Page as Image and print.
  - Test: `test_known_bugs_pdf_files` (T-94).
  - Src: C4. No file in `src` sets `QPdfDocumentRenderOptions::RenderFlag::Annotations`. Save
    and recovery keep these annotations; they are just never drawn.
- **T-95 Print leaves out annotations, signatures and unsaved marks, and rasterizes on the GUI
  thread.** `print`
  - Done when: printing to PDF a document with a saved mark and an unsaved mark contains both;
    images print with their marks; the GUI thread stays responsive while printing (counted).
  - Test: `test_known_bugs_pdf_files` (T-95).
  - Src: C5. `PdfAdapter.cpp:2052-2099` renders `m_doc` with default flags; `ImageAdapter.cpp:
    1517-1538` prints `m_image` without the overlay.
- **T-97 Page operations cost O(document) on the GUI thread.** `open`
  - Done when: rotate, delete and undo make O(1) engine calls on the GUI thread at N = 20,000
    pages (counted at the engine boundary), and the first edit doesn't block on a whole-file
    parse.
  - Src: C8. One rotate takes 150 ms at 20,000 pages and 1,048 ms at 100,000; the first edit adds
    a lazy qpdf parse (776 ms / 3,249 ms). `PdfAdapter.cpp:2100-2147` serializes the whole
    document, closes, reloads and clears all OCR. A stable page model (source page, rotation,
    crop; written out only at Save) fixes this together with T-05, T-103 and T-116.
- **T-98 Saving a signed PDF silently invalidates the signature.** `save`
  - Done when: a clean ⌘S on a PDF with a /Sig field leaves its bytes unchanged (T-96); editing
    one shows an inline, non-modal notice that the signature will break, and Save defaults to a
    copy unless the user confirms.
  - Src: C10. Nothing in `src` handles /Sig or /ByteRange, and every Save is a full rewrite.
- **T-103 Save, page operations and Undo move the reading position.** `nav`
  - Done when: after Save, Save As, any page operation, Undo or Redo, the same document point
    stays under the same viewport pixel (±2 px).
  - Test: `test_known_bugs_window` (T-103).
  - Src: JMP-1, JMP-2. Save resets scrollY 2113 → 0; rotating an unseen page moves the view
    −840 px and Undo −420 px. Every page op reloads the viewer, and `jump(savedPage, {})` drops
    the offset within the page (`PdfAdapter.cpp:2140-2166`, `:2694-2714`).
- **T-104 Undoing back to the saved state leaves the document dirty, and the window is never
  marked modified.** `undo` `save`
  - Done when: edit then undo to the saved state, for a PDF rotate, a PDF mark and an image
    rotate: no dirty marker, `isWindowModified()` false, and close doesn't prompt; Redo makes it
    dirty again. After any edit `isWindowModified()` is true, so macOS shows the close-button dot
    and "Edited". A document restored from crash recovery shows as edited.
  - Test: `test_known_bugs_window` (T-104).
  - Src: UND-4, SAV-5, SAV-8. `PdfAdapter.cpp:477-480`, `:2209`; `ImageAdapter.cpp:1133`; the
    title uses a "• " prefix and `setWindowModified` is never called.
- **T-107 Form-field edits can't be undone.** `undo`
  - Done when: type in a form field, ⌘Z: the previous value is restored; ⌘⇧Z re-applies it.
  - Src: UND-8. `PdfAdapter.cpp:2817-2825` marks the document dirty and records nothing.
- **T-108 Auto-save rewrites every dirty document every 30 s on the GUI thread, even when
  nothing changed.** `save` `open`
  - Done when: a tick after no new edit writes nothing, and snapshot work causes no GUI-thread
    stall over one frame (counted as GUI-thread engine and file calls: zero).
  - Src: SAV-6. 69–88 ms per tick on a 300-page, 22.5 MB file, 247 ms with one redaction; about
    2.7 GB an hour while a large document stays dirty. `MainWindow.cpp:995-1003`, `:1067`.
- **T-110 Image windows show a permanent "install language pack" link.** `chrome` `ocr`
  - Done when: with the OCR model bundled (PRODUCT.md, open decision 3) the link never shows;
    otherwise it shows at most once per document, in context, with a dismiss.
  - Src: SB-3. `MainWindow.cpp:722-733`, `:979-980`.
- **T-112 Disabled items explain nothing, or point at other items.** `dialogs`
  - Done when: a test walks every menu and toolbar action in the empty, PDF and image states, and
    each disabled action is either self-evident (Undo, Redo, Cut, Copy, Paste) or has a tooltip
    that differs from its label and says why (an image-only command on a PDF says it works on
    images); switching between a PDF tab and an image tab moves no control; there is one OCR
    command.
  - Src: NO-2, NO-8. Nine disabled items on a PDF window have a tooltip that only repeats the
    label (Adjust Size…, Adjust Colour…, Remove Background, Instant Alpha…, Smart Lasso…, Export
    As…, Crop Image…, Fill Forms, and one since removed); "Re-run Text Recognition" is disabled
    with a tooltip pointing to another item.
- **T-115 OCR'd text can't be selected with the default tool, and the I-beam lies.** `ocr` `text`
  - Done when, in the app's default state after OCR completes on an image and on a scanned PDF
    page, driven through MainWindow with real events at positions found from pixels:
    - hovering a word shows the I-beam, and the I-beam shows only where a drag would select;
    - a drag across two words highlights exactly those glyphs;
    - ⌘C and Edit ▸ Copy copy exactly those words;
    - Find highlights a match on the glyphs.
  - Src: OCR-1, OCR-2, OCR-5. The overlay takes the mouse with the default tool (Select), and
    its Select path asks only the PDF text layer; OCR text selects only with tool None, which
    the app never sets. `MainWindow.cpp:4045`; `MarkupToolbar.cpp:43`, `:170`;
    `AnnotationOverlay.cpp:104-120`; `PdfAdapter.cpp:958-1027`.
- **T-118 Images at Fit are drawn at 1x and upscaled on HiDPI screens.** `open`
  - Done when: on a screen with dpr d, at any zoom, the displayed pixmap's device width is at
    least min(source width, drawn logical width × d) − 1. PDFs stay as crisp as they are today
    (fringe ratio within 25 % of a native render at dpr 1 and 2).
  - Test: `test_known_bugs_image` (T-118, dpr 2 entry).
  - Src: HD-1. A 4000×3000 photo at Fit gets a 997×748 pixmap drawn across 1,994 device px.
    `ImageAdapter.cpp:94-97`, `:117-127`.
- **T-120 The Edit menu has no Copy, Cut, Paste or Delete.** `text`
  - Done when: Edit has Copy, enabled exactly when a selection exists and equal to ⌘C, and Cut,
    Paste and Delete apply to marks, in every view mode.
  - Src: SEL-6. ⌘C works only while the overlay or text layer has focus.
    `MainWindow.cpp:1528-1610`.
- **T-127 Dragging a page forward lands it one slot early, and no page can reach the end.**
  `annotations`
  - Done when: in a 4-page document, dragging page 1 in the sidebar to after page 2, after page 3
    and to the end puts it 2nd, 3rd and 4th; `movePage(0, 3)` makes it last. A move that leaves
    the order unchanged neither marks the document edited nor adds an undo step.
  - Src: the sidebar's drop handler subtracts 1 for a forward move (`Sidebar.cpp:234`), and
    `PdfEditor::movePage` subtracts 1 again (`PdfEditor.cpp:153`) and rejects any target past the
    last index (`:146`). `movePage(0, 1)` and `movePage(1, 2)` change nothing but still mark the
    document edited and push an undo step.

## P2: Preview parity and polish

- **T-31 Rotate ignores the thumbnail selection.** `annotations`
  - Done when: with three thumbnails selected, Rotate Left rotates all three, and one ⌘Z
    un-rotates all three.
  - Src: `MainWindow.cpp:2313-2344` rotates `currentPage()` only.
- **T-32 HEIC images don't open.** `open`
  - Done when: an iPhone .heic opens with the right orientation on macOS, Windows and Linux, is
    offered by the Open dialog's filter, and exports to JPEG and PNG.
  - Src: `ImageAdapter.cpp:43-46` (`kExtensions`).
- **T-33 A second drag-crop lands offset.** `annotations`
  - Done when: on a page whose CropBox is already smaller than its MediaBox, a dragged crop
    yields a CropBox equal to the drawn rectangle within 1 pt. Checked at dpr 1 and 2.
  - Src: `PdfEditor.cpp:199-231`; `MainWindow.cpp:2257-2295`.
- **T-34 Several untitled documents all read "Untitled".** `save`
  - Done when: two open untitled documents read "Untitled" and "Untitled 2" in their tabs or
    windows and in the close prompts.
  - Src: `ImageAdapter.cpp:590-596`.
- **T-35 Unsaved work lives in two stores, and Reset doesn't say what it leaves behind.** `save`
  - Done when:
    - recovery copies and ⌥⌘Q drafts share one store under AppPaths, and their existing tests
      stay green;
    - Reset Trailer's confirmation lists every store it deletes or keeps: the OCR cache is
      deleted, and unsaved work is kept and named as kept;
    - a test checks that the disk matches that list;
    - Reset ends without a success toast.
  - Src: `RecoveryStore.cpp:13-19` uses `QStandardPaths::AppDataLocation/autosave`;
    `AppPaths.cpp:98-99`; `MainWindow.cpp:5456-5495`.
- **T-36 Deleting or renaming a document's folder leaves the change monitor deaf.** `save`
  `data-loss`
  - Done when: the open file's parent directory is removed or renamed, then recreated with the
    file, and the monitor still reports the next change. Checked by a headless
    `ExternalChangeMonitor` test.
  - Src: `ExternalChangeMonitor.cpp:62-70`.
- **T-37 On macOS and Windows, the same file spelled with different letter case opens a second
  copy.** `platform` `data-loss`
  - Done when: with `Report.pdf` open, opening `report.pdf` brings the existing window forward on
    macOS and Windows. On Linux, two files that differ only in case stay two documents.
  - Src: `PathKey.h:21-28`.
- **T-38 Two Pages has no cover toggle.** `nav`
  - Done when: a View menu toggle switches Two Pages between two layouts and re-lays out live.
    One shows page 1 alone, then [2,3], …; the other pairs [1,2], [3,4], …. The choice survives a
    relaunch.
  - Src: `TwoPageView.cpp:66` (`setCoverAlone` has no caller).
- **T-39 Ctrl/⌘ + wheel and pinch don't zoom in Two Pages.** `nav`
  - Done when: Ctrl/⌘ + wheel and trackpad pinch over a spread zoom it in and out, as in
    Continuous, at dpr 1 and 2.
  - Src: `TwoPageView.{h,cpp}` have no `wheelEvent`.
- **T-40 Lines and arrows have no endpoint handles.** `annotations`
  - Done when: a selected line or arrow shows a handle at each end. Dragging one moves only that
    end, as a single undo step.
  - Src: `AnnotationOverlay.cpp:1834-1845`.
- **T-41 Saved text boxes carry no appearance stream.** `annotations`
  - Done when: a text box or speech bubble saved by Trailer has an /AP, so pdfium renders its
    text with annotations on. Also check it by hand in Preview.
  - Src: `PdfEditor.cpp:620-626` writes /DA but no /AP.
- **T-42 OCR text isn't embedded on PDF export.** `ocr`
  - Done when: exporting an OCR'd scan writes an invisible text layer. Reopened with OCR off, in
    Trailer and in pdfium, the recognised words can be found and selected.
  - Src: old TODO.md:489-494.
- **T-43 Images opened together don't share one sidebar.** `nav`
  - Done when: opening five images together shows one window whose sidebar lists all five.
    Clicking a thumbnail shows that image, and the selected tab and sidebar row stay in step.
  - Src: old TODO.md:817-828.
- **T-44 Thumbnail page numbers are painted over the page.** `chrome`
  - Done when: each page number sits in a label row below its thumbnail, never over page content,
    at dpr 1 and 2.
  - Src: `Sidebar.cpp:129-153`.
- **T-45 Session restore loads documents one after another.** `open`
  - Done when: restoring five documents paints the frontmost document's first page before the
    other four finish loading. This is an ordering test, with no clock.
  - Src: `Application.cpp:1163-1185`.
- **T-46 Every open runs a whole-document qpdf parse.** `open`
  - Done when: opening a PDF runs no qpdf parse before page one shows, and at most one per
    document. A 100,000-page file without annotations adds no qpdf memory until its first edit;
    today the parse costs 1.43 s of CPU and 265 MB.
  - Src: `PdfAdapter.cpp:507`.
- **T-47 MainWindow is a 5,700-line god object, and documents are held by raw pointer.** `infra`
  - Done when: no file in `src/ui/` exceeds 1,500 lines, and nothing outside `DocumentView`
    caches a raw `IDocument*`. Documents are QObjects, held by QPointer or looked up per call. No
    `findChild<AnnotationOverlay*>` remains. Split controllers out as the subsystem under each is
    replaced, not as a separate big bang.
  - Src: `onCurrentDocumentChanged` alone is 714 lines.
- **T-48 53 modal call sites remain.** `dialogs`
  - Done when: each site in a committed modal census is either a decision PRODUCT.md allows or
    gone. The allowed decisions are a file or page-range pick, confirming irreversible loss,
    one-time consent, and an error the UI can't show. The census gates in CI.
  - Src: the census on branch `claude/trailer-coordinator-elk85x` (`0da986d`). Known noise: the
    Export As pre-dialog (T-114), the Recognize Text page-range modal (T-19), the Saving… modal
    (T-10), "Model Not Available — install manually" (`ModelManagerDialog.cpp:130`).
- **T-49 Trailer can't be built without ONNX Runtime.** `packaging`
  - Done when: configuring with ML off and no ONNX Runtime present builds the app and passes the
    unit tests, and the ML commands are absent from the UI.
  - Src: `CMakeLists.txt:239`; `cmake/OnnxRuntime.cmake:57` and `:78-115`.
- **T-50 Most UAT binaries never run at dpr 2.** `infra`
  - Done when: every UAT binary runs at dpr 1 and 2 through `trailer_register_uat_dpr_matrix`,
    and they are green at release.
  - Src: only 5 of 35 use it.
- **T-51 The macOS UAT crashes in recognize-text on hosted runners.** `infra`
  - Done when: the faulting stack is captured first. Then three consecutive macOS nightlies on new
    main commits finish `test_uat_recognize_text` with `crashed=false`.
  - Src: 8 of 12 nights crashed. valgrind memcheck on Linux finds 0 errors, but the slot that
    runs the model was skipped there for lack of weights: capture the lldb stack on macOS, or run
    that slot under ASan on Linux with the weights present.
- **T-52 Nothing is checked on a real Mac or a real Windows desktop.** `infra` `platform`
  - Done when: each of these passes once on a real Mac (arm64, macOS 14+) and on real Windows 11,
    and the result is recorded in RELEASING.md:
    - reopening the last document;
    - pasted-capture scale at 2x;
    - the macOS screen-capture picker flow;
    - installing a nightly update.

    A GUI smoke lane also runs in macOS and Windows CI.
  - Src: `UpdateManager.cpp:215-221`.
- **T-53 Updates install only on macOS.** `packaging`
  - Done when: on Windows (MSI) and Linux (DEB, RPM, tarball), Check for Updates downloads the
    next version, verifies its ed25519 signature, and either starts the platform installer or
    states the one manual step.
  - Src: `UpdateManager.cpp:211` (`#ifdef Q_OS_MACOS`).
- **T-54 Packages ship a placeholder maintainer and homepage.** `packaging`
  - Done when: no `TODO` placeholder such as `TODO@trailer.example.com` remains under
    `packaging/` or `platform/`, and the DEB, RPM and AppStream metadata carry the real maintainer
    and homepage.
  - Src: `packaging/deb/DEBIAN/control:7` and `copyright:3`; `packaging/rpm/trailer.spec:7`,
    `:61`, `:146` and `:148`; `metainfo.xml:10` and `:31`.
- **T-55 About says "(Phase 1)" and credits no one, and the spelling is mixed.** `docs`
  - Done when: About shows the real version and credits Qt (LGPL), qpdf, ONNX Runtime and each
    model, with links to their licences. User-visible strings use US English throughout
    ("Recognize", "Color").
  - Src: NO-9. `MainWindow.cpp:5053-5058`, `:727-728`, `:2066`.
- **T-57 Save errors don't say why.** `dialogs`
  - Done when: a failed Save names the cause, such as permission denied or a full disk, and says
    what to do next.
  - Src: `MainWindow.cpp:3218`, `:3278` and `:3290`.
- **T-99 Image undo memory is unbounded by size.** `undo`
  - Done when: undo history per image stays under 4× the decoded image, with every step still
    undoable, and rotate and flip are stored as operations, not pixels. An edited image holds no
    full copy outside the history: today the finished decode keeps the originally decoded image
    alive until the document closes (`ImageAdapter.cpp:348-374`).
  - Test: `test_known_bugs_image` (T-99; history only).
  - Src: C9, UND-10. `kMaxUndoSteps = 32` full-resolution copies (`ImageAdapter.cpp:71`,
    `:1077-1080`): about 3 GB for a 24 MP photo.
- **T-100 The compiled-out recorder still changes behaviour, and PR CI tests a different
  configuration from the one that ships.** `infra`
  - Done when: the no-op recorder facade doesn't evaluate its arguments (a macro or lambda); the
    Linux PR job builds the shipped configuration (recorder off); and either product code holds
    no recorder call sites (it attaches from outside, through an event filter and action
    signals) or a test proves the compiled-out build has no side effects.
  - Src: C11. The no-op `recordEvent` receives a fully built QJsonObject (`UxRecord.h:53`), so
    `addDocument` forces the deferred open via `pageCount()` (`MainWindow.cpp:5251-5254`). The
    Linux PR job configures `-DTRAILER_ENABLE_UX_RECORDER=ON` (`ci.yml:185`, `:322`). 26 recorder
    references sit in 9 product files. In recorder builds, "Hand Off to Preview" saves to the
    user's file on macOS (`UxTrailerHooks.cpp:287-293`): a dev tool must not write documents.
- **T-101 Filled form values rely on NeedAppearances.** `annotations`
  - Done when: a filled text field renders in pdfium with NeedAppearances off, and prints.
  - Src: C15. `PdfEditor.cpp:1402-1416` writes /V and sets NeedAppearances but builds no /AP.
- **T-105 Undo and Redo don't name the operation.** `undo`
  - Done when: Edit ▸ Undo and Redo read "Undo Rotate", "Undo Delete Page", "Undo Add Rectangle"
    and so on for every operation type.
  - Src: UND-6. `MainWindow.cpp:1533`; `PdfCommand::description()` is never shown.
- **T-106 A text box needs two ⌘Z to remove.** `undo`
  - Done when: create a text box, type, commit, ⌘Z once: the box is gone.
  - Src: UND-7. `AnnotationOverlay.cpp:916` commits in a second frame.
- **T-109 Close → Save on a file changed on disk fails with a status-bar line.** `save`
  - Done when: a close-time Save of an externally changed file offers the same Reload / Keep mine
    choice as ⌘S.
  - Src: SAV-7. `MainWindow.cpp:5773-5797` ("Could not save X; close cancelled.").
- **T-111 Errors are posted to the status bar, out of view of what failed.** `dialogs`
  - Done when: each failure shows on the document (a banner) or as a sheet and stays until
    dismissed; no error is posted to the status bar.
  - Src: SB-4. 18 `flashError` sites (12 s) and 8 `flashStatus` sites (4 s), `MainWindow.cpp:5227`,
    `:5241`.
- **T-113 Export As… is disabled for PDFs.** `dialogs`
  - Done when: File ▸ Export… on a PDF exports pages to PNG, JPEG or TIFF, as Preview does.
  - Src: NO-4. `MainWindow.cpp:4335`.
- **T-114 Image Export shows a filter dialog before the save panel.** `dialogs`
  - Done when: one dialog per Export, with format options inside the save panel.
  - Src: NO-5. `MainWindow.cpp:2981-3006`.
- **T-116 Any page operation discards OCR for every page.** `ocr`
  - Done when: rotate page 1: page 2 keeps its OCR, and page 1's is rotated or recomputed once.
  - Src: OCR-4. `PdfAdapter.cpp:2140-2146` clears all OCR.
- **T-117 A drag that starts in the margin selects nothing.** `text`
  - Done when: a drag from 25 px left of a line to its middle selects the first half of the line,
    for native and OCR text.
  - Src: SEL-4.
- **T-119 Text selection stops at the page boundary.** `text`
  - Done when: a drag from page 1's last line to page 2's first line copies both lines in order,
    and a drag from mid-word copies from that glyph.
  - Src: SEL-5. `SelectableTextLayer.cpp` handles the current page only.
- **T-121 Invisible text (render mode 3) selects unreliably.** `text`
  - Done when: on a page carrying invisible text over a scan (as ocrmypdf writes), and on the
    owner's 365-page manual, a drag over a visible word copies that word.
  - Src: SEL-8; was `docs/backlog/2026-07-31-pdf-text-render-mode-detection.md`.
- **T-122 The search poll ticks every 150 ms forever.** `open`
  - Done when: with no search active, no timer fires (checked with a timer-event counter over 5 s
    of idle).
  - Src: `MainWindow.cpp:244-263`; the comment says it only runs during a search.
- **T-125 OCR on the reference machine is unmeasured.** `ocr`
  - Done when: with the weights present, OCR of one 300 dpi page uses at most one worker thread
    on battery, is cancellable within one page, makes no GUI-thread engine calls, and its time
    on the reference machine is recorded.
  - Src: no OCR run has been timed on old hardware.
- **T-126 Releases are untested end to end.** `packaging`
  - Done when: the release workflow produces every artifact from one tag; `dpkg-deb -c` on the
    `.deb` lists `libonnxruntime`, and `ldd` of the installed binary in a clean `ubuntu:24.04`
    container reports nothing "not found"; release notes name the right qpdf package
    (`libqpdf29t64` on 24.04).
  - Src: `release.yml` has succeeded 3 times, last on 2026-07-13, and its `.deb`/`.rpm` steps
    have never run in a release. `scripts/bundle-qt-runtime.sh:60-64` drops libraries `ldd`
    can't resolve. `release-publish.yml:250` says `libqpdf28`.

## P3: later or stretch

- **T-59 An external edit that keeps the size and mtime goes unnoticed.** `save`
  - Done when: an overwrite with different content of the same length, within the same second,
    is classified as a change. Checked by a headless `classifyExternalChange` test.
  - Src: `ExternalChangeState.cpp:34`.
- **T-60 Moving a window between dpr 1 and dpr 2 screens doesn't re-render the image.** `open`
  - Done when: an image window dragged between a 2x and a 1x screen stays sharp (T-118 holds
    within one frame). At the pixel-exact stop it stays unresampled, with the pixmap stamped with
    the new screen's dpr. This has a test plus a real mixed-DPI check.
  - Src: HD-3. `ImageAdapter.h:318`.
- **T-61 ⌥⌘Q prompts when a PDF has page edits plus a pending redaction or signature.** `save`
  - Done when: such a PDF is kept by ⌥⌘Q with no prompt. At launch it returns with both edits
    intact, the annotation still editable, and the document dirty.
  - Src: `Application.cpp:847-849`.
- **T-62 The empty window's drop target uses a stock Qt icon.** `chrome`
  - Done when: the Windows/Linux empty window shows Trailer's own drop-target artwork, crisp at
    dpr 1 and 2.
  - Src: `EmptyStateWidget.cpp:38`.
- **T-63 The Dock icon doesn't follow dark mode.** `platform`
  - Done when: on a real Mac in dark mode (Tahoe), the Dock icon shows the dark variant and flips
    when Appearance is toggled.
  - Src: the dark variants in `resources/macos/Assets.xcassets/AppIcon.appiconset` compile
    without a warning, but none reach `Assets.car` (nightly macOS log, 2026-08-02): a macOS
    `.appiconset` has no light/dark axis. The likely fix is an Icon Composer `.icon` source,
    which needs Xcode 26+. First run `scripts/diagnose-appicon-dark-variants.sh` on a Mac to
    rule out `assetutil` simply not listing the variants. Build wiring: `CMakeLists.txt:684-742`.
    History: `git show 09bc1e3:docs/backlog/2026-07-17-adaptive-dock-icon-option-b.md`.
- **T-64 Windows has no Jump List of recent files.** `platform`
  - Done when: the taskbar icon's Jump List shows up to 10 recent files, from the same source as
    the macOS Dock menu. It works even when Trailer isn't running, and picking a file opens it.
  - Src: was `docs/backlog/2026-07-31-windows-jumplist-recents.md`.
- **T-65 Spotlight's window result for an open document fails.** `platform`
  - Done when: on macOS, choosing an open document's "… — Trailer / Window" Spotlight result
    brings it to the front with no error notification. It is also acceptable for the result not
    to be offered, as long as the file result still works.
  - Src: was `docs/backlog/2026-08-06-macos-spotlight-activate-tab.md`.
- **T-66 Problem reports can't tell invisible OCR text from visible text.** `ocr`
  - Done when: given an ocrmypdf-style page (a scan with render-mode-3 text) and a born-digital
    page, the feedback report states two different, correct text states without comparing
    pixels.
  - Src: `FeedbackReport.cpp:260-270`.
- **T-67 The nightly Linux tarball has no .desktop file.** `packaging`
  - Done when: the tarball contains `trailer.desktop` and an icon, so
    `xdg-mime default trailer.desktop application/pdf` works after extraction with no hand edits.
  - Src: `nightly.yml:548-552`.
- **T-68 The UAT ratchet compares counts, not tests.** `infra`
  - Done when: the ratchet is deleted with T-58, or `compare-uat-baseline.sh` reports `worse` when
    a test that passed in the baseline fails, even if the passing total rose.
  - Src: `scripts/compare-uat-baseline.sh:59-65`.
- **T-69 The stable update channel isn't signed.** `packaging`
  - Done when: the release workflow publishes an ed25519-signed stable appcast, and Preferences
    offers Stable as a working choice.
  - Src: `PreferencesDialog.cpp:329-345`.
- **T-70 The macOS app bundle omits the licence files.** `packaging`
  - Done when: `Trailer.app/Contents/Resources` contains LICENSE, THIRD_PARTY_LICENSES.md and
    `licenses/third-party`.
  - Src: `scripts/build-macos.sh:566`.
- **T-71 The MSI shows no licence and isn't signed.** `packaging`
  - Done when: the MSI shows the MIT licence and the third-party notices. A comment in
    `trailer.wxs` settles the WiX 4 question and the Authenticode question (sign only if a
    certificate is ever bought).
  - Src: `platform/windows/trailer.wxs:6-11`.
- **T-72 Linux distribution beyond DEB and RPM.** `packaging`
  - Done when: one .desktop / metainfo / icon source feeds DEB, RPM and the tarball (there are two
    copies today), and a Flathub manifest builds in CI. Each further channel the owner wants gets
    its own entry: COPR, PPA, Snap, aarch64, GPG-signed RPM, AppStream screenshots.
  - Src: the old `packaging/rpm/TODO-rpm-packaging.md` and `platform/linux/TODO-linux-packaging.md`.
- **T-73 Accessibility for 1.0.** `chrome`
  - Done when:
    - an automated sweep finds an accessible name on every actionable control;
    - every markup tool has a shortcut;
    - colour swatches show their state without relying on colour;
    - a signature can be typed;
    - a keyboard-only run completes the PRODUCT.md "Done means" jobs at dpr 1 and 2;
    - contrast meets WCAG AA, focus is always visible, Reduce Motion is honoured, and text scales
      to 200 %.
  - Src: `MarkupToolbar.cpp` has no `setShortcut`; `SignatureCaptureDialog.cpp:257`.
- **T-75 Untrusted input has no size caps.** `security`
  - Done when: clipboard images, form values and embedded /Contents above a stated cap are
    refused with a message. File type is decided by content, not extension. EXIF location can be
    stripped on Export. Password buffers are zeroed after use. Each of these has a unit test.
  - Src: the May 2026 audit (`git show 09bc1e3:docs/audit-2026-05-19.md`), SEC-MOD-1, -3, -4 and
    SEC-MIN-1..3.
- **T-76 There is no local log file for diagnosing crashes.** `infra`
  - Done when: Trailer writes a size-capped local log under `AppPaths::logsDir()` that Report a
    Problem can attach. Nothing is uploaded.
  - Src: `AppPaths.cpp:94`.
- **T-77 AppPaths declares directories nothing uses.** `infra`
  - Done when: AppPaths declares only directories that something reads or writes, which removes
    versions, icc, filters and plugins.
  - Src: `AppPaths.h:16-22`.
- **T-78 Unit tests are missing.** `infra`
  - Done when: unit tests cover a corrupt settings.toml, ModelDownloader errors,
    AnnotationOverlay, Magnifier and Inspector. The recorder evidence test writes only to a temp
    dir, and `test_pdf_editor` is split by area.
  - Src: `test_uat_ux_recorder_evidence.cpp:59-74`.
- **T-79 ML caches have no memory cap, and MlScheduler runs one job at a time.** `ocr`
  - Done when: the SAM encoder cache and the background-removal cache stay under a stated cap
    (with a test), and MlScheduler runs more than one job when cores allow.
  - Src: old TODO.md:519-528.
- **T-80 The sidebar rebuilds every row on any change, and the Inspector re-renders on every
  slider tick.** `open`
  - Done when: deleting one page updates only the affected thumbnail rows, and dragging an
    Inspector slider re-renders at most once per frame.
  - Src: old TODO.md:532-540.
- **T-81 Inspector visibility isn't remembered.** `chrome`
  - Done when: showing or hiding the Inspector survives a relaunch.
  - Src: old TODO.md:541-544.
- **T-82 Signatures are stored only as bitmaps, and ink pressure isn't saved.** `annotations`
  - Done when: a captured signature keeps its strokes as vectors, so it can be placed at any size
    without blur, and ink pressure survives a PDF save and reload.
  - Src: old TODO.md:958-973.
- **T-83 Read-only 3D viewing (OBJ, STL).** `open`
  - Done when: after Tier 1 is complete, OBJ and STL files open read-only with orbit and zoom, and
    a large mesh shows its first frame as fast as a PDF's first page.
  - Src: PRODUCT.md, Tier 3.
- **T-84 Leftovers in the UX recorder (a dev tool).** `infra`
  - Done when: the recorder reports its input-permission state, and caps its camera storage.
  - Src: `git show 09bc1e3:docs/ux-recorder-todo.md` (UXR-003, UXR-005).
- **T-85 Nothing stops new network code from landing.** `infra`
  - Done when: a CI check fails if a network class (QNetworkAccessManager, QTcpSocket,
    QUdpSocket, QNetworkRequest, …) appears outside `src/ml/ModelDownloader.*`, `src/update/`
    and their tests.
  - Src: AGENTS.md, Hard constraints.
- **T-86 The deleted-file banner says "Your edits are still open" when there are no edits.**
  `dialogs`
  - Done when: for a clean document whose file was deleted, the banner text doesn't mention edits.
  - Src: `FileChangeBanner.cpp:103`.
- **T-87 Open Recent is a flat list of up to 50 entries.** `chrome`
  - Done when: Open Recent shows at most 10 entries, most recent first, open files included, as
    on macOS (decided). History depth is unchanged.
  - Src: `Settings.h:233` (`m_recentMax = 50`); `MainWindow.cpp:5012`.
- **T-88 The macOS window picker lists Trailer's own windows.** `screenshot`
  - Done when: on macOS, From Window… doesn't offer Trailer's own windows.
  - Src: `MacScreenCapture.mm:195`.
- **T-89 ML plumbing fails silently.** `ocr`
  - Done when: if the OCR dictionary fails to load, the OCR controls are disabled with a tooltip
    naming the cause. An unknown model id is handled without `Q_UNREACHABLE`.
  - Src: `OcrEngine.cpp:353-355`; `ModelRegistry.cpp:43`.
- **T-123 PDFs that forbid modification are edited without notice.** `save`
  - Done when: PRODUCT.md states the policy for owner-password permission flags, and a PDF opened
    with `--modify=none` behaves as it says.
  - Src: Trailer edits such files without asking (verified with qpdf `--modify=none`).
- **T-124 Cold launch to the first window has never been measured on the reference machine.**
  `open`
  - Done when: a developer-only flag (`--quit-after-first-frame`) reports launch-to-first-frame
    time, and a run after a reboot on the reference machine is recorded. Never gated.
  - Src: warm `trailer --version` takes 17–30 ms here; cold launch on old hardware is unknown.
