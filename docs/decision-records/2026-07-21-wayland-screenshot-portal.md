# Wayland whole-screen capture routes through the XDG screenshot portal

- **Status:** accepted
- **Slug:** `wayland-screenshot-portal`
- **Arbiter:** the agent role named for this record; the owner (programmerq) is the escalation-only override.
- **Date proposed:** 2026-07-21
- **Date accepted / superseded:** 2026-07-21 (accepted)
- **References:** backlog item `2026-07-12-wayland-screenshot-portal` (the work
  item this record closes; deleted in the implementing change per the backlog
  close convention). Sibling: `docs/ci/wayland-tier.md` (the Phase-2 sway+grim
  smoke tier) and the macOS capture records
  [`2026-07-16-permissionless-screen-capture`](2026-07-16-permissionless-screen-capture.md)
  / [`2026-07-16-capture-permission-preflight`](2026-07-16-capture-permission-preflight.md).

## Context

Whole-screen capture (File ▸ Screenshot ▸ Whole Screen) on non-macOS used
`QScreen::grabWindow(0)`. Under **Wayland** a client may not grab the screen
directly, so `grabWindow` returns a **null pixmap**; the old code then
`return`ed silently — a user clicking "Whole Screen" got **nothing, with no
explanation**. That is exactly the "silent null" gate **G3 (no lying controls)**
forbids, and the open backlog item `2026-07-12-wayland-screenshot-portal`
tracked it.

The compositor-mediated way to capture under Wayland is the
`org.freedesktop.portal.Screenshot` desktop portal (backed by
`xdg-desktop-portal-wlr` / `-gnome` / `-kde`). It is **local D-Bus IPC** to the
session portal service — not an outbound network call — so it stays inside
Trailer's no-telemetry / no-network constraint (PHILOSOPHY → *No telemetry*;
the ban targets `QNetworkAccessManager`-class outbound calls, which QtDBus is
not).

## Decision

On non-macOS, resolve the whole-screen capture backend from two inputs — is
this a Wayland session, and is the screenshot portal reachable — via the pure,
unit-tested policy `chooseLinuxScreenshotBackend`
(`src/platform/ScreenCaptureBackend.cpp`):

- **Not Wayland** (X11/xcb/offscreen/Windows) → `QScreen::grabWindow` as
  before. Unchanged behaviour; the policy ignores portal presence here so X11
  is never perturbed.
- **Wayland + portal available** → drive
  `org.freedesktop.portal.Screenshot.Screenshot` and open the returned image
  (`src/platform/PortalScreenshot.cpp`, Linux-only QtDBus; a stub elsewhere).
- **Wayland + no portal** → **honest-degrade**: the *Whole Screen* menu item is
  `setEnabled(false)` with a tooltip pointing at the missing portal
  (`Application::addAcquireItems`, re-evaluated on menu open), and the capture
  entry point (`Application::captureScreenshot`) flashes an explanation instead
  of returning a null result.

This is the user-visible behaviour change (a control that silently no-oped now
either works or is disabled-with-reason), hence this record per **G6**.

## Threshold (satisfies the backlog item, G1)

On Wayland, the screenshot affordance **either works via the XDG portal, or is
disabled with a tooltip explaining why; it never silently returns null.**
Verified by (a) the deterministic policy test
`TestCaptureBackend::linuxScreenshotPolicy` pinning the four-way selection, and
(b) the gated live test `TestCaptureBackend::livePortalCaptureOrSkip`, which
runs the real portal call end-to-end when a portal is on the session bus and
QSKIPs otherwise (Wine-QSKIP precedent).

## In-code anchors

- Selection policy + enum: `src/platform/ScreenCaptureBackend.h` /
  `.cpp` (`chooseLinuxScreenshotBackend`).
- Portal probe + capture: `src/platform/PortalScreenshot.cpp`
  (`portalScreenshotAvailable`, `capturePortalScreenshotToPng`); non-Linux stub
  `src/platform/PortalScreenshot_stub.cpp`.
- Wiring + honest-degrade: `src/app/Application.cpp`
  (`captureScreenshot` `#else` branch, `addAcquireItems`).

## Notes / limits

- Only **whole-screen** capture is wired to the portal. Window and Selected-Area
  remain disabled-with-tooltip on non-macOS (unchanged; a future item can map
  them to the portal's interactive mode).
- The portal response timeout (30 s, `kResponseTimeoutMs`) is a hand-tuned
  guard against a wedged portal; it is an internal tuning constant, not a
  user-visible default (PHILOSOPHY → *Hand-tuned values stay hand-tuned*).
- The non-interactive request (`interactive=false`) is used for the automated
  whole-screen grab; some backends still draw a brief confirmation. That is the
  backend's choice, honestly surfaced (a Cancelled response is treated as a
  self-caused no-op, not an error).
