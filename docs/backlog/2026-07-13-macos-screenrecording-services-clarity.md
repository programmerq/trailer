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

## Reconciliation 2026-07-18 (verified against merged main)

Trimmed to what actually remains open. Only merged-on-`main` state is treated as
closed here; in-flight PRs are cross-referenced, not counted as done.

- **(b) Services submenu — CLOSED.** Resolved as a recorded ruling: **keep** the
  stock AppKit/Qt Services submenu (no `[NSApp setServicesMenu:nil]` shim). The
  "Apple developer settings profiler" entry is injected by the owner's installed
  developer tooling, not registered by Trailer. This decision needs no code and
  no real-Mac pass; it is settled and not to be re-litigated (see the 2026-07-15
  resolution note below). This sub-issue no longer gates the item.
- **(a) core explainer — landed on `main`.** The first-use (never launch-time)
  pre-permission explainer + basic graceful-denial message shipped:
  `src/platform/ScreenCapturePermission.{h,cpp}` with
  `maybeShowScreenCaptureExplainer()` wired at both capture sites
  (`MainWindow::onTakeScreenshot` and `Application::acquireFromScreenshot`).
  Code-verified on `main`. Both `screencapture` sites are behind user actions;
  nothing captures at launch.
- **(a) deny dead-end hardening — STILL OPEN, in-flight in PR #77** (`fix/
  capture-permission-flow`, open/unmerged). A reviewer showed the shipped
  deny-degrade can leave a permanent dead-end (denied state indistinguishable
  from cancel; app re-launches the crosshair into nothing). #77 reworks this to a
  3-state preflight (`CGPreflightScreenCaptureAccess` / `CGRequestScreenCaptureAccess`),
  drops the sticky `screen_capture_attempted` marker, and degrades recoverably.
  Do not consider this closed until #77 merges.
- **Real-Mac verification — STILL OPEN** for the (a) permission flow, on a clean
  TCC state (per the evidence tier).
- **Picker future — tracked in PR #72** (draft ADR, `adr/permissionless-capture`):
  the ScreenCaptureKit `SCContentSharingPicker` backend that would remove the
  permission prompt entirely for stills. Not part of this item's threshold.

**Remaining threshold for this item:** the (a) deny-degrade robustness lands
(via #77 or successor) AND the real-Mac clean-TCC pass is observed. Sub-issue (b)
is done.

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

## Resolution / implementation note (2026-07-15)

**(a) Pre-permission explainer + graceful denial — implemented (code).**
- A shared helper `src/platform/ScreenCapturePermission.{h,cpp}` factors the
  logic. A pure, platform-agnostic query `shouldShowScreenCaptureExplainer(Settings&)`
  reports whether to show (true while unacknowledged) without mutating anything,
  and `acknowledgeScreenCaptureExplainer(Settings&)` persists the `[first_use]`
  flag (`screen_capture_explainer`) only when the user proceeds — so cancelling
  the explainer re-shows it next time, and continuing suppresses it across
  relaunch. A macOS-only, `#ifdef Q_OS_MACOS`-guarded
  `maybeShowScreenCaptureExplainer(Settings&, QWidget*)` shows the one-time modal
  explaining that macOS calls this "Screen Recording" even for a still
  screenshot, points to System Settings ▸ Privacy & Security ▸ Screen Recording,
  and returns whether to proceed.
- Both `screencapture` call sites invoke it immediately before capture:
  `MainWindow::onTakeScreenshot()` and `Application::acquireFromScreenshot()`.
  The permission is therefore **deferred to first actual use, never at launch**
  (nothing in `main.cpp`/`Application` init captures the screen).
- Graceful denial: the previously-silent cancel/empty-output paths now surface a
  user-visible message — `flashStatus(...)` on the status bar in the MainWindow
  path, and a brief informational dialog in the no-window Acquire path (which has
  no status bar) — noting the capture was cancelled and that Screen Recording
  permission may need granting in System Settings. The non-macOS
  `QScreen::grabWindow` fallback is untouched.
- The pure gate is covered by `tests/test_screen_capture_permission.cpp`
  ("shows once then suppressed" + persists across reload); the native dialog is
  Mac-only and verified on real hardware per the evidence tier.

**(b) Services submenu — owner-default decision: keep it (no code).** The
Services submenu is stock AppKit/Qt macOS behaviour that every Cocoa app gets;
it is harmless and the platform default, **not** something Trailer registers
(the "Apple developer settings profiler" entry is injected by the owner's
installed developer tools / config profile). The chosen default is to **keep**
it — we do **not** add `[NSApp setServicesMenu:nil]`. This is a deliberate
keep-vs-suppress ruling, documented here so it is not re-litigated: suppressing
stock system chrome would be more surprising than leaving the standard submenu
in place.

## Provenance

v0.3.0 real-Mac dogfood report, 2026-07-13. Root-cause file:line refs from the
grounded investigation pass against `a4abbcf`.
