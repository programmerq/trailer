---
id: 2026-07-17-adaptive-dock-icon-option-b
title: macOS adaptive Dock icon doesn't flip with system appearance — escalate to ADR 0009 Option B (Icon Composer)
priority: P3
status: open
source: owner live dogfood 2026-07-17 (macOS Tahoe, local 0.3.1-dev.0 from fix/macos-actool-byproducts)
created: 2026-07-17
---

## Threshold

On a dark-mode Mac (Tahoe) running an Option-B build, the Dock icon renders the
dark variant and toggling System Settings ▸ Appearance flips it light↔dark.
**Evidence tier: real-Mac required** — a native Dock-chrome appearance item, so
`grab()` does not suffice; the pass is observing the Dock squircle swap on real
hardware.

## Context

**Problem.** The macOS adaptive light/dark app icon — shipped as ADR 0009
**Option A** (luminosity Asset Catalog → `Assets.car` + `CFBundleIconName=AppIcon`)
— compiles correctly but does **not** drive the macOS Dock icon's light/dark
swap. Confirmed on the owner's live dogfood: macOS **Tahoe**, local
**0.3.1-dev.0** build (from `fix/macos-actool-byproducts`), where the Dock icon
stayed the light variant while other apps' icons flipped to dark.

**Ruled out (not a build/wiring bug).**
- *Runtime override:* the only app-level `QApplication::setWindowIcon` is compiled
  out on macOS (`src/main.cpp`, `#ifndef Q_OS_MACOS`), so nothing replaces the
  bundle icon.
- *Missing asset:* a successful `make release-macos` (default, no
  `--skip-adaptive-icon`) hard-requires `Assets.car` + `CFBundleIconName=AppIcon`
  + the dark luminosity variant, so the asset is present. Owner-verifiable via
  `assetutil --info Trailer.app/Contents/Resources/Assets.car | grep -i luminosity`.

**Root cause.** Known limitation documented in **ADR 0009** — a luminosity Asset
Catalog isn't confirmed to drive the macOS Dock appearance swap, and on Tahoe
structurally cannot render the tinted/clear Liquid Glass variants. This is
exactly ADR 0009's reopen-trigger #1 ("built green but icon didn't change"). No
CMake/wiring change makes Option A flip the Dock — it's the mechanism ceiling.

**Proposed fix.** ADR 0009's documented escalation to **Option B — an Icon
Composer `.icon` source** feeding the same actool build step (A and B share build
wiring; the escalation swaps the authoring source, not the pipeline). When
actioned, record a decision — either a new
`docs/decision-records/2026-07-17-<slug>.md` per the current date-slug naming, or
an accepted update to ADR 0009.

**Priority rationale.** P3 — cosmetic; the dev build is acceptable as-is; revisit
for a polished 0.3.1/0.4 release.

## Supersedes the Option-A item (2026-07-18)

This item now **subsumes and replaces** the older P3 backlog file
`2026-07-13-macos-dark-app-icon.md`, which has been deleted. That item tracked
the same underlying defect (the Dock icon not flipping with system appearance)
and carried the ADR 0009 **Option A** work — an authored luminosity Asset Catalog
(`resources/macos/Assets.xcassets/AppIcon.appiconset/`, adopting the previously
orphaned `trailer-dark_*` PNGs) compiled to `Assets.car` via a `xcrun actool`
POST_BUILD step, plus `CFBundleIconName=AppIcon` in `Info.plist.in` and a
`scripts/build-macos.sh` assertion. That Option-A code has landed; the owner's
2026-07-17 live Tahoe dogfood then showed it does **not** drive the Dock swap —
which is exactly ADR 0009's reopen-trigger #1 and the whole reason this Option-B
escalation exists. The only surviving work from the older item was its **real-Mac
visual verification**, which is identical to this item's Threshold, so nothing is
lost by folding it in here. Root-cause history for the orphaned dark asset lives
in ADR 0009.

Related: `docs/decision-records/0009-macos-adaptive-app-icon-mechanism.md`.
