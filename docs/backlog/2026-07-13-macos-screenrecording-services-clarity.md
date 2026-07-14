---
id: 2026-07-13-macos-screenrecording-services-clarity
title: macOS first-launch "Screen Recording" prompt is unexplained, and the Services submenu shows unexpected system entries
priority: P3
status: open
source: v0.3.0 real-Mac dogfood report (2026-07-13)
created: 2026-07-13
---

## Threshold

**Evidence tier: real-Mac required.** Both sub-issues are macOS TCC / native-menu
behaviour, so per the ux-evidence ruling `grab()` does not suffice — verification
is a real-Mac pass on a clean TCC state.

(a) Screen-recording permission: the prompt appears only on first *use* of Take
Screenshot / Acquire from Screenshot, never at idle launch; a pre-permission
explainer precedes the first `screencapture`; denial degrades gracefully with an
explanatory status message.

(b) Services menu: decide keep-vs-suppress with the owner; if suppressed, the
Trailer app menu no longer shows a Services submenu.

Verified on macOS (real hardware): resetting TCC and launching idle raises no
prompt; invoking Take Screenshot shows the explainer then the OS prompt; the
Services-menu decision is applied.

## Context

Owner dogfood report, two sub-issues:

**(a) Screen-recording prompt with no visible recording feature.** Trailer has a
screenshot-**import** feature, not a video recorder:
- "Take Screenshot" (Tools menu, ⌘⇧3) — `src/ui/MainWindow.cpp:1402-1404`,
  handler `onTakeScreenshot` `:2063`, macOS path shells to
  `/usr/sbin/screencapture` `:2120` (`-iW` / `-i -s` at `:2110-2116`);
  non-macOS fallback `QScreen::grabWindow(0)` `:2138`.
- "Acquire from Screenshot" — `src/app/Application.cpp:279` (connect), handler
  `:344-356`, also `/usr/sbin/screencapture -i -x` `:347-348`.

On macOS 10.15+, interactive `screencapture` and `QScreen::grabWindow` are gated
behind the **Screen Recording** TCC permission — macOS labels the whole category
"Screen Recording" even for still screenshots, which is why the owner saw a
"recording" prompt with no recording feature. It is intentional and required for
the feature; the naming mismatch is macOS's. **No launch-time capture call
exists** — both `screencapture` sites are behind user-triggered actions, and
nothing in `main.cpp`/`Application` init captures the screen, so an idle
first-launch prompt is unexpected from the code (most likely the Tahoe upgrade
reset TCC and macOS re-prompted). There is no screen-capture usage-description
key (Screen Recording has no app-supplied rationale string, unlike Camera/Mic),
so the app should show its own pre-permission explainer before the first
`screencapture`.

**(b) "Services" submenu with an "Apple developer settings profiler" entry.**
There is **no** Services-menu code in Trailer — grep across `src/` and
`platform/` for Services / `NSApp` services wiring returns nothing. The Services
submenu is the default AppKit/Qt-provided one every Cocoa app gets; the "Apple
developer settings profiler" entry is a system-provided Service injected by the
owner's installed developer tools / configuration profile, not something Trailer
registers. Default, not by-design, harmless.

Fix direction: (a) add an in-app pre-permission explainer before the first
`screencapture` ("Trailer uses macOS screen capture to import a screenshot;
macOS calls this Screen Recording"); optionally guard/hide on denial with a
settings deep-link. (b) Optionally suppress the Services submenu via
`[NSApp setServicesMenu:nil]` (a small `.mm` shim; `src/platform/Share.mm` is the
existing Obj-C++ seam), or document it as standard macOS behaviour and leave it.

Cross-link: `2026-07-12-wayland-screenshot-portal` — the Linux/Wayland side of
the same screenshot-capture surface (G3 "no silent null").

## Provenance

v0.3.0 real-Mac dogfood report, 2026-07-13. Root-cause file:line refs from the
grounded investigation pass against `a4abbcf`.
