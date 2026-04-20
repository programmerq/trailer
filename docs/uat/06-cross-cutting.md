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

Platform support varies:

- **macOS** uses `screencapture -i` (full / window / region pickers).
- **Linux** falls back to `gnome-screenshot` when available.
- **Windows** currently only supports full-screen (see UAT-XCT-015).

### UAT-XCT-010 — Take Screenshot launches the picker (macOS)

**Preconditions:** App running on macOS. Any or no document open.
**Steps:**
1. `Tools > Take Screenshot` (`Ctrl+Shift+3`).
**Expected:**
- A platform screenshot picker appears (crosshair / window target,
  depending on the mode prompt).
- Choosing an area saves a PNG and either opens it as a new tab or
  writes to a default location (document actual behaviour).

### UAT-XCT-011 — Screenshot integrates with the app

**Preconditions:** macOS. App running with no documents.
**Steps:**
1. `Tools > Take Screenshot`.
2. Select a region.
**Expected:**
- A new tab opens with the captured image (if the workflow opens it
  in-app).
- The image is editable (zoom, crop, annotate, etc.).

### UAT-XCT-012 — Cancel screenshot

**Preconditions:** Screenshot picker open.
**Steps:**
1. Press Escape (or the platform's cancel binding).
**Expected:**
- Picker closes. No tab opened. App unchanged.

### UAT-XCT-013 — Screenshot on Linux with `gnome-screenshot`

**Preconditions:** Linux. `gnome-screenshot` installed.
**Steps:**
1. `Tools > Take Screenshot`.
**Expected:**
- `gnome-screenshot` launches (may open its own UI).
- On completion, the resulting PNG path is ingested into Trailer (if
  that is the flow — document actual behaviour).

### UAT-XCT-014 — Screenshot on Linux without `gnome-screenshot`

**Preconditions:** Linux without `gnome-screenshot` on `$PATH`.
**Steps:**
1. `Tools > Take Screenshot`.
**Expected:**
- An error or fallback message is shown.
- The app does not crash.

### UAT-XCT-015 — Screenshot on Windows (Known gap)

**Preconditions:** Windows.
**Steps:**
1. `Tools > Take Screenshot`.
**Expected (future):** region / window / full-screen picker like macOS.
**Current:** full-screen capture only. Cross-ref TODO.md.

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
| `Cmd/Ctrl+F` | `Edit > Find…` |
| `Cmd/Ctrl+G` (macOS) / `F3` | `Edit > Find Next` |
| `Cmd+Shift+G` (macOS) / `Shift+F3` | `Edit > Find Previous` |

### UAT-XCT-022 — View menu shortcuts

| Shortcut | Action |
|---|---|
| `Ctrl+Shift+D` | `View > Toggle Sidebar` |
| `Ctrl+Shift+A` | `View > Toggle Markup Toolbar` |
| `Ctrl+Shift+I` | `View > Toggle Inspector` |
| `Page Up` | `View > Previous Page` |
| `Page Down` | `View > Next Page` |
| `Cmd/Ctrl++` (or `Ctrl+=`) | `View > Zoom In` |
| `Cmd/Ctrl+-` | `View > Zoom Out` |
| `Ctrl+0` / `Cmd+0` | `View > Actual Size` |
| `Ctrl+1` / `Cmd+1` | `View > Fit to Width` |
| `` ` `` (backtick) | `View > Magnifier` |

### UAT-XCT-023 — Tools menu shortcuts

| Shortcut | Action |
|---|---|
| `Ctrl+L` | `Tools > Rotate Left` |
| `Ctrl+R` | `Tools > Rotate Right` |
| `Ctrl+Shift+3` | `Tools > Take Screenshot` |

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

### UAT-XCT-041 — Quit with multiple windows

**Preconditions:** Two windows open, each with a dirty document.
**Steps:**
1. `File > Quit`.
**Expected:**
- User is prompted for each dirty document (save / discard / cancel).
- Cancelling any prompt aborts the quit.
- Accepting all saves and quits everything.

---

## Process lifecycle

### UAT-XCT-050 — Second launch with an arg attaches to running instance (macOS)

**Preconditions:** Trailer running on macOS.
**Steps:**
1. From another terminal, run `open -a Trailer file.pdf`.
**Expected:**
- The running Trailer opens `file.pdf` (no second `.app` instance
  spawns).
- File added to Recent.

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
