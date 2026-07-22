# Wayland whole-screen capture routes through the XDG screenshot portal

- **Status:** accepted
- **Slug:** `wayland-screenshot-portal`
- **Arbiter:** the agent role named for this record; the owner (programmerq) is the escalation-only override.
- **Date proposed:** 2026-07-21
- **Date accepted / superseded:** 2026-07-21 (accepted)
- **References:** backlog item `2026-07-12-wayland-screenshot-portal` (the work
  item this record closes; deleted in the implementing change per the backlog
  close convention). This record **builds on PR #106** ("Honest Wayland
  screenshot degrade + capability gating"), which landed first and added the
  *disable-with-tooltip* honest degrade (its pure `linuxCaptureCapability`
  policy) without a working capture path; #106 deferred the real portal to the
  backlog item `2026-07-20-wayland-screenshot-portal-dbus`. This change
  **implements that portal** and therefore supersedes #106's placeholder:
  `linuxCaptureCapability` (disable-only, no `portalUsable` call site) is
  replaced by `chooseLinuxScreenshotBackend` (which actually routes to the
  portal), and the `2026-07-20-wayland-screenshot-portal-dbus` backlog item is
  closed (deleted) as done. The behaviour is a strict superset — the Wayland
  no-portal case still disables + tooltips exactly as #106 did, and the
  session detection is widened from `WAYLAND_DISPLAY`-only to also honour
  `XDG_SESSION_TYPE`. The companion Wayland-CI PR (#117) adds a
  `docs/ci/wayland-tier.md` sway+grim launch-and-screenshot smoke tier; that
  file is **not present on this branch** and that tier does **not** run the
  live portal test (see *CI coverage* below). Related: the macOS capture
  records
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
- **Wayland + no portal** → **honest-degrade**: the *Whole Screen* control is
  `setEnabled(false)` with a tooltip pointing at the missing portal in **both**
  entry points — the File ▸ Screenshot menu item
  (`Application::addAcquireItems`, re-evaluated on menu open) and the Tools ▸
  Take Screenshot dialog radio (`MainWindow::onTakeScreenshot`) — and the
  capture entry point (`Application::captureScreenshot`) flashes an explanation
  instead of returning a null result.

**Detecting "is this a Wayland session" — from the display-server signals, not
the Qt plugin name.** The session verdict is `isWaylandSession()`
(`src/platform/PortalScreenshot.cpp`), computed by the pure, unit-tested
`isWaylandSessionFromSignals(platformName, WAYLAND_DISPLAY, XDG_SESSION_TYPE)`
(`src/platform/ScreenCaptureBackend.cpp`). A session counts as Wayland when the
native Wayland plugin is loaded (`platformName()` starts with `wayland`) **OR**
`WAYLAND_DISPLAY` is set (non-empty) **OR** `XDG_SESSION_TYPE == "wayland"`.
Keying off the plugin name **alone** is unsafe: under **XWayland** (the common
GNOME/KDE default) Qt loads the `xcb` plugin while the compositor is Wayland,
and `QScreen::grabWindow(0)` there returns a **BLACK pixmap** on Mutter/KWin —
a silent wrong result that would be saved as a "screenshot". The
display-server-signal detection routes that XWayland case to the portal (or the
disabled+tooltip degrade), never to the black direct-grab path. Genuine X11
(no Wayland signals) and the offscreen/CI plugin resolve to `QScreenGrab`
unchanged.

This is the user-visible behaviour change (a control that silently no-oped now
either works or is disabled-with-reason), hence this record per **G6**.

## Threshold (satisfies the backlog item, G1)

On Wayland, the screenshot affordance **either works via the XDG portal, or is
disabled with a tooltip explaining why; it never silently returns null.**
Verified by (a) the deterministic policy test
`TestCaptureBackend::linuxScreenshotPolicy` pinning the four-way selection,
(a2) the XWayland-safeguard tests
`TestCaptureBackend::waylandSessionFromSignalsTruthTable`,
`::xwaylandRoutesToPortalOrUnavailable`, and `::waylandSessionDetectsXWaylandViaEnv`
pinning that an XWayland session (xcb plugin + `WAYLAND_DISPLAY`) resolves to
Portal/Unavailable and never to the black direct grab, and
(b) the gated live test `TestCaptureBackend::livePortalCaptureOrSkip`, which
exercises the real `capturePortalScreenshotToPng` round-trip **only when a
screenshot portal is actually on the session bus**, and `QSKIP`s otherwise
(Wine-QSKIP precedent).

The end-to-end portal round-trip was proven **manually / locally** against a
stood-up headless stack — `sway` (headless) + a private dbus session +
`pipewire` + `wireplumber` + `xdg-desktop-portal-wlr` + a stub Access portal,
driven by `portals.conf` — where the live test runs for real
(`portalScreenshotAvailable()` → true, `capturePortalScreenshotToPng()` → `Ok`,
non-blank PNG). See *CI coverage* for why that proof is not yet reproduced in
automated CI.

## CI coverage

**The live portal path has zero executed coverage in the current PR/release
CI.** Both PR CI and the release UAT gate run under `QT_QPA_PLATFORM=offscreen`
with no screenshot portal on the session bus, so `livePortalCaptureOrSkip`
always hits its `QSKIP` there — the always-on coverage is the deterministic
`linuxScreenshotPolicy` selection test plus the offscreen-safe honest-degrade
wiring, **not** the portal DBus call itself. Automated coverage of the real
`capturePortalScreenshotToPng` round-trip requires a **future CI job that
stands up the full portal stack** (compositor + dbus + pipewire +
`xdg-desktop-portal(-wlr)`). The companion Wayland-CI PR (#117) adds only a
launch-and-screenshot smoke tier (`docs/ci/wayland-tier.md`, not on this
branch); that tier does **not** run the portal test, so it does not close this
gap. Until such a job exists, the portal round-trip's guarantee rests on the
manual/local proof above.

## In-code anchors

- Selection policy + enum: `src/platform/ScreenCaptureBackend.h` /
  `.cpp` (`chooseLinuxScreenshotBackend`).
- Wayland-session detection (display-server signals):
  `src/platform/ScreenCaptureBackend.cpp` (`isWaylandSessionFromSignals`) and
  its live wrapper `isWaylandSession()` in `src/platform/PortalScreenshot.cpp`
  (stub `PortalScreenshot_stub.cpp`).
- Portal probe + capture: `src/platform/PortalScreenshot.cpp`
  (`portalScreenshotAvailable`, `capturePortalScreenshotToPng`); non-Linux stub
  `src/platform/PortalScreenshot_stub.cpp`.
- Wiring + honest-degrade: `src/app/Application.cpp`
  (`captureScreenshot` `#else` branch, `addAcquireItems`) and the Take
  Screenshot dialog `MainWindow::onTakeScreenshot` (`src/ui/MainWindow.cpp`).

## Notes / limits

- Only **whole-screen** capture is wired to the portal. Window and Selected-Area
  remain disabled-with-tooltip on non-macOS (unchanged; a future item can map
  them to the portal's interactive mode).
- The portal response timeout (30 s, `kResponseTimeoutMs`) is a hand-tuned
  guard against a wedged portal; it is an internal tuning constant, not a
  user-visible default (PHILOSOPHY → *Hand-tuned values stay hand-tuned*). It
  bounds **each phase separately** — the initial `bus.call()` ack and the
  subsequent guarded `loop.exec()` wait for the async `Response` — so the
  worst-case GUI-thread block for a maximally-slow-but-not-dead portal is
  **~2× the constant (≈60 s), not 30 s**. A portal that is simply *absent*
  fails fast (no name registered); the doubling only applies to a backend that
  half-answers then wedges.
- The non-interactive request (`interactive=false`) is used for the automated
  whole-screen grab; some backends still draw a brief confirmation. That is the
  backend's choice, honestly surfaced (a Cancelled response is treated as a
  self-caused no-op, not an error).
