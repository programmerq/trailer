---
id: 2026-07-20-wayland-screenshot-portal-dbus
title: Wayland whole-screen capture via org.freedesktop.portal.Screenshot D-Bus backend
priority: P3
status: open
source: follow-up to the honest Wayland-degrade PR (claude/wayland-capture-honest-degrade)
created: 2026-07-20
---

## Threshold

On a Wayland session, invoking File → Screenshot → Whole Screen captures the
screen via `org.freedesktop.portal.Screenshot` and opens the result, instead of
showing the honest "not available yet" degrade. Implemented behind the existing
`portalUsable` seam so exactly one call site flips
(`linuxCaptureCapability(..., /*portalUsable=*/…)` in
`src/app/Application.cpp`), with the `X11Grab` path left byte-identical.

- [ ] Verified against a **real Wayland session** (GNOME or KDE with
      `xdg-desktop-portal` running) — the portal path must actually produce a
      PNG and open it. This cannot be exercised in CI or on the owner's Mac, so
      a manual on-hardware check is required before this item is Done.

## Context / Body

The honest-degrade PR (`claude/wayland-capture-honest-degrade`) landed platform
detection + capability gating + the never-silent-null degrade, but deliberately
shipped **no D-Bus code**: the XDG Screenshot portal exposes no window/area
mode (only an interactive/modal whole-screen flow), and no Wayland session
exists in CI or on the owner's Mac, so a D-Bus path would ship never-executed
(the class of speculation that broke the owner's build with the never-compiled
`.mm` in #72).

**Scope note — both native Wayland and XWayland degrade today.** The
capability seam routes two distinct sessions to the same
disabled+tooltip/degrade: a native Wayland session (Qt's `wayland` plugin,
where `grabWindow(0)` is null) *and* an XWayland session (Qt's `xcb` plugin
under a Wayland compositor, detected via `WAYLAND_DISPLAY`, where
`grabWindow(0)` returns a **black** pixmap on Mutter/KWin). Flipping the
single `portalUsable` seam to `true` therefore restores capture for **both**
of those sessions at once — the follow-up covers the whole Wayland surface,
not just the native-plugin case. Genuine X11 (WAYLAND_DISPLAY unset) is
unaffected and keeps the direct `grabWindow` path.

Scope for this item:

1. Probe `org.freedesktop.portal.Desktop` for a usable `Screenshot` interface
   and feed the result into the `portalUsable` argument of
   `trailer::linuxCaptureCapability(...)`.
2. Implement the `org.freedesktop.portal.Screenshot.Screenshot` request/response
   (async `Response` signal) and write the returned image to the capture temp
   path, then route into `openFiles(...)` from the existing `WaylandPortal`
   branch of `captureScreenshot`.
3. Add the `Qt6::DBus` component only in this item's PR.

**Out of scope:** Window and Selected-Area capture on Wayland. The XDG
Screenshot portal does not expose per-window or freeform-region modes, so those
items stay disabled-with-tooltip (no new promise, no regression) exactly as the
degrade PR left them.

## Provenance

Split out of `docs/backlog/2026-07-12-wayland-screenshot-portal.md` when the
honest-degrade half shipped; that item's "disabled + tooltip, never silent
null" threshold is now satisfied, and this item carries the remaining
"works via the XDG portal" half.
