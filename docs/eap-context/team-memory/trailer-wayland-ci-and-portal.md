---
name: trailer-wayland-ci-and-portal
description: Verified recipe (2026-07-21) for running Trailer natively on headless Wayland in-container + the XDG-portal screenshot stack; PRs #117 (wayland-smoke CI tier) + #118 (portal capture). Qt 6.11 aqtinstall bundles the wayland plugin; sway+grim works, weston does not; input injection infeasible (no /dev/uinput).
metadata:
  type: reference
  modified: 2026-07-21T15:03:48.608Z
---

# Trailer on headless Wayland — CI smoke tier (#117) + XDG-portal capture (#118)

## Native Wayland runs in the remote container
- Qt 6.11.0 from aqtinstall (`/opt/qt/6.11.0/gcc_64`) **already bundles the
  wayland platform plugin** (`libqwayland.so` + integration dirs) — no
  `qtwayland` / `qt6-wayland` install needed.
- Proof: under a compositor `QGuiApplication::platformName() == "wayland"`;
  qt.qpa log emits `Successfully loaded Qt platform plugin "wayland"` over
  xdg-shell. Qt talks Wayland natively — **no XWayland**.

## Compositor: sway (weston is a dead end)
- `sway` 1.9 headless (`apt install sway`) with `WLR_BACKENDS=headless`,
  `WLR_RENDERER=pixman`, a short `mktemp` `XDG_RUNTIME_DIR` (`chmod 700`;
  the sway IPC socket has a path-length limit so keep it short), output
  `HEADLESS-1` bg `#000000` `solid_color`.
- Screenshot via `grim` (`apt install grim`).
- **WESTON IS A DEAD END for screenshots**: grim needs `wlr-screencopy`
  which weston lacks; `weston-screenshooter` aborts on a headless output.
- `swaymsg` needs `SWAYSOCK` **exported** (sway only sets it in its own
  env) — resolve `sway-ipc.*.sock` and export it, else `get_tree` silently
  no-ops.

## Smoke script + CI job (#117)
- Repro smoke script committed at `scripts/wayland-smoke.sh`: sway headless
  + launch trailer under `QT_QPA_PLATFORM=wayland` + grim capture +
  non-blank assert.
- CI job **`wayland-smoke`** added to `.github/workflows/ci.yml` on
  trailer-k8s; apt-installs sway+grim per run with retry (ride-along until
  `docker/runner/Dockerfile` bake lands). Recommended to stay a
  **NON-REQUIRED (advisory) status check** until baked.
- Unit suite stays on **offscreen**: under real Wayland 3/60 fail — a
  dirty-close modal that only auto-accepts on offscreen/minimal at
  `MainWindow.cpp` ~4433 → hang; a fractional-scaling rounding diff; a
  perf-budget test. UAT hard-forces offscreen at
  `tests/uat/CMakeLists.txt:15`.

## Input injection is INFEASIBLE in the k8s pod
- `/dev/uinput` absent (rules out `ydotool`); `wtype` keystroke delivery
  unconfirmed. Wayland CI is launch + screenshot + smoke only, no input
  automation.

## XDG portal screenshot works headless (#118)
- Stack = sway headless + `dbus-daemon --session` + pipewire +
  wireplumber + `xdg-desktop-portal` + `xdg-desktop-portal-wlr`, with a
  `portals.conf` mapping `Screenshot` + `ScreenCast` to `wlr`.
- **GOTCHA**: `xdg-desktop-portal` 1.18.4 only registers the frontend
  Screenshot portal inside `if (access_impl != NULL)`, and
  `xdg-desktop-portal-wlr` does NOT provide
  `org.freedesktop.impl.portal.Access` — so add a **STUB access portal**
  (a `.portal` declaring the Access interface) + map
  `org.freedesktop.impl.portal.Access=stubaccess` in `portals.conf`; the
  stub need not actually run for a non-interactive grab. Then
  `org.freedesktop.portal.Screenshot.Screenshot(interactive:false)` returns
  a real non-blank PNG.
- The wlr backend needs **pipewire running or it crashes**.
- Repro script was `scratchpad/portal-stack.sh`.

## App code
- `src/platform/PortalScreenshot.cpp` (QtDBus, Linux-only; `Qt6::DBus`
  linked only `UNIX AND NOT APPLE`, stub elsewhere).
- `chooseLinuxScreenshotBackend` in `ScreenCaptureBackend.cpp` selects
  portal / direct-grab / disabled. On Wayland-without-portal the
  "Acquire ▸ Whole Screen" item is **disabled + tooltip (G3)**, replacing
  the prior silent null-pixmap no-op.
- ADR `docs/decision-records/2026-07-21-wayland-screenshot-portal.md`.
- Portal path has **NO automated CI coverage yet** (live test QSKIPs
  offscreen; #117 smoke tier doesn't stand up the portal stack) — a
  full-portal-stack CI job is a known follow-up.

## Related
[[trailer-remote-build-recipe]], [[trailer-ci-on-k8s-runners]],
[[trailer-review-before-push-policy]]
