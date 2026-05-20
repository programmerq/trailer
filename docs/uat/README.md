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
| [07-security.md](07-security.md) | Password-protected PDFs, permissions, redaction (Phase 5) |

## Test case format

Each case has a stable ID of the form `UAT-<area>-<NNN>` (area matches the
spec file prefix — `FND` foundations, `VWR` viewer, `PDF` pdf pages, `IMG`
images, `ANN` annotations, `XCT` cross-cutting, `SEC` security). IDs are
append-only; when a case is removed, mark it obsolete rather than reusing
the number.

> **Spec ID vs. test slot name — not always 1:1 today.** The four
> spec-area codes that *do* have matching test slot prefixes are
> `FND`, `VWR`, `ANN`, `SEC` (so `UAT-FND-001` → `uat_fnd_001_*`).
> The other three (`PDF`, `IMG`, `XCT`) are spec-only — their cases
> live in those `.md` files but the test harness organises the
> corresponding slots under topical-feature prefixes instead
> (`uat_af_*` autofill, `uat_bgr_*` background removal, `uat_frm_*`
> forms, `uat_hn_*` highlights & notes, `uat_ocr_*` recognize text,
> `uat_red_*` redaction, `uat_sam_*` Smart Lasso + Instant Alpha,
> `uat_sig_*` signatures, `uat_toc_*` table of contents). This
> two-naming-axis shape works but isn't enforced; tracked as a
> backfill item in `TODO.md ## 2026-05-19 HITL pass` (audit ref
> DOC-FOLLOWUP-1). When adding a new spec case, name the test slot
> after either the spec ID *or* its topical prefix; just don't
> invent a third.

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

## Automated harness (MVP)

A thin QTest-driven harness lives under [`tests/uat/`](../../tests/uat/).
It constructs the `Application` + `MainWindow` in-process under
`QT_QPA_PLATFORM=offscreen`, so no display server, Xvfb, or GPU is
needed. Each slot is named after a spec ID (`uat_fnd_001_*`) so
failures point directly at a case above.

The suite is labelled `uat` in CTest. The normal commit/PR CI runs
`ctest --label-exclude uat`, so UAT stays out of the push loop.

Run locally against your existing dev build:

```sh
scripts/run-uat.sh --host
```

Run inside the pinned Docker image (recommended — matches what the
eventual tag-triggered CI will use):

```sh
scripts/run-uat.sh              # builds docker/uat/Dockerfile if needed
scripts/run-uat.sh --rebuild    # force a clean image rebuild
```

Manual override — reuse your IDE-configured build dir:

```sh
cd build
QT_QPA_PLATFORM=offscreen ctest -L uat --output-on-failure
```

### When UAT runs

- **Commit / PR CI**: excluded (see `.github/workflows/ci.yml`).
- **Tag push**: an opt-in workflow scaffold exists at
  `.github/workflows/uat.yml.disabled` — rename to `.yml` once we
  trust the suite.
- **Developer machine**: anytime, via the script above.

### Adding cases

1. Pick an unclaimed case ID from a phase doc (e.g. UAT-VWR-010).
2. Add a slot named `uat_vwr_010_shortTitle()` in the matching
   `tests/uat/test_uat_<area>.cpp`.
3. The slot's body implements the Steps and asserts the Expected.
   Prefer driving via `QAction::trigger()` and direct widget APIs;
   fall back to `QTest::keyClick` / `QTest::mouseClick` only for
   genuinely input-driven behaviours.

### Future considerations (not yet done)

- **Screenshots / video**: the offscreen plugin supports
  `QScreen::grabWindow` with some caveats. When we want real visual
  diffs we can swap `QT_QPA_PLATFORM=offscreen` for `xvfb-run` in the
  Docker image — that's a drop-in change; no test-code edits needed.
- **File dialogs**: when a case needs to exercise `QFileDialog`, the
  suite will need a debug build flag that forces
  `QFileDialog::DontUseNativeDialog` so QTest can drive the child
  widgets. Not needed by any current flow.
- **Windows / macOS hosts**: the Docker image is Linux-only. Running
  platform-tagged cases on the other two hosts is a separate future
  pass.

Until automated coverage expands, keep the manual cases authoritative
— changes to behaviour must update this doc first, then the harness.

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
