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

## Investigation update (2026-07-31, macOS Dock-menu-and-icon session)

Re-investigated per the owner's "look at git history, or take a fresh look"
request. Two findings, neither closes this item — both sharpen it.

**1. Git history confirms there is no prior *working* build to regress
from.** `9b9063f` shipped ADR 0009 Option A (luminosity `.xcassets` →
`actool` → `Assets.car`, `CFBundleIconName=AppIcon`); `111e798`/`39aa542`
fixed the `actool` invocation itself (missing
`--output-partial-info-plist`, an invalid BYPRODUCTS genex) so the compile
step actually produces `Assets.car`. That's the full history — Option A was
never *observed* driving the Dock swap before the 2026-07-17 dogfood found
it doesn't. The owner's "did a previous build work" is answered: no — what
shipped was build-integration-correct (verified via `assetutil`) but the
*visual* half was never checked until the Tahoe dogfood found it fails.
Nothing regressed; the mechanism was unconfirmed from day one, exactly as
ADR 0009's own "needs-live-verification" list already flagged.

**2. Fresh research surfaced real, citable instability in this whole area
across 2025–2026 — but nothing that pins today's exact root cause without
real-hardware access.** Sources: [Michael Tsai's actool/Tahoe-icon
thread](https://mjtsai.com/blog/2025/08/08/separate-icons-for-macos-tahoe-vs-earlier/)
(running commentary Aug 2025 → Feb 2026), [Apple Developer Forums thread
797463](https://developer.apple.com/forums/thread/797463) ("macOS 26 Beta
Dark Mode Icons Fallback Removed"), and
[successfulsoftware.net's Tahoe icon writeup](https://successfulsoftware.net/2025/09/26/updating-application-icons-for-macos-26-tahoe-and-liquid-glass/)
(the same URL ADR 0009 already cites as evidence for Option A — re-reading
it closely, its actual worked example compiles a **`.icon` file** via
`actool --app-icon Icon --include-all-app-icons`, i.e. it demonstrates
**Option B**'s mechanism, not Option A's plain-bitmap luminosity catalog;
ADR 0009's citation of it as Option-A evidence should be read with that
correction — not retracted, since the shared-build-wiring point it
supports still holds, but "an app just like ours confirmed the plain
luminosity path works" is not what it actually shows).

Concretely: (a) macOS 26 betas 1–3 auto-generated a fallback dark
appearance for icons with no developer-supplied dark asset; that fallback
was removed at beta 4 and confirmed gone as of beta 7, so by GA an app
*with* a real dark asset (which Trailer ships) is exactly the case Apple
says should now render correctly — this cuts *against* assuming Option A
is structurally broken. (b) Conversely, `actool`'s app-icon compile path
has an undocumented `--enable-icon-stack-fallback-generation` flag whose
default behaviour silently substitutes actool's own generated icon in
place of the developer's `.xcassets` bitmaps under some conditions, and
multiple developers report the exact matching/precedence behaviour
changing between Xcode 26.0 and 26.1, with Apple confirming a "by design"
change mid-stream. Neither (a) nor (b) was tested against Trailer's
*specific* shape (a `.icon`-free `.xcassets` `AppIcon` with `luminosity:
dark` variants) by any source found — this is exactly the kind of claim
ADR 0009 already fences off as **real-Mac-tier, not adjudicable from
docs**, and that fence is confirmed to still be the right call rather than
guessing further.

**What this PR does NOT do, and why:** it does not change the `actool`
invocation (e.g. adding the undocumented flag above) — landing an
unverified flag flip on an already-broken, actively-shifting mechanism
risks a false "should be fixed now" claim, which is exactly the
half-shipped outcome the owner's brief for this session rules out. What it
DOES add, low-risk and inspectable without a Mac:

- `scripts/build-macos.sh`'s existing `assetutil --info` build-integration
  check now also greps for a luminosity/dark marker in the *compiled*
  catalog (not just the source `.xcassets`, which the actool
  auto-substitution behaviour above means can diverge from what's
  actually compiled in) — informational only, never fails the build, so
  it can't itself regress a passing pipeline. Gives the next real-Mac pass
  concrete build-log evidence instead of requiring a manual `assetutil`
  invocation.

**Two cheap checks for the owner's next real-Mac pass, before spending
Icon-Composer authoring effort:**
1. **Rule out icon-cache staleness** — a `.icns`/`Assets.car` swap on an
   already-installed `.app` at the same bundle path is a well-known macOS
   gotcha where the Dock/Finder keep rendering a cached icon. Before
   judging Option A broken: `killall Dock; killall Finder`, or move
   `Trailer.app` to a new path once, and re-observe. This is unrelated to
   the Tahoe-specific uncertainty above and costs ten seconds.
2. **Confirm the Xcode/Icon-Composer version, not just "full Xcode."**
   Icon Composer (Option B's authoring tool) requires **Xcode 26+**
   specifically — an older full Xcode install (pre-26) has no Icon
   Composer and its `actool` won't recognise a `.icon` source at all.
   Check with `xcodebuild -version` on the M4 VM before assuming Option B
   is tooling-ready there.

Priority and status unchanged (P3, open) — this remains a real-Mac-tier
item; nothing above is a substitute for the Threshold's live observation.

## Investigation update (2026-08-02, "is it a manifest one-liner?")

Asked before committing to Icon-Composer authoring effort: **actool is
dropping the dark bitmaps — is that a flag or a manifest key we're
missing, or is it the mechanism ceiling?** Answer: **the mechanism
ceiling. There is no one-liner.**

**The drop is now confirmed from CI, not inferred.** The 2026-07-31
informational `assetutil` grep added by the previous investigation has
been running on every nightly macOS lane since. `nightly-20260802`
(run 30745321546, job 91489836596) logs:

```
==> Verifying adaptive app icon (Assets.car + CFBundleIconName)
    Assets.car present; CFBundleIconName=AppIcon; assetutil confirms AppIcon
    (info) compiled catalog does NOT visibly mention dark/luminosity —
           see docs/backlog/2026-07-17-adaptive-dock-icon-option-b.md
```

So: catalog compiles, `AppIcon` set lands, **zero dark/luminosity
entries survive**. Option A has never worked, on any build — consistent
with finding 1 above (there is no prior working build to regress from).

**actool emits no warning.** The same log shows actool's full result
plist, and it contains only `com.apple.actool.compilation-results` with
three `output-files` — no `com.apple.actool.notices`, no warnings. The
`appearances` key in
`resources/macos/Assets.xcassets/AppIcon.appiconset/Contents.json` is
parsed without complaint and then has no effect on the `--app-icon`
compile. A silent, successful-looking no-op is exactly why this shipped
looking correct.

**Why: `luminosity` appearance variants are not an input to the macOS
app-icon compile path.** `appearances` is the documented dark-mode
mechanism for `.imageset`s; the macOS **`.appiconset`** has no supported
light/dark variant axis. Per-appearance *app* icons arrived on macOS with
Tahoe's Icon Composer `.icon` format — i.e. ADR 0009 **Option B** — not
by adding keys to a bitmap appiconset. Corroborating (all secondary, none
Apple-official, none testing Trailer's exact shape):

- Keith Harrison, [Adding Icon Composer Icons to Xcode](https://useyourloaf.com/blog/adding-icon-composer-icons-to-xcode/):
  "You no longer need to add default, dark, and tinted variants of the
  app icon to the asset catalog… drag the Icon Composer `.icon` file into
  the project navigator" — the catalog route is superseded, not
  configured differently.
- Frank A. Krueger, [App Icons](https://praeclarum.org/2025/09/12/app-icons.html):
  `actool` takes a **`.icon`** file and produces "(1) a compiled
  `Assets.car` that contains the layered icon for macOS 26 and iOS 26,
  and (2) a backwards-compatible `.icns`". Trailer's build emits exactly
  that pair (`AppIcon.icns` + `Assets.car`) — but from bitmaps, so the
  layered/appearance half has nothing to carry, and `.icns` has no
  appearance axis at all.
- Howard Oakley, [Appearance matters](https://eclecticlight.co/2025/09/15/appearance-matters-get-tahoe-looking-in-better-shape/):
  "Using the new Icon Composer, each mode is designed separately… a
  developer can (and often should) use a different icon for default and
  dark modes."

This also **retires the `--enable-icon-stack-fallback-generation` lead**
from the 2026-07-31 update. That flag governs whether actool *generates*
a fallback icon stack; it does not make an `.appiconset`'s `luminosity`
variants compile. Flipping it would not have helped, which is the outcome
the previous session's decision not to land it unverified was protecting
against.

**One alternative hypothesis is not yet falsified**, and it is the only
thing that could still change the recommendation: that the dark bitmaps
*are* in `Assets.car` and `assetutil --info` simply does not report an
appearance axis for app-icon entries — making the grep a false negative
and the root cause something else entirely. `scripts/diagnose-appicon-dark-variants.sh`
(added with this update) settles it in one run on any Mac with Xcode, by
compiling the catalog twice — once as-is, once with every `_dark` entry
stripped — and comparing the two `Assets.car` outputs byte-for-byte.
Identical output proves the dark bitmaps are absent from the archive
regardless of what `assetutil` chooses to print.

**Recommendation:** run that script once; if it confirms (expected),
proceed to Option B — author an Icon Composer `.icon`, keep the existing
`actool` wiring, and record the outcome as an accepted update to ADR
0009. Do not spend further effort on the Option-A catalog. The two cheap
real-Mac checks from the previous update (icon-cache staleness;
`xcodebuild -version` ≥ 26) still apply before judging an Option-B build,
and the runner's Xcode 26.3 satisfies the second.
