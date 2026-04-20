# Trailer — User Acceptance Tests

These documents describe end-to-end behaviours a human (or, later, an
automated Qt test harness) should exercise against a built Trailer binary.
They complement the unit tests in `tests/` — unit tests verify that a
function does what its contract says; UAT verifies that the product as a
user touches it behaves as expected.

## Scope

Each file covers a coherent slice of the product:

| File | Covers |
|---|---|
| [01-foundations.md](01-foundations.md) | Launch, window shell, settings persistence, recent files, CLI, drag-and-drop |
| [02-viewer.md](02-viewer.md) | Opening files, PDF and image viewing, zoom, tabs, search, print, magnifier, thumbnails |
| [03-pdf-pages.md](03-pdf-pages.md) | PDF page operations: rotate, delete, move, crop, extract, insert, save |
| [04-image-editing.md](04-image-editing.md) | Image edits: rotate, flip, resize, crop, colour adjust, export, animation playback |
| [05-annotations.md](05-annotations.md) | Markup toolbar, all annotation types, Inspector, inline editing, text markup, undo/redo, PDF round-trip |
| [06-cross-cutting.md](06-cross-cutting.md) | Theme, screenshot tool, keyboard shortcut matrix, multi-window, process lifecycle |

## Test case format

Each case has a stable ID of the form `UAT-<area>-<NNN>` (area matches the
file prefix — `FND` foundations, `VWR` viewer, `PDF` pdf pages, `IMG`
images, `ANN` annotations). IDs are append-only; when a case is removed,
mark it obsolete rather than reusing the number.

```
### UAT-XXX-NNN — Short title

**Preconditions:** State the app/doc/file state required before the test.
**Steps:**
1. …
2. …
**Expected:**
- …
- …
```

Guidelines:

- One case per observable behaviour. If a step has two expected outcomes
  (e.g. UI changes *and* file-on-disk changes), list both.
- Preconditions list fixtures, settings, and document state. Assume a
  fresh app launch unless stated otherwise.
- Steps are imperative and use menu paths exactly as they appear
  (`File > Open…`, not `open a file`).
- Expected outcomes describe what the user sees, not the implementation.
  Avoid "the `m_dirty` flag is set" — write "the tab title shows a `•`
  prefix".
- Platform-specific differences (e.g. `Cmd` vs `Ctrl`) are noted inline
  where they matter. Screenshot and menu-role behaviours differ on macOS
  and are flagged in the individual case.

## Running these manually

1. Build a release binary: `cmake --build build --config Release -j`.
2. Launch with `./build/trailer` (or the platform-specific path).
3. Work through a file top to bottom, marking each case pass / fail /
   blocked. A checklist-style spreadsheet is fine; we don't have a
   formal tracking system yet.
4. File defects against the specific UAT ID so we can attribute them.

## Fixtures

Cases that need specific files reference them by description
(e.g. "a 4-page PDF", "a 64×64 PNG"). Any file meeting the description
works. Once we move to automation we'll generate fixtures inline (as
existing tests already do with `QPdfWriter` and `QImage::save`).

## Automation plan (not yet implemented)

- Run the binary under an offscreen Qt platform
  (`QT_QPA_PLATFORM=offscreen` on Linux/macOS, or `minimal` in CI).
- Drive the UI with `QTest::keyClick`, `QTest::mouseClick`, and direct
  action triggers (`action->trigger()`) for menu-driven flows.
- File dialogs need `QFileDialog::DontUseNativeDialog` in a test build
  so QTest can interact with them.
- Each case becomes a `QTest` slot; the `// Steps:` comments mirror the
  manual case so the two stay in sync.

Until the harness lands, keep the manual cases authoritative — changes
to behaviour must update this doc first.

## Status legend

Cases may be tagged in their title with one of:

- **(Phase N)** — introduced or expected from that phase; older phases
  may still evolve.
- **(Known gap)** — the behaviour is known to be missing or partial;
  included so the gap is testable and we notice regressions when it
  lands. Cross-referenced with [TODO.md](../../TODO.md).
- **(Platform: macOS)** / **(Platform: Linux)** / **(Platform:
  Windows)** — runs only on that OS.

Cases without a tag are expected to pass on all three platforms as of
the current phase.
