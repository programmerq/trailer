# ux-walkthrough drive harness (Tier 1 — Linux/Xvfb)

This is the **DRIVE + CAPTURE** half of the `ux-walkthrough` review gate. It
drives the **real built `trailer` binary** through the nine ux-walkthrough
golden paths under a **real X server (Xvfb) with a real window manager
(openbox)**, and captures a screenshot after every scripted step into a
per-run artifact directory. The **JUDGE** half is persona (A) in
[`.claude/skills/ux-walkthrough/SKILL.md`](../../.claude/skills/ux-walkthrough/SKILL.md),
which reads those per-step bundles and rates each step against the
cognitive-walkthrough questions.

It closes the backlog item
`docs/backlog/2026-07-17-ux-walkthrough-drive-harness.md`.

## Why Xvfb + xdotool, not `QT_QPA_PLATFORM=offscreen`

The backlog item sketched an in-process QTest/offscreen tier. This
implementation deliberately uses a **real X server** instead, because the
whole point of the walkthrough gate is **real window-manager / focus / menu /
modal behaviour** — the class of bug where a control or modal vanishes. Two
concrete things prove the difference:

- The **unsaved-changes close prompt** is *deliberately skipped* under the
  `offscreen`/`minimal` platforms (`src/ui/MainWindow.cpp` `closeEvent` /
  `confirmCloseDirtyDoc`) and fires **only** on a real platform such as `xcb`.
  An offscreen tier is structurally blind to golden path 4.
- Without a window manager, Qt under Xvfb sets no `WM_CLASS` and
  `_NET_ACTIVE_WINDOW` is unavailable, so focus/activation and modal stacking
  don't behave like a user's session. openbox restores that.

Input is real X events via **xdotool**; capture is the whole X root (so open
menus and modal dialogs are included) via ImageMagick `import` (xwd fallback).

## Resilient selectors (and the PR #86 dependency)

The scripts drive the app through **keyboard shortcuts** (`Ctrl+O`, `Ctrl+=`,
`Ctrl+W`, …) plus **Qt `objectName`s** for identifying widgets — never pixel
coordinates and never menu *label text*. Most shortcuts are stable across **PR
#86's File-menu information-architecture rename**, which moves/renames menu items
but keeps their `QKeySequence` bindings. The one exception is **Tools → Take
Screenshot**: #86 deliberately *removed* its OS-reserved `⌘⇧3` binding as a lying
control (DR `2026-07-18-file-menu-acquire-ia`), so that action now has a stable
`objectName` but no shortcut. Golden path 2 therefore reaches it with the
`menupick` verb (open the menu by its Alt mnemonic, trigger the item by its own
mnemonic — `Alt+T`, then `T`), which is likewise stable across the IA rename and
uses no pixel geometry. The other three paths drive purely via shortcuts +
`objectName`s.

**Tools vs File — which capture action path 2 drives (verified 2026-07-20).**
Post-#86 there are *two* distinct capture entry points, and path 2 targets the
Tools one deliberately: **Tools → Take Screenshot** (objectName
`action.tools.takeScreenshot`, `MainWindow::onTakeScreenshot`) opens the
**"Take Screenshot" capture-mode dialog** and is the action carrying the stable
`objectName` this path asserts on — #86 did *not* move or rename it, it only
stripped its dead `⌘⇧3` shortcut. Separately, **File → Screenshot** (the
`Whole Screen / Window / Selected Area` submenu added by #86 via
`Application::addAcquireItems`) is the DR's canonical *discoverable* acquire
surface, but its items carry **no** `objectName` and fire capture **directly
with no dialog**. An audit note reading "canonical path is File → Screenshot"
refers to that discoverable submenu; the dialog-bearing, `objectName`'d action
path 2 exercises lives under **Tools**, so path 2 stays on `menupick t t`.

For any future AT-SPI / QTest tier that wants to target widgets by name, the
actions the golden paths touch now carry stable `objectName`s (added in
`src/ui/MainWindow.cpp`): `action.file.open` / `action.file.save` /
`action.file.close`, `action.view.zoomIn` / `.zoomOut` / `.actualSize` /
`.nextPage` / `.previousPage`, `action.tools.rotateRight` /
`action.tools.takeScreenshot`, plus the pre-existing `zoomIndicator`.
**#86 conflict note (resolved):** #86 renamed/moved these File-menu actions and
removed the `⌘⇧3` screenshot binding; the merge re-attached every `objectName`
above to its (possibly renamed) action and switched path 2 to `menupick` for the
now-shortcut-less Take Screenshot. Shortcut-based driving keeps working for the
other actions, whose bindings #86 preserved.

## Requirements

Linux with:

```sh
apt-get install xvfb x11-apps xdotool imagemagick xclip libxcb-cursor0 openbox
```

- `xvfb` — headless X server (`xvfb-run`)
- `openbox` — lightweight WM (required, see above)
- `xdotool` — real X input events + window queries
- `imagemagick` — `import` (screenshot) and `convert` (fixtures); `x11-apps`
  provides `xwd` as the capture fallback
- `xclip` — puts an image on the X clipboard for golden path 1
- `libxcb-cursor0` — **required**: Qt 6's `xcb` platform plugin fails to
  load at runtime without it (not otherwise pulled by the toolchain layer)

These are baked into the CI runner image (`docker/runner/Dockerfile`).

Build the binary first:

```sh
cmake -S . -B build -G Ninja && cmake --build build -j
```

## Running it

```sh
# all nine golden paths
tools/ux-walkthrough/run.sh all

# a subset
tools/ux-walkthrough/run.sh 01 04 07

# options
tools/ux-walkthrough/run.sh all --bin build/trailer --out /tmp/uxw --geometry 1400x1000
```

Artifacts land under `uat-screenshots/ux-walkthrough/<timestamp>/<path>/`
(the `uat-screenshots/` tree is gitignored — working captures never land in
git). Each step emits:

- `NN-<label>.png` — the screenshot
- `NN-<label>.txt` — the metadata bundle: step label, **expected effect**,
  notes, any documented **boundary**, and the window inventory at capture time

That per-step bundle is the exact shape persona (A) consumes.

## Selector techniques added by paths 05–09

Paths 05–09 keep the resilient-selector rule (shortcuts + `objectName`s +
`menupick` mnemonics, never label-text matching) and add three drive techniques
for surfaces that have no shortcut/objectName handle. Each is documented inline
in its path header with the fragility it carries:

- **In-window banner, screenshot-verified (06).** The external-file-change
  `FileChangeBanner` (objectName `fileChangeBanner`) is a `QFrame` *inside* the
  document window, **not** a top-level X window, so `assert_window` cannot see
  it. Path 06 verifies it by screenshot + note (its text/buttons are fixed in
  `src/ui/FileChangeBanner.cpp`); the X-level asserts it *can* make are the
  window-title dirty marker (edit survives a conflict) and fixture stem.
- **Pixel-offset toolbar click (07).** The Freehand markup tool has no shortcut
  and the toolbar is icon-only, so path 07 clicks it by an offset **from the
  window origin** (not absolute screen coords, so it survives WM placement).
  The offset is **client-relative** in this harness (`getwindowgeometry` on the
  Qt window reports the client origin under Xvfb+openbox), tuned by sweeping the
  toolbar until a freehand *squiggle* (multi-point path, not a straight
  Line/Arrow) rendered and dirtied the title. Drawing itself is a real canvas
  mouse-drag — the legitimate user act, not a control selector.
- **Focus-stable arrow-key menu nav (09).** The File ▸ Screenshot submenu items
  have no shortcut, objectName, or Alt mnemonic. Qt menu *type-ahead* did not
  register under this session, so path 09 drives the submenu by **arrow keys**
  (Down ×2 from the auto-highlighted `Open…` to `Screenshot`, Right to open,
  Return on `Whole Screen`). Critically it sends these with **raw `xdotool key`
  after activating once** — the harness `press()`/`menu()` verbs call
  `activate()`, which re-raises the main window and *dismisses the open menu*, so
  they must not be used mid-menu.

## The nine golden paths and what "pass" looks like

A review agent runs the harness, then reads each path's `NN-*.png` +
`NN-*.txt` and judges with persona (A). Per-step pass criteria:

### 01 — new-from-clipboard
| step | screenshot should show | pass |
|---|---|---|
| 01 empty-state-clipboard-primed | app empty state; `.txt` confirms N bytes of `image/png` on the clipboard | image really on clipboard; empty state visible |
| 02 clipboard-image-opens | the clipboard image open as a document, `100%` in the status bar | the *same* image is visible at the oracle zoom |

**Boundary:** the clipboard-consuming *menu action* (`New from Clipboard`,
⌘-bound) is **macOS-only** (`src/app/Application.cpp`, `#ifdef Q_OS_MACOS`,
`installNoWindowMenuBar`/`newFromClipboard`, ~lines 389–560) — **not compiled on
Linux**. So this path proves the clipboard *input* half and reproduces the exact
*outcome* of `Application::newFromClipboard` (clipboard image → temp PNG →
`openFiles`); the ⌘N binding + menu wiring themselves route to the owner
real-Mac checklist in the skill.

### 02 — screenshot-acquire
| step | screenshot should show | pass |
|---|---|---|
| 01 empty-state | empty state (drop target + Open File…) | G5 empty state visible |
| 02 take-screenshot-dialog | the "Take Screenshot" modal; **Whole screen** enabled, **Single window / Region disabled with an explanatory note** | modal present; disabled controls are non-lying (G3) |
| 03 captured-image-opens | the captured X screen open as a document | capture opened as a document |

**Boundary:** window/region capture are disabled on Linux (a G3
non-lying-control state); the macOS native TCC "Screen Recording" prompt is
real-Mac only. Whole-screen capture genuinely runs under Xvfb via
`QScreen::grabWindow(0)` — the path creates `~/Pictures` first, because the
Linux capture saves to `QStandardPaths::PicturesLocation` and a bare CI
container has no XDG user dirs (a real desktop always does); with the dir
present, grab → save → open-as-document runs end-to-end and step 3's assert is
on the `trailer-screenshot-…` document stem, not a bare `Trailer`.

### 03 — open-image → zoom → navigate
| step | screenshot should show | pass |
|---|---|---|
| 01 opened | image at open, `100%` readout | opened at oracle zoom |
| 02–03 zoom-in ×2 | readout climbing (e.g. `305%`), scrollbars appear | **zoom-% readout updates (finding #5, H1)** |
| 04 zoom-out | readout decreasing | readout updates downward |
| 05 actual-size | readout back to `100%` | Ctrl+0 resets |
| 06 navigate-next | unchanged (single image) | next/prev correctly inert |

**Boundary:** Trailer has **no cross-file next/previous-image navigation**;
next/previous is *page* navigation, gated on `pageCount>1`
(`updateActionStates`). For a single image the actions are correctly disabled.

### 04 — close-with-unsaved → prompt
| step | screenshot should show | pass |
|---|---|---|
| 01 opened-clean | image open, no `•` in the title | clean state |
| 02 edited-dirty | image rotated; title gains `• ` | edit registered as dirty |
| 03 close-prompts | the **"Unsaved changes" Save/Discard/Cancel modal** | the modal offscreen capture is blind to appears |
| 04 cancel-keeps-work | modal gone, doc still open and still `•` | Cancel aborts the close |

This path is the reason Tier-1 uses a real X server (see top).

### 05 — menu-IA + new-from-clipboard (PR #86)
| step | screenshot should show | pass |
|---|---|---|
| 01 file-menu-no-doc | File menu open in the empty state: **New from Clipboard disabled** (empty clipboard), Open, Open Recent, **Screenshot ▸**, Scanner/Camera disabled | create/acquire group present with a no-doc; disabled items are non-lying (G3) |
| 02 text-clipboard-menu-disabled | File menu with plain **text** on the clipboard: New from Clipboard **still disabled** | non-image clipboard is not openable (`inspectClipboard`) |
| 03 text-clipboard-silent-noop | after `Ctrl+N` with text: still empty state, **no dialog**, no new window | silent no-op (PHILOSOPHY → *No popup that just says no*) |
| 04 image-clipboard-menu-enabled | File menu with an **image** on the clipboard: New from Clipboard **enabled** | enable-gate tracks clipboard content |
| 05 image-clipboard-opens-doc | `Ctrl+N` opens the clipboard image as a `trailer-clipboard-…` document | real Linux new-from-clipboard runs end-to-end via Qt's clipboard read |
| 06 file-menu-with-doc | with a document open, the create/acquire group is **still present** | #86 anti-vanish fix (group no longer disappears once a window is key) |

**Selectors:** File menu via `Alt+F`; New from Clipboard via `Ctrl+N`
(`QKeySequence::New`). **Note (supersedes path 01's boundary):** #86 made
`Application::addNewFromClipboardAction` + `newFromClipboard` **cross-platform**
(`MainWindow::buildMenus` calls them on every OS; the `#ifdef Q_OS_MACOS` is only
the *no-window* menu bar), so the Linux `Ctrl+N` clipboard-open this path drives
is real, not a reproduction. The macOS no-window menu bar stays owner real-Mac.

### 06 — external file-change flows (PR #89)
| step | screenshot should show | pass |
|---|---|---|
| 01–02 clean → auto-reload | a clean doc (VERSION A) whose file is overwritten on disk **silently reloads to VERSION B**, "Reloaded — the file changed on disk." status, **no banner** | clean external change auto-reloads Preview-style |
| 03–04 dirty → conflict banner | a dirty doc (rotated, title `•`) whose file is overwritten shows the amber **"changed by another program while you had unsaved edits"** banner — Reload / Keep mine / **Compare (disabled, G3)** — edit intact | never auto-decides; no silent clobber |
| 05 seam-menu-over-banner | File menu opens cleanly on top of the banner | #86 menu × #89 banner coexist |
| 06–07 delete underneath | deleting the file shows **"deleted on disk … Save to recreate it"** banner with a Save button; document stays open | delete-underneath keeps the buffer |

**Boundary:** the banner is an **in-window `QFrame`** (`fileChangeBanner`), not
an X window, so it is **screenshot-verified**, not `assert_window`'d; the
X-level asserts are the title dirty marker + fixture stem. Debounce ~250 ms
(`ExternalChangeMonitor kDebounceMs`), so each disk mutation is followed by a
>1 s settle before capture.

### 07 — freehand draw → zoom → draw-over (PR #91)
| step | screenshot should show | pass |
|---|---|---|
| 01 opened-actual | grid image at `Ctrl+0` 100% baseline | known 1:1 before drawing |
| 02 freehand-selected | markup toolbar shown (`Ctrl+Shift+A`), Freehand picked | tool active |
| 03 stroke-at-100 | a freehand **squiggle** on the grid; title gains `•` | stroke registers + dirties the doc |
| 04 zoom-in-anchoring | `Ctrl+=` ×2: the existing stroke stays **glued to the same grid intersections** | discrete-zoom anchoring holds (no drift) |
| 05 draw-over-at-zoom | Freehand re-picked (auto-reverts to Select on commit), a **2nd stroke** drawn at the zoomed level | draw-over lands under the cursor |
| 06 roundtrip-100 | `Ctrl+0`: both strokes at their true doc coords | zero drift round-trip |

**Selector (fragile, documented):** Freehand has no shortcut/menu, so it is
clicked by a **client-relative pixel offset** (`TOOL_FREEHAND_DX/DY`); if the
markup toolbar layout changes, re-tune those. dpr=1 under Xvfb; the residual #91
Retina pinch-zoom drift is owner real-Mac.

### 08 — zoom indicator / steps / open-at-100% (#76/#80/#88)
| step | screenshot should show | pass |
|---|---|---|
| 01 open-default-zoom | 800×600 grid at open; **judge measures a cell (px/100) vs the readout** | render and readout must agree |
| 02 actual-size-truth | `Ctrl+0` = 100%, 800 px wide | truth anchor |
| 03–05 zoom-in ladder | readout climbs ~×1.25 each `Ctrl+=` (≈125/156/195%) | readout tracks each step |
| 06 zoom-out | readout drops one notch | tracks downward |
| 07 zoom-floor-clamp | readout bottoms out at **5%** | visible min clamp (`kZoomMin`) |
| 08 zoom-ceiling-clamp | readout tops out at **3200%** | visible max clamp (`kZoomMax`) |

**Known finding (referenced, not a harness failure):** backlog
`2026-07-19-image-open-zoom-readout-mismatch.md` — on open the readout can print
a value (e.g. `5%`) that disagrees with the true render magnification; path 08 is
the focused capture of that discrepancy for the judge. **Perf caveat (F6):** at
extreme zoom the full-res pixmap rebuild is slow and can drop rapid keys, so on a
slow host the ceiling step may under-reach 3200% — a perf artifact, not a broken
clamp. `SETTLE_MS` is bumped to 900 ms for this path.

### 09 — File → Screenshot → Whole Screen (direct, dialogless; PR #86)
| step | screenshot should show | pass |
|---|---|---|
| 01 empty-state | G5 empty state | start state |
| 02 file-menu-open | File menu with the **Screenshot ▸** acquire submenu | discoverable acquire surface present |
| 03 screenshot-submenu-open | Screenshot submenu: **Whole Screen** enabled, **Window / Selected Area disabled** (G3) | Linux-correct submenu |
| 04 whole-screen-captures-doc | the captured screen open as a **`trailer-screenshot-…` document, with NO capture-mode dialog** in between | direct dialogless capture (contrast path 02's Tools dialog) |

**Selector (fragile, documented):** the submenu items have no
shortcut/objectName/mnemonic, so path 09 drives them by **focus-stable arrow-key
nav** (Down ×2 → Right → Return) sent with raw `xdotool key` after a single
`activate` — type-ahead did not register in this session, and `press()` would
dismiss the menu. **Graceful degrade:** if the capture document doesn't appear
(a File-menu IA reorder shifts the Down count), the step records a **boundary**
and captures the reachable menu state instead of hard-failing; the grab itself is
proven under Xvfb by path 02 (shared `Application::captureScreenshot`).

## What this tier cannot verify (owner real-Mac checklist)

Per the decision record and the skill: HiDPI/dpr=2 sharpness (#1), the native
TCC prompt (#6 native half), OS-level global-shortcut clashes (#7), and
Dock/Services/native-menu-bar chrome stay on the owner's real-Mac milestone
checklist in [`.claude/skills/ux-walkthrough/SKILL.md`](../../.claude/skills/ux-walkthrough/SKILL.md).
Offscreen Xvfb is dpr=1.

## Curated evidence

The pivotal per-path outcome shots from a real run are committed under
`docs/uat/images/ux-walkthrough-*.png` (G2 evidence). Working/throwaway
captures stay in the gitignored `uat-screenshots/` tree.
