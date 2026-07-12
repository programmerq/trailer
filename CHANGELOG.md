# Changelog

All notable user-facing changes to Trailer are recorded here. The
format follows [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/);
Trailer's `VERSION` follows [Semantic Versioning](https://semver.org/),
with the 0.x caveats spelled out in [PHILOSOPHY.md](PHILOSOPHY.md) — until
1.0, minor bumps may carry breaking changes to on-disk formats, settings,
and the `IDocument` interface.

Each release section is sourced into the GitHub Release body at tag time
by [`scripts/extract-changelog.sh`](scripts/extract-changelog.sh). Keep
entries terse and user-visible; CI / infrastructure churn lives in the
*Infrastructure* subsection so users browsing release notes can skip it.

## [Unreleased]

### Added

- **PDF page-op undo.** `Delete Pages`, `Move Page`, `Insert
  Pages from File…`, and `Crop Pages…` are now undoable via the
  existing `PdfCommand` stack. A single user gesture that affects
  N pages (e.g. cropping a multi-selection) produces ONE command,
  so one Ctrl-Z reverts the whole batch atomically.
- **Page-mode keyboard shortcuts.** `Cmd-1` / `Cmd-2` (Ctrl on
  Windows/Linux) switch to Continuous / Single Page view. `Cmd-3`
  is reserved for Two Pages once a facing-page layout exists.
- **Segmentation model preload.** Picking the Instant Alpha or
  Smart Lasso tool now warms the MobileSAM encoder in the
  background so the first stroke is responsive. Controlled by the
  existing "preload on tool activation" setting; never triggers a
  model download.

### Changed

- **Zoom shortcuts moved off the digit row** to make space for the
  page-mode shortcuts: Actual Size is now `Cmd-0` (was `Cmd-1`), Fit
  Page is `Cmd-9` (was `Cmd-0`), and Fit to Width keeps its menu item
  but no longer has a digit shortcut. Zoom In / Out remain `Cmd-+` /
  `Cmd--`.
- **Linux power awareness.** Battery vs. AC state is now read from
  `/sys/class/power_supply`, so speculative ML work (OCR / SAM /
  background-removal prefetch) pauses on battery the same way it
  already does on macOS and Windows.

### Fixed

- **Undo order across subsystems.** Undo / Redo now pop a single
  chronological log per document, so interleaved gestures (rotate →
  annotate → rotate) revert in true reverse order. Previously PDF
  documents routed Ctrl-Z by a most-recently-touched-stack heuristic
  and image documents drained all annotation undo before pixel
  edits.
- **Undo past the annotation history cap.** The bounded annotation
  history now evicts in lockstep with the chronological log, so a
  long markup session no longer produces "Undo does nothing"
  presses, annotations stranded after undo-all, or Redo entries
  that no-op. The annotation history cap was also raised from 64 to
  128 gestures, and Undo / Redo degrade to a logged warning (never
  a crash) if the history ever desynchronises.
- **Search bar "Close".** Clicking the X (or pressing Esc) now
  fully collapses the search bar instead of leaving an empty gap in
  the toolbar.

### Infrastructure

- **Release tooling.** New `scripts/bump-version.sh` (VERSION-file
  lifecycle), `scripts/release-notes.sh` (git-log → CHANGELOG
  draft), `scripts/extract-changelog.sh` (CHANGELOG section →
  GitHub Release body). `release-publish.yml` now splices the
  CHANGELOG section for the tagged version into the release body.
  `RELEASING.md` documents the cut-a-release runbook.
- **README** refreshed: dropped the stale "Phase 0" framing,
  reframed macOS unsigned-ness as project policy rather than a
  follow-up, marked Intel-Mac support as in-scope future work.
- **CRITERIA.md keystone added.** New third keystone doc alongside
  `DESIGN.md` and `PHILOSOPHY.md`: defines what "done," "priority,"
  and "1.0" mean — the ranking function (which job it degrades × how
  badly × how often that job is done) that orders `TODO.md`'s queue.
- **TODO.md restructured into a ranked queue + ambiguity ledger.**
  The dated-HITL-pass prose is replaced by a single ranked queue read
  off CRITERIA.md's ranking function, a Someday pool for findings
  that degrade no listed job, and an Owner questions ledger for the
  judgment calls the ranking function can't resolve on its own.
- **Recording-findings protocol added.** New
  `docs/recording-findings.md` documents how the owner's webcam+screen
  self-recorder's footage becomes structured findings (timestamp, repro,
  job, degree, platform) that feed the same `TODO.md` queue as HITL
  passes and smoke sessions.
- **PHILOSOPHY.md / DESIGN.md / ROADMAP.md acceptance-gate
  recentering.** "Done" for a listed job is now certified by the
  owner's own sustained use rather than the reference-user lens;
  PHILOSOPHY.md and DESIGN.md point to CRITERIA.md §2 as the
  canonical statement, and ROADMAP.md's *Now* section is read off
  the ranking function instead of a hand-picked list.

## [0.1.0] - 2026-05-16

First publicly tagged build. Trailer at this point covers the
foundations, viewer, PDF page operations, image editing, markup /
annotations, forms / signatures / password / redaction, and an initial
on-device ML stack (background removal, Smart Lasso / Instant Alpha,
OCR).

### Added

- **Distribution.** Tagged release pipeline with installers for macOS
  (`.dmg`, Apple Silicon arm64), Linux (`.tar.gz`, `.deb`, `.rpm`), and
  Windows (`.zip`, `.msi` via WiX). macOS builds adhoc-sign the bundle
  so Qt 6.8 produces a structurally valid `.app`; Gatekeeper still
  warns on first launch (project does not enroll in the Apple Developer
  Program — see release body for the `xattr` bypass).
- **Viewer / shell.** Sidebar with explicit modes (Hidden /
  Thumbnails / Search Results / Table of Contents / Highlights &
  Notes), default-hidden. Main toolbar with sidebar-mode picker, zoom
  (in / out / actual), rotate (L / R), markup toggle, form toggle,
  and an always-visible search field. Window menu, Go menu, and
  macOS-specific no-window file menu actions when launched with no
  arguments.
- **Search.** Highlighter-yellow overlay on `QPdfView` for matches,
  with a current-match highlight and "X of Y" counter in the search
  field. Sidebar auto-switches to Search Results mode on non-empty
  query.
- **Annotations.** Inline text editor for Text / SpeechBubble
  annotations (no modal dialog). Pressure-aware Ink for tablet
  styluses and Force Touch trackpads. Re-selectable, movable, and
  resizable annotations with arrow-key nudge and Delete/Backspace.
  Inspector pane tracks the selection. Contextual tool gating hides
  text-aware tools (Highlight / Underline / StrikeOut) on PDFs
  without a text layer. /AP appearance streams emitted for
  Rectangle, HighlightShape, Ellipse, Line, Arrow, and Ink so other
  viewers render them correctly.
- **Forms.** Form widgets auto-shown on any fillable PDF (one-shot
  per-doc, an explicit user-hide is sticky). Subtle border on
  fillable regions; Tab navigates between fields in document
  reading order.
- **Signatures.** Popover-based placement (no modal dialog). Pressure
  curve and per-stroke smoothing on the capture canvas. New
  signatures auto-arm on save.
- **PDF page operations.** Save off the UI thread via
  `QFutureWatcher` + `QProgressDialog`. Undo/redo command pattern
  (`PdfCommand`) with `RotatePageCommand` as the first instance.
- **ML.** Model manager dialog with explicit consent prompts, policy
  gates that only block when a download is actually pending, and
  per-feature greying when a required model isn't available.
- **Icons.** 29 base + 6 filled-variant SVG markup icons under
  `resources/icons/actions/`, themed via `IconHelper` for light /
  dark adaptation. App icon redesigned with bell-curve sprockets and
  a dark-mode variant; canonical icon source feeds the macOS
  `.icns`, Linux RPM / DEB desktop entries, and the Windows MSI.

### Changed

- **Window-per-file is the default.** `openFiles({a, b, c})` spawns
  three windows. Tabs remain available via `open_files_in =
  "new_tab"`. Single-doc windows hide the tab strip.
- **Default tool is Select**, not box-drawing. Default annotation
  stroke is dark grey, not red.
- **AutoFill from My Card** demoted to `Tools → Forms →` submenu.
  Direct field manipulation is the primary path; per-PDF AutoFill
  tuning is explicitly out of scope.

### Fixed

- Cmd-Tab while dragging an annotation no longer leaves an
  undo-less ghost annotation behind — in-flight drags abort on
  `applicationStateChanged` deactivate.
- macOS Dock drag-onto-icon opens the file in-process; pinned by
  `uat_fnd_050_fileOpenEventOpensWindow`.
- PDF thumbnails composited over an opaque white canvas in dark mode
  (no more grey paper).
- Magnifier deactivates on Esc and on app deactivate.
- Sidebar thumbnails render at native pixels on Retina (no more soft
  thumbnails).
- `selectAll()` always emits `selectionChanged` regardless of prior
  selection state.
- Build under Qt 6.11 + AppleClang 17: address-of-temporary and
  sign-conversion warnings cleared.

### Infrastructure

- CI: warnings-as-errors is **off** by default in both PR CI and the
  release pipeline (Qt / libstdc++ / qpdf system headers fire too
  many false positives to make `-Werror` workable). Source still
  kept clean by review; flip `-DTRAILER_WERROR=ON` locally to chase
  a regression.
- CI: mold linker + Ninja generator land the cold build comfortably
  under the timeout.
- Release pipeline: `release.yml` gated on a `release-candidate` PR
  label; `release-autotag.yml` tags merged RCs at PR HEAD;
  `release-publish.yml` promotes the prior build's artifacts to a
  GitHub Release without rebuilding.
- UAT harness: ~70 offscreen slots wired via `tests/uat/`, runnable
  via `scripts/run-uat.sh` (host or Docker).

[Unreleased]: https://github.com/programmerq/trailer/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/programmerq/trailer/releases/tag/v0.1.0
