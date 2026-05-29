# UAT — Cross-Cutting

Covers the features that don't belong to a single document type:
screenshot, theme, and a consolidated keyboard shortcut reference used
as a regression sweep.

Per-feature behaviour is in the phase docs; this file captures the
overall integrations.

---

## Theme

Theme is controlled by `general.theme` in `settings.toml`, one of
`"system"`, `"light"`, `"dark"`. There is no in-app preference dialog
yet (see UAT-FND-091); users must edit the file and restart.

### UAT-XCT-001 — `theme = "system"` follows OS

**Preconditions:** Set `theme = "system"` in `settings.toml`. OS is in
light mode.
**Steps:**
1. Launch the app.
**Expected:**
- Window chrome, menu bar, and viewport background use light-theme
  colours.
2. Switch the OS to dark mode (platform-specific — macOS: System
   Settings > Appearance; Windows: Settings > Personalisation; Linux:
   DE-specific).
**Expected:**
- The app tracks the OS theme either live or on next launch (document
  actual behaviour — live tracking is preferred but not guaranteed).

### UAT-XCT-002 — `theme = "dark"` overrides OS

**Preconditions:** Set `theme = "dark"`. OS in light mode.
**Steps:**
1. Launch.
**Expected:**
- App is in dark theme regardless of OS.

### UAT-XCT-003 — `theme = "light"` overrides OS

Symmetric to UAT-XCT-002.

### UAT-XCT-004 — Invalid theme value

**Preconditions:** Set `theme = "neon"` (or any unsupported value).
**Steps:**
1. Launch.
**Expected:**
- App launches without crash.
- Falls back to system or a documented default.
- Invalid value is either preserved in the file or replaced with the
  default on next save.

---

## Screenshot (Tools > Take Screenshot)

The flow is the same on every platform: a Qt modal prompts for
`Whole screen` / `Single window` / `Region`, then the app hides itself
and performs the capture. The resulting PNG is opened as a new tab via
the same path as `File > Open`. Implementation lives at
[src/ui/MainWindow.cpp](../../src/ui/MainWindow.cpp) around line 730.

Platform backends:

- **macOS** — shells out to `/usr/sbin/screencapture` with `-iW`
  (window) or `-i -s` (region). The native macOS picker appears once
  the Trailer prompt is accepted.
- **Linux / Windows** — `QScreen::grabWindow(0)` captures the primary
  screen. The `Window` and `Region` radios are disabled and an
  explanatory note is shown in the prompt.

There is no `gnome-screenshot` fallback, nor any Windows-specific
capture API — both platforms go through Qt's generic screen grab.

### UAT-XCT-010 — Take Screenshot prompt opens

**Preconditions:** App running. Any or no document open.
**Steps:**
1. `Tools > Take Screenshot` (`Ctrl+Shift+3`).
**Expected:**
- A Trailer modal dialog appears with three radio buttons:
  `Whole screen`, `Single window (click to select)`,
  `Region (drag to select)`.
- **Platform: macOS** — all three radios are enabled.
- **Platform: Linux / Windows** — only `Whole screen` is enabled; an
  inline note explains the limitation and points to TODO.md.

### UAT-XCT-011 — Whole-screen capture opens a new tab (all platforms)

**Preconditions:** App running.
**Steps:**
1. `Tools > Take Screenshot`.
2. Accept with `Whole screen` selected.
**Expected:**
- The main window briefly hides, the primary screen is captured, and
  a new tab opens containing the resulting PNG.
- The PNG path is also appended to `File > Open Recent`.
- The image is editable (zoom, crop, annotate).

### UAT-XCT-012 — Window capture (Platform: macOS)

**Preconditions:** macOS. Multiple windows visible (at least one other
than Trailer's).
**Steps:**
1. `Tools > Take Screenshot`, pick `Single window`, accept.
**Expected:**
- Trailer hides; macOS's crosshair/window picker from `screencapture
  -iW` takes over.
- Clicking a window captures it; Trailer reappears and opens the PNG
  as a new tab.

### UAT-XCT-013 — Region capture (Platform: macOS)

**Preconditions:** macOS.
**Steps:**
1. `Tools > Take Screenshot`, pick `Region`, accept.
2. Drag-select a region.
**Expected:**
- Trailer hides; macOS's drag-region picker from `screencapture -i -s`
  takes over.
- Releasing the drag captures that region; Trailer reappears and opens
  the PNG as a new tab.

### UAT-XCT-014 — Cancel native picker (Platform: macOS)

**Preconditions:** macOS. Native `screencapture` picker active
(Window or Region mode).
**Steps:**
1. Press Escape.
**Expected:**
- Picker cancels; `screencapture` exits with no output file.
- Trailer reappears. No new tab is opened. No error dialog is shown —
  cancellation is not treated as a failure.

### UAT-XCT-015 — Cancel Trailer's prompt (all platforms)

**Preconditions:** `Take Screenshot` dialog open.
**Steps:**
1. Click `Cancel` (or press Escape on the Qt dialog).
**Expected:**
- Dialog closes. No capture runs. No tab opens. Window never hides.

### UAT-XCT-016 — Window/Region radios on non-macOS (Known gap)

**Preconditions:** Linux or Windows.
**Steps:**
1. Open the `Take Screenshot` dialog.
**Expected:**
- `Single window` and `Region` radio buttons are disabled.
- Forcing them (e.g. via a future config) and accepting shows an
  `Unsupported` information dialog rather than capturing. Cross-ref
  TODO.md.

---

## Keyboard shortcut matrix

Run through every shortcut in order on a fresh app launch with a
multi-page PDF and an image each open in separate tabs. Verify each
triggers the documented action. Platform column gives the canonical
form — `Cmd` on macOS, `Ctrl` on Windows / Linux.

### UAT-XCT-020 — File menu shortcuts

| Shortcut | Action |
|---|---|
| `Cmd/Ctrl+O` | `File > Open…` |
| `Cmd/Ctrl+S` | `File > Save` |
| `Cmd/Ctrl+Shift+S` | `File > Save As…` |
| `Cmd/Ctrl+P` | `File > Print…` |
| `Cmd/Ctrl+W` | `File > Close Window` |
| `Cmd/Ctrl+Q` | `File > Quit` |

### UAT-XCT-021 — Edit menu shortcuts

| Shortcut | Action |
|---|---|
| `Cmd/Ctrl+Z` | `Edit > Undo` |
| `Cmd+Shift+Z` (macOS) / `Ctrl+Y` | `Edit > Redo` |
| `Cmd/Ctrl+A` | `Edit > Select All` |
| `Cmd/Ctrl+F` | `Edit > Find…` |
| `Cmd/Ctrl+G` (macOS) / `F3` | `Edit > Find Next` |
| `Cmd+Shift+G` (macOS) / `Shift+F3` | `Edit > Find Previous` |

### UAT-XCT-022 — View menu shortcuts

| Shortcut | Action | Notes |
|---|---|---|
| `Ctrl+Shift+D` | `View > Toggle Sidebar` | Qt `Ctrl` → `Cmd` on macOS |
| `Ctrl+Shift+A` | `View > Toggle Markup Toolbar` | |
| `Ctrl+Shift+I` | `View > Toggle Inspector` | |
| `Page Up` | `View > Previous Page` | |
| `Page Down` | `View > Next Page` | |
| `Cmd/Ctrl++` **or** `Cmd/Ctrl+=` | `View > Zoom In` | Two shortcuts bound: `QKeySequence::ZoomIn` (the `+` form) and an explicit `Ctrl+=`/`Cmd+=` so users don't need Shift on US layouts |
| `Cmd/Ctrl+-` | `View > Zoom Out` | `QKeySequence::ZoomOut` |
| `Ctrl+0` / `Cmd+0` | `View > Actual Size` | |
| `Ctrl+1` / `Cmd+1` | `View > Fit to Width` | |
| `` ` `` (backtick) | `View > Magnifier` | Same key on all platforms |

### UAT-XCT-023 — Tools menu shortcuts

| Shortcut | Action | Notes |
|---|---|---|
| `Ctrl+L` | `Tools > Rotate Left` | Qt `Ctrl` → `Cmd` on macOS |
| `Ctrl+R` | `Tools > Rotate Right` | |
| `Ctrl+Shift+3` | `Tools > Take Screenshot` | **Platform: macOS** — Qt maps this to `Cmd+Shift+3`, which is also macOS's native full-screen capture shortcut. The OS typically intercepts it, so the Trailer action may not fire from the keyboard — use the menu item or change the shortcut. Tracked in TODO.md. |

### UAT-XCT-024 — Annotation keys

| Shortcut | Action |
|---|---|
| `Delete` / `Backspace` | Delete selected annotation (or selected page in Sidebar) |
| `Escape` | Cancel inline editor / dismiss search bar |
| `Ctrl+Return` / `Cmd+Return` | Commit inline text editor |

### UAT-XCT-025 — Shortcut disabled states match menu

**Preconditions:** No document open.
**Steps:**
1. Try every doc-specific shortcut above.
**Expected:**
- Shortcuts for disabled menu items do not trigger anything.
- App does not crash.

---

## Dock layout persistence

### UAT-XCT-030 — Dock geometry persists across restart (Known gap)

**Preconditions:** App launched.
**Steps:**
1. Move / resize the Sidebar and Inspector docks.
2. Quit.
3. Relaunch.
**Expected (future):** the dock layout reopens matching the saved
geometry.
**Current:** docks reopen in their default positions (no persistence
yet). This is a known gap — the case exists so we notice when it
lands.

---

## Multi-window behaviour

### UAT-XCT-040 — Open in new window

**Preconditions:** `open_files_in = "new_window"`. One window open with
a PDF.
**Steps:**
1. `File > Open…`, pick another file.
**Expected:**
- Two independent windows exist, each with its own menu bar, tabs,
  Sidebar, Inspector.
- Edits in one window do not affect the other.
- Closing one leaves the other running.

### UAT-XCT-041 — Quit with multiple windows (Known gap)

**Preconditions:** Two windows open, each with a dirty document.
**Steps:**
1. `File > Quit`.
**Expected (future):**
- User is prompted for each dirty document (save / discard / cancel).
- Cancelling any prompt aborts the quit.
- Accepting all saves and quits everything.

**Current:** all windows close unconditionally; edits are discarded
with no warning. Cross-ref UAT-FND-092.

---

## Layout robustness

### UAT-XCT-060 — Layout survives font scaling and RTL

**Preconditions:** A document is open.
**Steps:**
1. (Automated sweep) Realize the main window across a matrix of
   application font sizes and layout directions (LTR / RTL).
**Expected:**
- No visible interactive control collapses to zero size or renders
  narrower/shorter than the size it needs for its content — i.e.
  nothing clips or gets squashed under large system fonts or
  right-to-left mirroring. Driven by `tests/uat/test_uat_sweep.cpp`.

---

## Process lifecycle

### UAT-XCT-050 — Second launch with an arg attaches to running instance (Platform: macOS)

**Preconditions:** Trailer running on macOS.
**Steps:**
1. From another terminal, run `open -a Trailer file.pdf`.
**Expected:**
- The running Trailer opens `file.pdf` via `QFileOpenEvent` in
  [src/app/Application.cpp](../../src/app/Application.cpp) — no second
  `.app` instance spawns. This is provided for free by macOS's app
  bundle launcher.
- File added to Recent.

### UAT-XCT-052 — Second launch on Linux / Windows spawns a new process (Known gap)

**Preconditions:** Trailer running on Linux or Windows.
**Steps:**
1. From another terminal / shortcut, run `trailer file.pdf` (or
   `trailer.exe file.pdf`).
**Expected (future):** the running instance opens the file; no second
window/process spawns.
**Current:** a second Trailer process starts, independent from the
first. There is no IPC / single-instance guard — Trailer has no
equivalent of macOS's `QFileOpenEvent` on these platforms. Cross-ref
TODO.md.

### UAT-XCT-051 — Crash does not corrupt settings (smoke)

**Preconditions:** `settings.toml` contains valid content.
**Steps:**
1. Launch app.
2. Kill the process with `SIGKILL` (or equivalent).
3. Relaunch.
**Expected:**
- `settings.toml` is still valid; the app launches normally.
- Recent files list is intact up to the last persisted write.
- No partial / truncated file is left behind.
