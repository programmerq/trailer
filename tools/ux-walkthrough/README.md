# ux-walkthrough drive harness (Tier 1 — Linux/Xvfb)

This is the **DRIVE + CAPTURE** half of the `ux-walkthrough` review gate. It
drives the **real built `trailer` binary** through the four ux-walkthrough
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
- `libxcb-cursor0` — Qt 6's `xcb` platform plugin needs it at runtime

These are baked into the CI runner image (`docker/runner/Dockerfile`).

Build the binary first:

```sh
cmake -S . -B build -G Ninja && cmake --build build -j
```

## Running it

```sh
# all four golden paths
tools/ux-walkthrough/run.sh all

# a subset
tools/ux-walkthrough/run.sh 01 04

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

## The four golden paths and what "pass" looks like

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
