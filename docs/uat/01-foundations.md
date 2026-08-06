# UAT — Foundations

Covers application launch, the window shell, settings, recent files, the
command-line / file-open pipeline, and drag-and-drop into the app.
Format described in [README.md](README.md).

---

## Application launch

### UAT-FND-001 — Launch with no arguments

**Preconditions:** App is installed / built. No other Trailer instance running.
**Steps:**
1. Run `trailer` with no arguments.
**Expected:**
- A single main window appears.
- The window title is `Trailer`.
- Menu bar contains: `File`, `Edit`, `View`, `Tools`, `Help`.
- No tabs are open; the central area shows no document.
- The Markup Toolbar is hidden (toggle it on with `View > Toggle
  Markup Toolbar` or `Ctrl+Shift+A`).
- The Inspector dock is not visible.
- The Sidebar dock is visible.

### UAT-FND-002 — Launch with one file argument

**Preconditions:** A readable PDF file exists at a known path.
**Steps:**
1. Run `trailer path/to/file.pdf`.
**Expected:**
- A single main window appears.
- One tab is open, labelled with the file's basename.
- The tab is the active tab.
- The PDF renders in the central area.
- `File > Open Recent` lists this file as the topmost entry.

### UAT-FND-003 — Launch with multiple file arguments

**Preconditions:** Two readable files exist (any mix of PDF / image).
**Steps:**
1. Run `trailer file1.pdf file2.png`.
**Expected:**
- One window opens with two tabs, in the order given on the command line.
- The first argument's tab is the active tab.
- Each tab shows its respective document.
- Both files appear in `File > Open Recent`, most recent first.

### UAT-FND-004 — Launch with an unsupported file

**Preconditions:** A file with an unknown extension exists (e.g. `.xyz`).
**Steps:**
1. Run `trailer file.xyz`.
**Expected:**
- A tab opens for the file.
- The central view displays an "Unsupported file type" message that
  includes the path.
- The document tab is not marked dirty.

### UAT-FND-005 — `--help` prints usage and exits

**Preconditions:** None.
**Steps:**
1. Run `trailer --help`.
**Expected:**
- Stdout shows usage text including the positional `[files...]` argument
  and the `--help` / `--version` options.
- The process exits with status 0.
- No window appears.

### UAT-FND-006 — `--version` prints a version string and exits

**Preconditions:** None.
**Steps:**
1. Run `trailer --version`.
**Expected:**
- Stdout shows a version string.
- Process exits with status 0.
- No window appears.

### UAT-FND-007 — Launch with a path that does not exist

**Preconditions:** `/tmp/does-not-exist.pdf` is not a file.
**Steps:**
1. Run `trailer /tmp/does-not-exist.pdf`.
**Expected:**
- A window opens with a tab for the file.
- The view displays an error / unsupported placeholder (the stub
  adapter path).
- The application does not crash.

---

## Window shell

### UAT-FND-010 — Menu structure

**Preconditions:** App launched (no document open).
**Steps:**
1. Click each top-level menu in turn.
**Expected:**
- `File` contains: `Open…`, `Open Recent ▸`, `Save`, `Save As…`,
  `Print…`, `Close Window`, `Quit`.
- `Edit` contains: `Undo`, `Redo`, `Find…`, `Find Next`, `Find Previous`.
- `View` contains: `Toggle Sidebar`, `Toggle Markup Toolbar`,
  `Toggle Inspector`, view mode group (`Single Page`, `Two Pages`,
  `Continuous`), `Previous Page`, `Next Page`, `Zoom In`, `Zoom Out`,
  `Actual Size`, `Fit to Width`, `Magnifier`.
- `Tools` contains: `Rotate Left`, `Rotate Right`, `Flip Horizontal`,
  `Flip Vertical`, `Adjust Size…`, `Adjust Colour…`, `Export As…`,
  `Crop Image…`, `Insert Pages from File…`, `Crop Pages…`,
  `Take Screenshot`.
- `Help` contains: `About Trailer`.

### UAT-FND-011 — Menu state with no document

**Preconditions:** App launched, no tab open.
**Steps:**
1. Open each menu.
**Expected:**
- Document-specific actions are disabled: `Save`, `Save As…`, `Print…`,
  `Undo`, `Redo`, `Find…`, `Find Next`, `Find Previous`, all of the
  `Tools` menu except `Take Screenshot`, all `View` view-mode and page
  navigation entries.
- `Open…`, `Open Recent`, `Close Window`, `Quit`, the dock toggles,
  `Take Screenshot`, and `About Trailer` remain enabled.

### UAT-FND-012 — About dialog

**Preconditions:** App launched.
**Steps:**
1. `Help > About Trailer`.
**Expected:**
- An About dialog appears containing the app name and version.
- Closing the dialog returns focus to the main window.
- **(Platform: macOS)** The menu item is reparented to the application
  menu (`Trailer > About Trailer`) under macOS's standard menu role.

### UAT-FND-013 — Close Window via menu

**Preconditions:** One window open with no documents.
**Steps:**
1. `File > Close Window` (shortcut `Cmd+W` / `Ctrl+W`).
**Expected:**
- The window closes.
- The process exits cleanly (if it was the last window).

### UAT-FND-014 — Close with unsaved changes (Save / Discard / Cancel)

**Preconditions:** A document is open with unsaved modifications (the
tab title starts with `• `).
**Steps:**
1. Trigger a close — either close the tab (tab close button, last tab →
   empty state on Win/Linux, or a non-last/middle tab) **or**
   `File > Close Window`.
**Expected:**
- A prompt asks whether to save, discard, or cancel.
- `Save` writes the document (untitled docs route through Save-As to
  pick a destination) and closes; the document reports clean afterwards.
- `Discard` closes without writing; the original file on disk is
  unchanged.
- `Cancel` leaves the window/tab open and the document untouched — the
  unsaved edits (dirty state) survive and nothing is written to disk.
- Closing a **clean** document never prompts.
- Closing a non-last tab prompts only for that tab; the other open
  documents are unaffected.

**Automated coverage:** `test_uat_foundations` cases
`uat_fnd_014_closeDirtyTabCancelKeepsDocAndEdits`,
`uat_fnd_014_closeDirtyTabDiscardDropsDoc`,
`uat_fnd_014_closeDirtyTabSaveTitledWritesFile`,
`uat_fnd_014_closeDirtyTabSaveUntitledRoutesThroughSaveAs`,
`uat_fnd_014_closeDirtyNonLastTabCancelThenDiscard`, and
`uat_fnd_014_closeCleanTabNeverPrompts` drive the full matrix through the
`setCloseResponseForTesting` seam. Cross-ref UAT-FND-092 and TODO.md.

### UAT-FND-015 — Quit via menu

**Preconditions:** App launched.
**Steps:**
1. `File > Quit` (shortcut `Cmd+Q` / `Ctrl+Q`).
**Expected:**
- All windows close.
- The process exits.
- **(Platform: macOS)** This is reparented to `Trailer > Quit Trailer`.

### UAT-FND-016 — Toggle Sidebar

**Preconditions:** App launched.
**Steps:**
1. `View > Toggle Sidebar` (shortcut `Ctrl+Shift+D`).
2. Repeat.
**Expected:**
- First trigger hides the Sidebar dock.
- Second trigger shows it again.
- The `Toggle Sidebar` menu item reflects current state with a check
  mark.

### UAT-FND-017 — Toggle Markup Toolbar

**Preconditions:** App launched.
**Steps:**
1. `View > Toggle Markup Toolbar` (shortcut `Ctrl+Shift+A`).
2. Repeat.
**Expected:**
- The toolbar hides and reappears. Menu item state toggles.

### UAT-FND-018 — Toggle Inspector

**Preconditions:** App launched.
**Steps:**
1. `View > Toggle Inspector` (shortcut `Ctrl+Shift+I`).
2. Repeat.
**Expected:**
- The Inspector dock shows and hides. Menu item state toggles.

### UAT-FND-019 — Sidebar / Inspector docks: no visible caption, accessible name kept (G10)

**Preconditions:** App launched, Sidebar and Inspector both shown (`View >
Toggle Sidebar` / `Toggle Inspector`).
**Steps:**
1. Look at the title-bar strip atop each dock.
2. Query each dock's accessible name (e.g. via a screen reader, or
   `QAccessible::queryAccessibleInterface(dock)->text(QAccessible::Name)`
   in a debugger/test).
3. Click the close (✕) button in each dock's title-bar strip.
**Expected:**
- Neither strip shows the word "Sidebar" or "Inspector" as visible text —
  the panel's own content already makes its purpose obvious
  (docs/ux-guidelines.md's motivating example for gate G10).
- Each dock's accessible name still resolves to "Sidebar" / "Inspector"
  respectively — a screen-reader user hears the panel named correctly
  despite the blank visible caption.
- The close button still hides the corresponding dock (functional parity
  with the native captioned title bar it replaced).

Automated: `uat_fnd_019_dockPanelsHaveNoVisibleCaptionButKeepAccessibleName`
in `tests/uat/test_uat_foundations.cpp`.

### UAT-FND-032 — Recovery snapshot never flashes a status-bar message (G10)

**Preconditions:** App launched, `autoSave` on, a PDF open with an
unsaved edit (dirty via a page rotate or an annotation).
**Steps:**
1. Let (or force) an auto-save tick run.
**Expected:**
- A recovery snapshot is written (crash safety is preserved) — but no
  status-bar toast ("Recovery snapshot saved." or otherwise) appears.
  This is the anti-pattern named in docs/ux-guidelines.md: routine
  background work does not narrate itself.
- The document's actual unsaved-work signal — the title-bar "•" dirty
  marker — is present both before and after the snapshot, so the user
  still has a way to tell the document has unsaved changes.

Automated: `uat_fnd_032_recoverySnapshotNeverFlashesStatusBarMessage` in
`tests/uat/test_uat_foundations.cpp`.

### UAT-FND-093 — View menu page-mode items keep a fixed order (G10)

**Preconditions:** A multi-page PDF is open (so Two Pages is enabled).
**Steps:**
1. Note the top-to-bottom order of Continuous / Single Page / Two Pages in
   the View menu.
2. Select Single Page, then Two Pages, then Continuous, checking the order
   after each.
**Expected:**
- The three items always appear in Cmd-1/2/3 order — Continuous, Single
  Page, Two Pages — top to bottom, regardless of which one is active.
  Selecting a mode changes which item is checked, never its position.
  Gate G10 (spatial constancy, AGENTS.md) names exactly this — "a menu
  reordering its items by which mode is active" — as a violation.

---

## Settings persistence

Settings live at the OS-appropriate path (see [DESIGN.md](../../DESIGN.md)
§5.4 and [src/settings/AppPaths.cpp](../../src/settings/AppPaths.cpp)):

| Platform | Settings dir (contains `settings.toml`, `recent.json`) | Data dir |
|---|---|---|
| macOS | `~/Library/Application Support/Trailer/` | same |
| Windows | `%APPDATA%\Trailer\` (Qt `AppDataLocation`) | same |
| Linux | `${XDG_CONFIG_HOME:-~/.config}/trailer/` | `${XDG_DATA_HOME:-~/.local/share}/trailer/` |

Note the lowercase `trailer` on Linux and the split of `settingsDir`
vs `dataDir` — Phase 0 writes everything under `settingsDir`, so the
Linux data dir may not yet exist.

Verifying the contents requires reading the file on disk.

### UAT-FND-020 — `settings.toml` is created on first run

**Preconditions:** Delete the Trailer directory at the platform-specific
path above.
**Steps:**
1. Launch the app.
2. Quit.
3. Inspect the config directory on disk.
**Expected:**
- A `settings.toml` file exists at the platform-specific settings dir.
- It contains a `[general]` section with at minimum `theme` and
  `open_files_in` keys.
- It contains a `[files]` section with `auto_save` and `recent_max`.

### UAT-FND-021 — Settings persist across restart

**Preconditions:** Edit `settings.toml` to set `general.theme = "dark"`.
**Steps:**
1. Launch the app.
2. Quit.
3. Re-read `settings.toml`.
**Expected:**
- The `theme` value is still `"dark"`.
- No settings keys have been silently dropped or reordered in a way
  that changes semantic meaning.

### UAT-FND-022 — Corrupt `settings.toml` falls back to defaults

**Preconditions:** Write invalid TOML to `settings.toml`.
**Steps:**
1. Launch the app.
**Expected:**
- The app launches successfully.
- In-memory settings are the built-in defaults.
- The corrupt file is either left alone or rewritten with defaults (we
  should pick one — until we do, either outcome is acceptable, but the
  app must not crash).

### UAT-FND-094 — Quitting remembers the page you were on in each document

Implements DESIGN §6.13's PDF pane promise — *"reopen at last viewed
page"* — across an application **quit**, not just a window close.

**Preconditions:** Two multi-page PDFs of at least 12 pages each.
`session.restore_previous_windows` at its default (`true`).
**Steps:**
1. Open both PDFs (one window each).
2. In the first, navigate to page 8. In the second, navigate to page 3.
3. Quit with `File > Quit` (`Cmd+Q` / `Ctrl+Q`) — do **not** close the
   windows first.
4. Relaunch the app.
**Expected:**
- The first document reopens showing **page 8**; the second shows
  **page 3**. Neither returns to page 1.
- The saved page is per file: each document restores its own page, not
  the last-quit document's page.
- Repeating steps 1-4 with `Quit and Keep Windows`
  (`Opt+Cmd+Q`; **Platform: macOS** accelerator, the menu item exists on
  every platform per G4) restores the same pages.
- Reopening one of the files on its own via `File > Open Recent` — no
  session restore involved — also lands on its saved page.

**Regression note (2026-08-03 dogfooding report):** every document came
back on page 1 after `Cmd+Q`. Per-document view state was captured **only**
in `MainWindow::closeEvent`, and an application quit never delivers
`closeEvent` to its still-open windows (`Application::onAboutToQuit`
documents exactly that for macOS `Cmd+Q`), so nothing was ever written.
Additionally `Application::restoreKeptWindows()` called
`RecentFiles::add()` — which replaces an entry with a default-constructed
one — *before* handing the document to its window, wiping any saved page
on the `Opt+Cmd+Q` path. Harness slots:
`uat_fnd_094_normalQuitCapturesPageSoReopenLandsThere`,
`uat_fnd_094_keepWindowsQuitRestoresPage` in
[`tests/uat/test_uat_foundations.cpp`](../../tests/uat/test_uat_foundations.cpp).

### UAT-FND-095 — The restored page STAYS put once later layout runs

The other half of UAT-FND-094. Landing on the saved page is not enough if a
**later** layout pass then moves it: the reported symptom was the document
flashing its saved page and snapping away, which a check that samples the
page once, immediately, cannot see at all.

**Preconditions:** A long PDF (the report was against a multi-hundred-page
document, where a stale position is unmistakable).
**Steps:**
1. Read to a deep page — say page 212 of 300.
2. Quit (`Cmd+Q`), then reopen the file.
3. Once it has come back on page 212, resize the window.
**Expected:**
- The document is on page 212 after the reopen, and **stays** there — over
  time, and across the later relayout in step 3, not merely on the first
  frame.
- The same holds for `Quit and Keep Windows` (`Opt+Cmd+Q`) and with several
  windows open at once: each window gets its own saved page back, none
  inherits a sibling's, none snaps to page 1.

Harness slots: `uat_fnd_095_restoredPageStaysPutAfterLaterLayout`,
`uat_fnd_095_restoredPageStaysPutAcrossQuitModesAndWindows` in
[`tests/uat/test_uat_foundations.cpp`](../../tests/uat/test_uat_foundations.cpp).

**Regression note (2026-08-05, gating Linux nightly lane).** The first slot
failed on Linux — restored to page 212, then the step-3 resize carried it to
page 223. The restore itself was correct; step 3 was reaching a separate,
pre-existing defect in the viewer (a fit-mode re-layout keeps the scrollbar's
absolute pixel value while rescaling every page). Written up and fixed under
**UAT-VWR-111** in [`02-viewer.md`](02-viewer.md); this case now passes
because a resize no longer moves the page for anyone, restored or not.

---

## Recent files

### UAT-FND-030 — Opening a file adds it to the Recent menu

**Preconditions:** App launched, no files in Recent menu (or tracking
starting state).
**Steps:**
1. `File > Open…`, pick any supported file.
**Expected:**
- `File > Open Recent` now has the just-opened file as the topmost entry.
- The entry's label is the basename; hovering shows the full path in a
  tooltip.

### UAT-FND-031 — Recent menu entries open the file

**Preconditions:** At least one entry in `File > Open Recent`.
**Steps:**
1. `File > Open Recent`, click an entry.
**Expected:**
- The file opens in a new tab (or the routing set by
  `general.open_files_in`).
- That entry bubbles to the top of the Recent list.

### UAT-FND-032 — Recent menu deduplicates

**Preconditions:** App launched.
**Steps:**
1. Open the same file three times (any mix of `File > Open…`, Recent
   menu, CLI restart).
**Expected:**
- The file appears exactly once in `File > Open Recent`.
- It is the topmost entry.

### UAT-FND-033 — Recent menu respects `files.recent_max`

**Preconditions:** Set `files.recent_max = 3` in `settings.toml`. App
launched fresh.
**Steps:**
1. Open four distinct files in sequence.
**Expected:**
- `File > Open Recent` contains exactly three entries — the three
  most-recently-opened files.
- The fourth (oldest) is dropped.

### UAT-FND-034 — Recent menu persists across restart

**Preconditions:** App launched with some Recent entries.
**Steps:**
1. Note the current Recent entries in order.
2. Quit.
3. Relaunch.
4. Open `File > Open Recent`.
**Expected:**
- The menu contains the same entries in the same order.

### UAT-FND-035 — Recent menu Clear option

**Preconditions:** `File > Open Recent` has ≥1 entry.
**Steps:**
1. Click the "Clear Menu" entry at the bottom of the submenu.
**Expected:**
- The Recent list becomes empty.
- On restart, the list is still empty.

### UAT-FND-037 — Empty Recent menu

**Preconditions:** A fresh profile (no `recent.json`), or click
`Clear Menu` on an existing one.
**Steps:**
1. Open `File > Open Recent`.
**Expected:**
- The submenu contains a single disabled `(Empty)` entry and the
  `Clear Menu` entry. No separator line is drawn between them when the
  list is empty.

### UAT-FND-036 — Recent menu omits missing files on click

**Preconditions:** A file appears in Recent. Delete the file from disk
while the app is running.
**Steps:**
1. Click the Recent entry for the deleted file.
**Expected:**
- The app shows an error or opens the stub view (the same path used
  for a missing file given on the CLI).
- The entry remains in Recent (we do not silently prune on every click).
- The app does not crash.

---

## Drag and drop (application window)

### UAT-FND-040 — Drop a file on an empty window opens it

**Preconditions:** App launched, no tabs open.
**Steps:**
1. From a file manager, drag a PDF onto the main window.
2. Release.
**Expected:**
- A new tab opens with that PDF active.
- The file is added to `File > Open Recent`.

### UAT-FND-041 — Drop a file on a window with open tabs

**Preconditions:** App launched with one or more tabs already open.
Setting `general.open_files_in = "new_tab"`.
**Steps:**
1. Drag a file onto the window.
**Expected:**
- A new tab is added to the *same* window.
- The new tab becomes active.
- Existing tabs are unchanged.

### UAT-FND-042 — Drop multiple files at once

**Preconditions:** App launched.
**Steps:**
1. Select three files in the file manager and drag them together onto
   the window.
**Expected:**
- Three new tabs open.
- All three files are in `File > Open Recent`.

### UAT-FND-043 — Drop a folder

**Preconditions:** App launched.
**Steps:**
1. Drag a folder onto the window.
**Expected:**
- Either (a) the folder is silently ignored, or (b) an error is shown.
  The app must not crash. (This is an unspecified behaviour — the case
  exists to catch regressions when we decide.)

### UAT-FND-044 — Drop non-URL data (text)

**Preconditions:** App launched.
**Steps:**
1. Drag selected text from another app onto the Trailer window.
**Expected:**
- Drop is rejected (no tab is opened).
- App does not crash.

---

## Open-files-in routing

### UAT-FND-050 — `open_files_in = "new_tab"` (default)

**Preconditions:** `general.open_files_in = "new_tab"`. App launched
with one window and one open document.
**Steps:**
1. `File > Open…`, pick another file.
**Expected:**
- A new tab appears in the same window.
- The new tab is active.

### UAT-FND-051 — `open_files_in = "new_window"`

**Preconditions:** `general.open_files_in = "new_window"`. App launched
with one window and one open document.
**Steps:**
1. `File > Open…`, pick another file.
**Expected:**
- A new main window opens.
- The new window contains exactly one tab with the newly opened file.
- The original window is unchanged.

### UAT-FND-052 — `open_files_in = "same_window"`

**Preconditions:** `general.open_files_in = "same_window"`. App launched
with one window and one open document.
**Steps:**
1. `File > Open…`, pick another file.
**Expected:**
- No new window appears.
- **Current:** a new tab is added to the existing window (identical to
  `"new_tab"` — [src/app/Application.cpp](../../src/app/Application.cpp)
  treats both the same). The case exists so we catch regressions once
  true replace-in-place lands. Cross-ref TODO.md.

---

## Opening a file that is already open

**Context.** Owner HITL report, 2026-08-06 (macOS): typing a PDF's name
into Spotlight while Trailer already had that exact file open, and picking
the file, produced a **second window showing the same file** — "this feels
like I'm looking at two open files … opening it twice feels like *copying*
the document." Beyond the confusion it is a correctness hazard: two
`IDocument` instances over one path means two undo logs and two save paths
onto the same bytes, so whichever window saves last silently wins.

The rule these cases pin: **a file that is already open is surfaced, never
opened again.** Identity is the canonical on-disk path
([`src/util/PathKey.h`](../../src/util/PathKey.h)), so a symlink and its
target are one document. The rule is applied *before* `open_files_in`
routing — that preference decides where a **new** document lands, not
whether one document may exist twice — so it holds in all three modes.

### UAT-FND-053 — Re-opening an open file surfaces its window

**Preconditions:** `general.open_files_in = "new_window"`. One window
open showing `report.pdf`.
**Steps:**
1. Open `report.pdf` again — from Spotlight, Finder, `File > Open…`,
   `File > Open Recent`, or the command line.
**Expected:**
- **No** new window and **no** new tab appear; the window count is
  unchanged.
- The window already showing `report.pdf` comes to the front and becomes
  the active window (un-minimizing first if it was minimized).
- `report.pdf` is that window's current tab.
- `report.pdf` moves to the top of `File > Open Recent`, exactly as an
  ordinary open would.

G2 evidence: `docs/uat/images/2026-08-06-open-already-open-before.png` /
`docs/uat/images/2026-08-06-open-already-open-after.png` — the same file
opened, then asked for a second time, captured by the same harness slot
(`uat_fnd_053_090_openAlreadyOpenEvidence`) built against the tree before
and after the fix. Before: two windows, both titled
`Electrical service manual.pdf`. After: one.

### UAT-FND-054 — Same rule in `new_tab` mode

**Preconditions:** `general.open_files_in = "new_tab"`. One window open
showing `report.pdf`.
**Steps:**
1. Open `report.pdf` again.
**Expected:**
- No second tab for `report.pdf` is created; the tab count is unchanged.
- The existing `report.pdf` tab becomes current.

### UAT-FND-055 — Same rule in `same_window` mode

**Preconditions:** `general.open_files_in = "same_window"`. One window
open showing `report.pdf`.
**Steps:**
1. Open `report.pdf` again.
**Expected:**
- No new document is created; the window and tab counts are unchanged.
- The existing `report.pdf` is current.

### UAT-FND-056 — A symlink and its target are one document

**Preconditions:** `report.pdf` is open. `link.pdf` is a symlink pointing
at `report.pdf`. (Platform: macOS / Linux — creating symlinks on Windows
needs developer mode.)
**Steps:**
1. Open `link.pdf`.
**Expected:**
- No second document opens. The window already showing `report.pdf` is
  surfaced.

### UAT-FND-057 — Mixed batch: new files open, the open one is surfaced

**Preconditions:** `general.open_files_in = "new_window"`. `a.pdf` is
open. `b.pdf` and `c.pdf` are not.
**Steps:**
1. Open `b.pdf`, `c.pdf`, and `a.pdf` together (multi-select in the Open
   panel, one drag-and-drop, or one command line).
**Expected:**
- Two new windows appear (for `b.pdf` and `c.pdf`), not three.
- `a.pdf` is surfaced in the window that already held it; it is not
  opened a second time.
- Exactly one window/tab exists per distinct file.
- **Focus:** the batch is processed in order and the **last** entry
  decides what ends up in front — here `a.pdf`, so its existing window is
  frontmost. (Same rule as an all-new batch, where the last window shown
  is the frontmost one.)

### UAT-FND-058 — Transient imports are never merged

**Preconditions:** An image is on the clipboard.
**Steps:**
1. `File > New from Clipboard`.
2. `File > New from Clipboard` again, with the same image still on the
   clipboard.
**Expected:**
- Two separate untitled documents open. Nothing merges them, even though
  neither has a location the user chose and their pixels are identical.

---

## Clipboard

### UAT-FND-070 — Copy Page as Image

**Preconditions:** A document (a PDF page or an image) is open.
**Steps:**
1. `Edit > Copy Page as Image`.
**Expected:**
- The current page (or image) is rendered and placed on the system
  clipboard as an image, ready to paste into another app (chat, email).
- The action is disabled for documents that can't render a page raster
  (e.g. the stub adapter).

### UAT-FND-071 — A pasted HiDPI screen capture opens at its true size

**Context.** Owner dogfooding, 2026-08-02 (nightly
`0.3.1-dev+768.gce56b4b8`, macOS Retina): a macOS **window** screenshot
pasted with `File > New from Clipboard` opened at a reported **100%** zoom
but drew **2x too large**. The dpr recovery only matched a **whole-screen**
grab (raw pixels == a screen's full device resolution), so a window- or
region-sized capture fell through to dpr 1 and "Actual Size" mapped one
image pixel to one logical point.

Rationale and limits:
`docs/decision-records/2026-08-02-pasted-capture-scale-and-pixel-exact-zoom-stop.md`;
policy: `src/util/CaptureScale.h`.

**Preconditions:** A HiDPI (2x) screen.
**Steps:**
1. Capture a *window* to the clipboard with the OS screenshot tool.
2. `File > New from Clipboard`.
3. Separately: copy an ordinary non-capture image (a logo, a diagram) and
   repeat step 2.
**Expected:**
- The capture opens at Actual Size occupying `W/2 x H/2` **logical
  points** for a `W x H` device-pixel capture — i.e. the same on-screen
  size as the window it captured — and renders crisp (1 image pixel per
  screen device pixel).
- The ordinary pasted image is **not** shrunk: with nothing declaring a
  scale it opens at its natural logical size, unchanged. (A blanket
  "stamp the screen's dpr on every paste" was tried and reverted for
  exactly this reason.)
- A whole-screen grab keeps working as before.

Driven at policy level by the `recoverCaptureDpr` cases in
`tests/test_image_scale.cpp`.

**Known coverage limit (stated, not papered over).** The scale a capture
declares is read from the OS pasteboard
(`src/platform/ClipboardScale.h`), which only macOS answers today —
Windows and Linux expose nothing trustworthy through Qt, so a HiDPI
**window** screenshot pasted there still opens at dpr 1. The remedy on
those platforms is UAT-VWR-110's pixel-exact zoom stop, one keystroke
away and rendered unresampled. Neither the macOS pasteboard read nor the
Retina render is exercisable from the offscreen Linux/Windows harness;
both need a real-Mac check.

---

## macOS Finder integration (Platform: macOS)

### UAT-FND-060 — Open With → Trailer from Finder

**Preconditions:** Trailer built as a `.app` bundle and set as the
default opener for the test file type, or reachable via Finder's
"Open With" menu.
**Steps:**
1. Right-click a PDF in Finder, choose Open With → Trailer.
**Expected:**
- If Trailer is not running, it launches and opens the file.
- If Trailer is running, the existing instance opens the file via
  `QFileOpenEvent` (no second instance is spawned). This is
  macOS-specific — see UAT-XCT-050 for the Windows / Linux
  counterpart.
- The file appears in `File > Open Recent`.

### UAT-FND-061 — Drag file onto Trailer's Dock icon

**Preconditions:** Trailer is running.
**Steps:**
1. Drag a file from Finder onto the Trailer Dock icon.
**Expected:**
- The file opens in the running Trailer instance (via `QFileOpenEvent`).
- Identical routing to Finder Open With.

### UAT-FND-063 — Dock icon right-click shows the 10 most recent files (while running)

**Preconditions:** Trailer is running with more than 10 files in its
recent list (some files may have since been moved/deleted).
**Steps:**
1. Right-click (or Control-click, or click-and-hold) the Trailer Dock
   icon.
**Expected:**
- The Dock menu shows up to the **10** most-recently-opened files,
  most-recent-first, above the standard Show/Hide/Quit items.
- Files no longer present on disk are **not** listed (no dead entries in
  system chrome Trailer can't grey out or tooltip — see
  `RecentFiles::existingEntries`).
- Choosing an entry opens that file in the running instance, identical to
  `File > Open Recent`.
- Clearing recents (`File > Open Recent > Clear Menu`) empties the Dock
  menu too — both surfaces are driven by the same `RecentFiles` model
  (`Application::refreshDockRecents`).
- **Evidence tier:** the menu's *construction* (item count, order,
  existence-filtering) is covered headlessly by `uat_fnd_063_*` in
  `tests/uat/test_uat_dock_recents.cpp` via `grab()` of the constructed
  `QMenu`; the menu's live *attachment to the real Dock* is native
  Dock-chrome and needs a real Mac to confirm visually (`grab()` cannot
  render Dock chrome).

### UAT-FND-064 — Dock icon recents survive Trailer quitting (Known gap: real-Mac required)

**Preconditions:** Trailer has opened at least one file, then fully quit
(`⌘Q`, not just all windows closed).
**Steps:**
1. With Trailer NOT running, right-click the Trailer Dock icon.
**Expected:**
- The same recents list from UAT-FND-063 still appears, sourced from the
  macOS system Recent Documents store (`NSDocumentController`,
  `src/platform/DockRecents.mm`) rather than Trailer's own in-process
  state — Trailer's process is dead, so nothing in Trailer is serving
  this menu; it is macOS/Launch Services rendering what Trailer last
  registered via `noteNewRecentDocumentURL:`.
- Choosing an entry launches Trailer with that file open.
- **Evidence tier: real-Mac required.** This is exactly the surface a
  headless/offscreen harness cannot exercise (there is no live process to
  drive, by definition) — see the owner verification checklist in the PR
  that introduced this case.

### UAT-FND-062 — Command-menu reparenting

**Preconditions:** Trailer running on macOS.
**Steps:**
1. Look at the screen menu bar.
**Expected:**
- The menu sits at the top of the screen, not inside the window frame.
- `Trailer > About Trailer` and `Trailer > Quit Trailer` appear under
  the application menu.
- `Trailer > Preferences…` may be absent (we don't yet have a
  preferences UI — cross-ref Phase 8).

---

## Known gaps

These cases describe behaviour that does not yet work. Include them so
we notice the day they start working and can flip their status.

### UAT-FND-090 — Tab drag-detach to new window (Known gap)

**Preconditions:** App launched with two or more tabs in one window.
**Steps:**
1. Drag a tab out of the tab bar and drop it on empty space.
**Expected (future):** a new window is created containing that tab.
**Current:** the drag is ignored or the tab snaps back. The app does
not crash.

### UAT-FND-091 — Preferences dialog (Known gap)

**Preconditions:** App launched.
**Steps:**
1. Look for a Preferences / Settings menu entry.
**Expected (future):** a Preferences dialog exposes every setting.
**Current:** no such dialog. Users must edit `settings.toml` by hand.

### UAT-FND-092 — Dirty-close confirmation prompt (Known gap)

**Preconditions:** Open a document, edit it so the tab title starts
with `• `.
**Steps:**
1. Close the tab via the tab's `×` button.
2. Or close the window via `File > Close Window` / the red close button
   / `Cmd+W` / `Ctrl+W`.
3. Or quit via `File > Quit` / `Cmd+Q` / `Ctrl+Q`.
**Expected (future):** a Save / Discard / Cancel prompt appears for
each dirty document, and cancelling aborts the close.
**Current:** close proceeds unconditionally; edits are discarded with
no warning. Applies to tab close, window close, and quit. Cross-ref
UAT-FND-014, UAT-VWR-011, UAT-XCT-041.
