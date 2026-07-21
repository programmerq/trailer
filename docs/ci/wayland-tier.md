# Wayland smoke CI tier

The `wayland-smoke` job in [`.github/workflows/ci.yml`](../../.github/workflows/ci.yml)
launches the real built `trailer` binary on a headless Wayland compositor and
screenshots it. It is the one CI check that proves Trailer runs natively on
Wayland — something the offscreen unit tier and the Wine tier structurally
cannot prove. It is deterministic (hard oracle) and **blocks merge**.

Driver: [`scripts/wayland-smoke.sh`](../../scripts/wayland-smoke.sh).

## What the tier does

1. Creates a **short** `XDG_RUNTIME_DIR` (a fresh `mktemp` dir under `/tmp`).
   sway's IPC socket path must fit `sockaddr_un.sun_path` (~108 chars); a long
   scratchpad path overflows it and sway segfaults at startup.
2. Starts **sway** headless — `WLR_BACKENDS=headless`, `WLR_RENDERER=pixman`
   (no GPU in the k8s pods; the GLES renderer segfaults), a 3-line config
   pinning a `1280x800` `HEADLESS-1` output. Polls for the wayland socket; no
   blind sleep.
3. Launches `build/trailer` on a sample image (`docs/perf/corpus/photo.jpg`)
   under `QT_QPA_PLATFORM=wayland` with `QT_LOGGING_RULES='qt.qpa.*=true'`.
4. **Asserts `platformName == wayland`** by grepping the qt.qpa log for the
   `Successfully loaded Qt platform plugin "wayland"` line, and hard-fails on
   an `xcb`/`offscreen`/`minimal` fallback line. Strict.
5. Captures the compositor output with **grim** to a PNG (uploaded as a CI
   artifact).
6. **Asserts the PNG is `1280x800` and non-blank** — not a single flat colour.
   A bare sway output is a flat colour, so a capture stays blank unless Trailer
   actually paints; painted UI has thousands of unique colours. Uses ImageMagick
   `identify` when present, and falls back to a pure-stdlib Python PNG analyzer
   (zlib IDAT + scanline unfilter + unique-colour count) so the script is
   self-contained.

A `trap` kills trailer + sway and removes the runtime dir on exit; each run is
idempotent and re-runnable.

## Design decisions

### Why sway, not weston

Both compositors run headless and Qt connects to either. But **weston's
headless backend cannot be screenshotted**: `grim` needs wlroots'
`wlr-screencopy` protocol (weston doesn't expose it), and
`weston-screenshooter` aborts with `Assertion 'width > 0' failed` against the
headless output. sway is wlroots-based, so `grim` works. That is the deciding
factor — the screenshot is the whole deliverable.

### Why launch + screenshot only (no input automation)

Real pointer-driven UI automation is not cleanly available in this CI:
`/dev/uinput` doesn't exist in the k8s pods, so `ydotool` is out; `wtype`
speaks the virtual-keyboard protocol (exits 0) but keystroke *delivery* into a
focused widget was never confirmed. Launch + screenshot + assert is an honest,
useful, deterministic tier on its own; driven input is deferred.

### Why the unit suite and UAT stay on offscreen

They are **deliberately not** run under Wayland here, for reasons that would
otherwise force dishonest bookkeeping (per-test timeouts + label-excludes to
paper over a known hang):

- **Unit suite under Wayland is 57/60, not 60/60.**
  - `test_dirty_marker_zoom` **hangs**: `MainWindow::closeEvent`
    (`src/ui/MainWindow.cpp:4432`) auto-accepts the unsaved-changes modal
    **only** under `offscreen`/`minimal`; under `wayland` it shows the real
    Save/Discard/Cancel dialog and blocks forever with no user. This is the
    systemic blocker.
  - `test_image_scale` fails on fractional-scaling rounding (real-compositor
    DPR vs offscreen `dpr=1`).
  - `test_perf_freehand_repaint` is a wall-time perf budget — noise under a
    compositor (the same `perf` tier the Wine job already drops).
- **UAT can't be pointed at Wayland yet.** `tests/uat/CMakeLists.txt:15` sets
  `QT_QPA_PLATFORM=offscreen` as an *unconditional* per-test `ENVIRONMENT`
  property that overrides an ambient `wayland`, and the same modal hang hits 5
  UAT binaries.

So the unit suite stays on offscreen in `build-and-test` (60/60, ~10s), exactly
mirroring the Wine tier's honest `--label-exclude 'uat|perf'` exclusion, stated
inline rather than hidden.

## Runner image / ride-along

[`docker/runner/Dockerfile`](../../docker/runner/Dockerfile) bakes `sway` +
`grim` alongside the existing X11 tooling. Until that image is rebuilt and the
ARC runner-set repointed at the new tag, the CI job **apt-installs them
per-run** (mirroring the Wine tier's per-run install), so the tier is green
without waiting for an image rebuild — the same ride-along pattern PR #87 used.
The Qt wayland platform plugin needs no package: the aqt Qt 6.11 `linux_gcc_64`
bundle already ships `libqwayland.so` + the xdg-shell integration.

## Proof

`docs/ci/images/wayland-smoke-proof.png` is a genuine capture of Trailer
running on native Wayland (sway + grim) with an image document open — full menu
bar, markup toolbar, and status bar. Not a blank buffer.

## Phase 3 (deferred)

- Fix the offscreen-only modal auto-accept
  (`src/ui/MainWindow.cpp:4432`) so `close()` on a dirty document doesn't hang
  under Wayland, and make `tests/uat/CMakeLists.txt:15`'s forced offscreen a
  default a `-DTRAILER_UAT_PLATFORM=wayland` opt-in can override — the two
  prerequisites for a UAT-on-Wayland tier.
- Stand up the XDG screenshot portal path (dbus + pipewire +
  `xdg-desktop-portal-wlr` + a `portals.conf` mapping) to close the deferred
  backlog item
  [`docs/backlog/2026-07-12-wayland-screenshot-portal.md`](../backlog/2026-07-12-wayland-screenshot-portal.md)
  (screenshot picker must go through the portal or be disabled-with-tooltip,
  never a silent null).
