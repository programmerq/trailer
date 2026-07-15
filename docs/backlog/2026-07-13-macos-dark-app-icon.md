---
id: 2026-07-13-macos-dark-app-icon
title: Dark app-icon variant never shows on a dark-mode Mac (dark .icns shipped but orphaned; no adaptive-icon mechanism)
priority: P3
status: open
source: v0.3.0 real-Mac dogfood report (2026-07-13)
created: 2026-07-13
---

## Threshold

On a dark-mode Mac the Dock/Finder icon renders the dark variant; toggling the
system appearance flips it; light mode shows the light icon.

**Evidence tier: real-Mac required.** This is a native-chrome (Dock/Finder)
appearance item, so per the ux-evidence ruling `grab()` does not suffice — the
threshold is a real-Mac pass: build the bundle, set the system to Dark, and
observe the dark squircle in the Dock; flip to Light and observe the light icon.

Verified on macOS (real hardware): dark-mode Dock shows the dark icon,
light-mode shows the light icon, and toggling appearance swaps them.

## Context

Owner dogfood report: the dark icon variant never appears on a dark-mode Mac.

Root cause — two concrete problems:
1. **The dark asset is orphaned.** A dark variant is shipped and git-tracked —
   `resources/icons/trailer-dark.icns` (plus `trailer-dark_*.png` at all sizes),
   and the generator supports it (`icon/make_iconset.py:14-17,41-43,55-57`,
   `icon/make_simplified.py:12`) — but nothing references it. CMake bundles only
   the light icon: `TRAILER_MACOS_ICON = .../resources/icons/trailer.icns`
   (`CMakeLists.txt:221-222`), packaging block `:256-261`,
   `MACOSX_BUNDLE_ICON_FILE "trailer.icns"` `:270`; the plist wires the single
   static `CFBundleIconFile = ${MACOSX_BUNDLE_ICON_FILE}`
   (`resources/macos/Info.plist.in:10-11`). So the dark `.icns` is never copied
   into the bundle.
2. **`CFBundleIconFile` + a plain `.icns` has no dark-appearance mechanism.**
   macOS does not auto-swap `.icns` files by appearance. Adaptive light/dark
   (and Tahoe "tinted"/"clear") app icons require an **Asset Catalog**
   (`Assets.car`) with appearance variants, or macOS 26 Tahoe's Icon Composer
   `.icon` format. There is no adaptive-icon wiring in the tree — no
   `.xcassets`, no `Assets.car`, no `.icon` (confirmed via `git ls-files`).

Fix direction: author an Asset Catalog with an `AppIcon` carrying `luminosity:
light` and `luminosity: dark` images (feed it the existing `trailer_*` and
`trailer-dark_*` PNGs), compile to `Assets.car` at build, and set
`CFBundleIconName` instead of / in addition to `CFBundleIconFile`; consider an
Icon Composer `.icon` for Tahoe specifically. The macOS-adaptive-icons research
theme in `docs/research/2026-07-13-ux-research-agenda.md` feeds the Asset-Catalog
vs Icon-Composer decision.

## Provenance

v0.3.0 real-Mac dogfood report, 2026-07-13. Root-cause file:line refs from the
grounded investigation pass against `a4abbcf`. Note: the owner's current build
would not even contain `trailer-dark.icns` (per CMake) — confirm on real hardware
whether the bundle shipped it.

The Asset-Catalog vs Icon-Composer mechanism debate is captured in
`docs/decision-records/0009-macos-adaptive-app-icon-mechanism.md` (proposed).
