---
id: 2026-08-02-macos-paste-scale-realhw-verify
title: Real-Mac verification — pasteboard declares a Retina capture's scale, and the Feedback item no longer duplicates
priority: P2
source: owner dogfooding report 2026-08-02 (nightly 0.3.1-dev+768.gce56b4b8, macOS Retina)
status: open
created: 2026-08-02
---

## Threshold

On real macOS hardware (Retina, dpr 2), both of the following hold:

1. **Paste scale.** Capture a *window* to the clipboard (⌃⇧⌘4 then Space),
   then `File > New from Clipboard`. The image opens at Actual Size
   occupying `W/2 × H/2` **logical points** for a `W × H` device-pixel
   capture — i.e. the same on-screen size as the window that was captured —
   and the in-app Feedback Report's document line shows
   `devicePixelRatio: 2`. Separately, paste an ordinary non-capture image
   (a copied logo): it must **not** be shrunk, and its report line must show
   `devicePixelRatio: 1`.
2. **Menu.** Open four windows, close two, and open the Trailer application
   menu: **zero** `Feedback Report…` items appear there, and exactly one
   appears in the **Help** menu.

## Context / Body

Both halves of the 2026-08-02 dogfooding fix are guarded behind macOS-only
code paths that the offscreen Linux/Windows harness cannot exercise, so
neither is covered end-to-end by CI:

- **Paste scale.** `src/platform/ClipboardScale.mm` reads
  `NSPasteboard`'s TIFF/PNG representation and derives the declared scale
  from `NSBitmapImageRep`'s `pixelsWide`-vs-`size` ratio. The *policy* that
  consumes it (`src/util/CaptureScale.h`, `recoverCaptureDpr`) is fully unit
  tested; the *reader* is not, because there is no macOS host in PR CI (see
  `.github/workflows/ci.yml` — Linux + Windows only). The open empirical
  question is whether macOS's screenshot-to-clipboard flavour actually
  carries the 144-dpi resolution tags. If it does not, `clipboardImageDeclaredScale()`
  correctly returns 0.0 (it is fail-safe, not fail-wrong) — but the reported
  bug would be **unfixed on the recovery half**, and only the pixel-exact
  zoom stop would remedy it. That is exactly the "evidence required to
  reopen" clause in the decision record.
- **Menu.** `QAction::menuRole()` does nothing off macOS and the offscreen
  platform cannot render a native Cocoa menu bar, so
  `tests/test_menu_placement.cpp` and UAT-XCT-081 assert the *structural
  precondition* (no `ApplicationSpecificRole` actions exist) rather than the
  rendered menu.

A real-Mac run is available on demand: `.github/workflows/dev-build.yml`
has a `dev-macos` job on a self-hosted `[self-hosted, macOS, ARM64]` runner
with a real Aqua session (non-offscreen Qt works there). The runner is not
always online.

Close this item by recording the two observations above — a screenshot of
the application menu and of the pasted capture at Actual Size with its
Feedback Report dpr line — in the closing PR.

## Provenance

Owner dogfooding report, 2026-08-02. Fix and reasoning:
`docs/decision-records/2026-08-02-pasted-capture-scale-and-pixel-exact-zoom-stop.md`;
specs UAT-FND-071 and UAT-XCT-081.
