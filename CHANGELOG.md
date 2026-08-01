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

### Changed

### Fixed

- **View-mode switching now preserves the current page.** Switching between
  Single Page, Continuous, and Two Pages (Cmd-1/2/3) used to silently reset
  the visible page to the start of the document while the sidebar kept
  reporting the page you were actually on. The view now follows the model.
- **Two-Pages mode re-fits a Fit Page zoom to the spread.** Entering
  Two-Pages mode while Fit Page was active used to carry the single-page
  zoom unchanged, spilling a page off-screen and populating a scrollbar.
  It now recomputes a spread-aware fit, matching Fit Page's own meaning
  for a facing spread.
- **Search now scrolls to the match it selects.** The position-aware seed
  and Find Next / Find Previous already picked the right match; the
  viewport now actually scrolls there instead of leaving it selected but
  off-screen.
- **Shift+Enter in the search field now means Find Previous**, matching
  every other find-bar's convention. Previously it behaved like a plain
  Enter (Find Next).
- **View menu's Continuous / Single Page / Two Pages items keep a fixed
  order** matching their Cmd-1/2/3 shortcuts, regardless of which mode is
  active.

### Infrastructure

## [0.3.0] - 2026-07-12

Trailer's second minor release since the first public tag. Headline
work: a never-worry save invariant on dirty close, a rebuilt
chronological undo engine, a first-run / empty-state window model, a
unified Preferences dialog, live progress + cancel for on-device ML,
and a batch of honest-affordance and accessibility fixes — backed by a
new design-criteria gate system, decision records, and a self-hosted
CI pipeline that cross-builds and tests all three platforms. Version
0.2.0 was tagged but never published with release notes, so its
changes are consolidated into this section.

### Added

- **Preferences dialog.** A unified Preferences dialog (Edit →
  Preferences…, `Cmd-,`) collects previously scattered settings. A
  live-vs-restart volatility registry flags any setting that only
  takes effect after a restart, so changing one no longer silently
  no-ops until relaunch.
- **First-run / empty-state window model.** A window with no open
  document now presents a purposeful empty state, and document-only
  toolbars stay hidden until a document is loaded rather than showing
  a row of dead controls.
- **ML operation feedback.** Long-running on-device ML work (OCR and
  the segmentation / background-removal stack) now reports progress in
  the status bar with a scoped Cancel, and surfaces an in-context
  prompt when the operation needs a model that isn't installed yet
  instead of failing silently.
- **Copy Page as Image.** New `Edit → Copy Page as Image` renders the
  current page to the clipboard as a raster image.
- **Continuous-mode screenful navigation.** In Continuous view,
  `Down` / `Up` / `Space` now step by a full viewport (a screenful)
  instead of a single line.
- **Content-aware first-open sidebar defaults.** The sidebar's
  first-open mode is now chosen from the document's own content (e.g.
  whether it carries a table of contents) via tuned thresholds.
- **Accessibility naming.** Icon-only buttons (starting with the
  Search button) now carry accessible names, with a guard test against
  shipping unnamed icon buttons.
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
- **Honest disabled controls.** Controls that can't act in the current
  context are now disabled with an explanatory tooltip (the
  disable-plus-tooltip affordance policy, gate G3) instead of
  appearing live and doing nothing — including `Copy Page as Image`
  and the empty-state toolbar toggles. Settings that need a restart
  now say so up front rather than surprising the user later.

### Fixed

- **Never-worry save.** Closing a document or tab with unsaved changes
  now always prompts Save / Discard / Cancel; the previous path could
  silently drop edits when a dirty tab was closed. Codified as the
  never-worry-save invariant (decision record 0004).
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
- **Inspector tab scroll arrows.** The Inspector tab bar's overflow
  scroll arrows now meet the minimum touch-target size under large
  application fonts.

### Infrastructure

- **Developer UX session recorder** (maintainer tooling, not a user
  feature). New compile-time option `TRAILER_ENABLE_UX_RECORDER`
  (default **OFF** — release artifacts are unaffected). Recorder-enabled
  builds record every launch to a strictly local directory under the
  app-data folder (so the build can be set as the default file handler
  and capture Finder-launched sessions; `--no-ux-record` opts a single
  launch out): structured JSONL events (input, focus, dialogs, document
  state, manual frustration markers), ~3 fps screen frames and a
  face-cam movie on macOS (ScreenCaptureKit / AVFoundation), and an
  instrumented "Hand Off to Preview" action that follows the fallback
  workflow across the app switch. macOS surfaces the first-run "approve
  Screen Recording, then relaunch" step on the recording indicator. No
  network code anywhere in the feature; nothing is ever transmitted. See
  `docs/ux-recorder.md`.
- **Design-criteria hard gates + decision records.** `AGENTS.md`
  gains nine hard gates (G1–G9) with a companion `DESIGN.md` and
  `docs/performance-budgets.md`; recurring design calls are now
  captured as numbered decision records under
  `docs/decision-records/` (ADR 0002 ML progress/cancel, ADR 0003
  content-aware thresholds, and ADR 0004 never-worry-save all
  accepted this cycle).
- **Review-before-push + decision-brief skills.** New agent skills
  wire a mandatory pre-push review pass and a decision-brief
  triage step into the hard-gate workflow.
- **Structural performance tests + corpus.** Added structural perf
  assertions (render-before-read ordering, GUI-thread I/O guards,
  paint-budget checks) over a dedicated perf corpus, labelled `perf`
  so slower CI tiers can exclude them.
- **Self-hosted CI on trailer-k8s.** Linux build/test jobs moved to
  self-hosted `trailer-k8s` runners, with cmake / ninja / mold
  provisioned by the setup action and cold-cache-friendly timeouts.
- **Dockerless Windows cross-build + Wine tests.** The Windows
  release artifact is now cross-compiled with mingw-w64 directly on
  the self-hosted runners (no Docker daemon) and smoke-tested under
  Wine, with `onnxruntime.dll` bundled and its hash pinned.
- **Custom runner image.** A purpose-built runner image bakes in
  `ccache`, `gh`, `jq`, `zip`, and `shellcheck` (plus a PR build
  validation step), removing per-run apt installs.
- **Action runtime bumps.** node20-runtime actions bumped to
  node24-compatible releases and `actions/checkout` bumped 6 → 7.
- **Session-start setup hook.** A session-start setup script + hook
  bootstraps the build environment; the Qt minimum is raised to 6.6.
- **Release tooling.** New `scripts/bump-version.sh` (VERSION-file
  lifecycle), `scripts/release-notes.sh` (git-log → CHANGELOG
  draft), `scripts/extract-changelog.sh` (CHANGELOG section →
  GitHub Release body). `release-publish.yml` now splices the
  CHANGELOG section for the tagged version into the release body.
  `RELEASING.md` documents the cut-a-release runbook.
- **README** refreshed: dropped the stale "Phase 0" framing,
  reframed macOS unsigned-ness as project policy rather than a
  follow-up, marked Intel-Mac support as in-scope future work.

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

[Unreleased]: https://github.com/programmerq/trailer/compare/v0.3.0...HEAD
[0.3.0]: https://github.com/programmerq/trailer/compare/v0.1.0...v0.3.0
[0.1.0]: https://github.com/programmerq/trailer/releases/tag/v0.1.0
