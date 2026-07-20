# 0009 — macOS adaptive (light/dark/tinted) app-icon mechanism: Asset Catalog vs Icon Composer

- **Status:** accepted
- **Arbiter:** the agent role named for this record; the owner (programmerq) is the escalation-only override.
- **Date proposed:** 2026-07-15
- **Date accepted / superseded:** 2026-07-15

> **Update 2026-07-16:** the macOS deployment floor was raised from `11.0` to
> `14.0` (real dependency floor: ONNX Runtime 1.25 = 14.0, Qt 6.11 = 13.0). The
> `--minimum-deployment-target 11.0` example commands and the `min-deployment
> 11.0` note below are historical and left as originally recorded; the current
> bundle floor is `14.0` (`resources/macos/Info.plist.in`,
> `scripts/build-macos.sh`, `CMakeLists.txt` `TRAILER_ICON_MIN_MACOS`).

## Context

The v0.3.0 real-Mac dogfood pass (2026-07-13) found that Trailer's dark app-icon
variant never appears on a dark-mode Mac: the Dock and Finder always render the
light squircle, and toggling the system appearance does not swap it. The feeding
backlog item is `docs/backlog/2026-07-17-adaptive-dock-icon-option-b.md` (which
subsumes the former `2026-07-13-macos-dark-app-icon` item); the research
brief is Theme 4 of `docs/research/2026-07-13-ux-research-agenda.md:124-149`. This
record exists to settle the *mechanism* question that theme raises — what the
supported way is to ship a light/dark (and macOS 26 Tahoe tinted/clear) adaptive
app icon, and how it wires into Trailer's CMake-built bundle — because the choice
is a genuine build-integration debate rather than a value that research can just
write back as a threshold (the G6 escalation the agenda anticipates).

**What ships today (so this record isn't misread as describing the target):** a
single, static, light-only `.icns`, with no adaptive mechanism of any kind.
Concretely:

- CMake bundles only the light icon: `TRAILER_MACOS_ICON` is hard-set to
  `.../resources/icons/trailer.icns` (`CMakeLists.txt:222`); the packaging block
  copies exactly that one file into `Trailer.app/Contents/Resources`
  (`CMakeLists.txt:256-260`) and declares it via
  `MACOSX_BUNDLE_ICON_FILE "trailer.icns"` (`CMakeLists.txt:270`).
- The bundle plist wires the single static file through the legacy key:
  `CFBundleIconFile = ${MACOSX_BUNDLE_ICON_FILE}`
  (`resources/macos/Info.plist.in:10-11`). There is no `CFBundleIconName`, no
  `CFBundleIcons`, no `Assets.car`, and no `.icon` in the plist or the tree.
- A dark variant is **shipped and git-tracked but orphaned**:
  `resources/icons/trailer-dark.icns` plus `trailer-dark_{16..1024}.png`
  (confirmed via `git ls-files`), and the generators already know how to emit it
  (`icon/make_iconset.py:41-43,54-57` route `--dark` through
  `make_simplified.render_simplified(size, dark=True)`, described at
  `icon/make_simplified.py:12-19,29-37`). Nothing in the build references any of
  it, so it never reaches the bundle.

The design frame is `docs/icon-guidelines.md` (the app/launcher icon is
explicitly *out of scope* for the toolbar-glyph family — §4 "Out of scope" —
so this record governs a surface that document does not) and
`docs/packaging-macos.md`, whose build is `scripts/build-macos.sh` (unsigned
arm64 `.app`, min-deployment `11.0`; signing/notarization deferred,
`docs/packaging-macos.md:97-114`). Any mechanism here must survive that pipeline
(macdeployqt then DMG) without an Xcode project — Trailer has none.

**Key research finding that reframes the problem.** macOS does *not* auto-swap a
plain `.icns` by appearance, and — unlike iOS 18, where an Asset Catalog `AppIcon`
with `luminosity` light/dark/tinted variants is the shipping mechanism — a
light/dark *Dock app icon* that flips with system appearance is, on the evidence
gathered, fundamentally a **macOS 26 Tahoe** capability delivered by the new
Liquid Glass icon system, whose designated authoring tool is **Icon Composer**
(`.icon`). Pre-Tahoe macOS app icons did not offer an appearance-driven Dock
swap at all. This means the backlog's threshold ("dark-mode Dock shows the dark
icon; toggling appearance swaps it") is likely only satisfiable on macOS 26+,
which the arbiter cycle must confirm on real hardware — see the
needs-live-verification list below. Sources:
- Apple HIG, *App icons* (macOS; Tahoe appearance variants light/dark/tinted/clear):
  https://developer.apple.com/design/human-interface-guidelines/app-icons
- Apple, *Configuring your app icon using an asset catalog*:
  https://developer.apple.com/documentation/xcode/configuring-your-app-icon
- Apple, *Icon Composer*: https://developer.apple.com/icon-composer/
- `actool(1)` man page (the `--compile … Assets.car`, `--app-icon`,
  `--minimum-deployment-target`, `--output-partial-info-plist` flags):
  https://keith.github.io/xcode-man-pages/actool.1.html
- Practitioner walkthrough compiling `.icon` → `Assets.car` (+ backward-compat
  `.icns`) with `actool` for iOS and macOS 26, outside the normal build system:
  https://praeclarum.org/2025/09/12/app-icons.html
- A Qt/C++ (non-Xcode) app doing exactly the Tahoe migration — ship both
  `Assets.car` and the old `.icns`, set `CFBundleIconName`, verify with
  `assetutil`: https://successfulsoftware.net/2025/09/26/updating-application-icons-for-macos-26-tahoe-and-liquid-glass/
- Asset-catalog dark/tinted variant mechanics (iOS framing) and the "Include all
  app icon assets" / `CFBundleIcons` behaviour:
  https://www.nutrient.io/blog/dark-tinted-alternative-app-icons/
- Icon Composer notes — `.icon` complements an existing `AppIcon` set and falls
  back for older systems: https://mjtsai.com/blog/2025/06/23/icon-composer-notes/

## Options

- **A. Asset Catalog + `actool` in CMake → `Assets.car` with luminosity
  variants, wired by `CFBundleIconName`.** Author an `AppIcon` `.appiconset`
  (or `.xcassets` with an `AppIcon` set) whose appearances are `Any` / `Dark`,
  feeding the existing `trailer_*` PNGs as the light images and `trailer-dark_*`
  as the dark images (adopting the orphaned assets). At build time invoke `actool
  --compile <Resources> --app-icon AppIcon --minimum-deployment-target 11.0
  --platform macosx --target-device mac --output-partial-info-plist <partial>`,
  copy the resulting `Assets.car` into `Contents/Resources`, and set
  `CFBundleIconName AppIcon` in `Info.plist.in` (keeping `CFBundleIconFile` as a
  pre-catalog fallback). Lowest new-tooling cost — reuses PNGs the tree already
  has and needs no Mac-only GUI. **But** the luminosity light/dark *app-icon* swap
  is a documented iOS mechanism; whether it drives the macOS Dock's appearance
  swap on any target macOS (11–15, or only 26) is unconfirmed, and this path does
  **not** produce Tahoe's tinted/clear Liquid Glass variants.

- **B. Icon Composer `.icon` authoring (Tahoe-era path).** Author a single
  layered `.icon` in Apple's Icon Composer (macOS 26.4+), defining the
  default / dark / clear / tinted appearance variants, then compile it with the
  same `actool` pipeline (`actool MyIcon.icon --app-icon MyIcon --compile …
  --platform macosx --minimum-deployment-target 11.0`), which emits `Assets.car`
  for macOS 26+ **and** a backward-compatible `.icns` for macOS 11+. Wire via
  `CFBundleIconName`; ship both `Assets.car` and the fallback `.icns` in
  `Resources`. This is Apple's designated path for the adaptive behaviour the
  backlog actually asks for, and the successfulsoftware.net case shows a
  non-Xcode C++/Qt app doing it. **But** it introduces a Mac-only GUI authoring
  step and a re-draw of the icon into layered Liquid Glass artwork; the existing
  flat `trailer-dark_*` PNGs inform but do not directly become the `.icon`.

- **C. Keep the plain `.icns` (do nothing).** Ship the single light `.icns` as
  today. Zero build change; the dark variant stays orphaned. Never satisfies the
  threshold — the Dock never shows a dark or tinted icon — so this is the null
  baseline the record measures the others against, not a live contender.

Options A and B **converge on the same build wiring** — `actool` producing
`Assets.car`, `CFBundleIconName` in the plist, the file copied into
`Resources` — and differ only in the *authoring source* (hand-built luminosity
`.xcassets` from existing PNGs vs. a layered Icon Composer `.icon`) and therefore
in *which appearances* actually ship (A: light/dark only, macOS-Dock-swap
unconfirmed; B: full Tahoe default/dark/clear/tinted).

## Personas debate

The stakes here are unusual: the app icon is native Dock/Finder chrome, so no
persona interacts with it through a Trailer widget — the whole surface lives in
system-owned chrome, which is exactly why the terminus is a real-Mac pass and
`grab()` cannot see it.

- **Office non-technical user:** Runs the Mac on whatever appearance IT set,
  often Dark. Expects Trailer's Dock icon to look "right" — i.e. match every
  other modern app's squircle in Dark mode — without ever thinking about it.
  Has no stake in *how* it's produced, only that Dark mode doesn't leave Trailer
  looking like the one app with a glary light chip in the Dock. Favours anything
  over C.
- **Older careful user:** Notices when one icon looks out of place and reads it
  as "something's wrong / half-installed." A light-only icon sitting bright in a
  dark Dock is the concrete surprise this lens dislikes; a stable, appearance-
  matched icon is reassuring. Neutral between A and B provided the result is
  stable across an appearance toggle and doesn't flicker or fall back to a blob.
- **Power migrator (ex-Preview/Acrobat):** Their reference apps are Apple-native
  and now ship Tahoe Liquid Glass icons with proper dark/tinted rendering. Judges
  Trailer against that bar: a flat non-adaptive chip reads as non-native. This is
  the lens that most favours **B**, because only B produces the tinted/clear
  Tahoe variants their reference apps show; A's light/dark-only result is closer
  but still not what a 26-era native app renders.
- **Occasional user:** Sees the icon rarely and mostly launches from Spotlight,
  not the Dock. Low stake; needs only that the icon isn't visibly broken. Neutral
  across A/B, mildly against C only because "obviously wrong in Dark mode" is the
  one failure even a rare launcher notices.

## Admissible objections

- **Office / older-careful user, Option C:** on a Dark-mode Mac the Dock and
  Finder show the light chip permanently; the concrete failure is "Trailer looks
  wrong / half-installed in Dark mode," which is the exact dogfood finding. This
  is the decisive argument against doing nothing and the reason the record is
  open at all.
- **Power migrator, Option A on macOS 26:** if A ships light/dark luminosity
  variants but the target is a Tahoe Mac, the icon still won't render the tinted
  or clear Liquid Glass appearance the user's other apps show; the failure is
  "Trailer's icon looks a generation behind under Icon & widget style." Admissible
  because it names a real user, a real appearance mode (Tahoe tinted/clear), and a
  concrete visible shortfall — and it is the strongest argument for B over A.
- **Any user, Option A on pre-Tahoe macOS (11–15) — conditional:** if the
  premise that a luminosity `AppIcon` drives the *macOS Dock* appearance swap
  turns out false on pre-26 systems, then A silently fails the threshold on the
  whole pre-Tahoe install base while appearing wired — the failure is "the build
  looks correct, `Assets.car` is present, but Dark-mode Dock still shows light."
  This objection is **admissible but unproven**: it hinges on the
  needs-live-verification item below and cannot be adjudicated from docs alone.
- **Any user, either mechanism, build-integration failure:** if `actool` runs but
  `Assets.car` isn't copied into `Contents/Resources`, or `CFBundleIconName`
  isn't set (or collides with the retained `CFBundleIconFile`), the bundle ships
  a catalog the OS never reads and the Dock shows nothing new — "it built green
  but the icon didn't change." This is why the checkable threshold has a
  build-integration half, not just a visual half.

### Rejected as naked preference

- "Icon Composer is the new hotness, just use it." — rejected: asserts novelty,
  names no user, step, or failure. The admissible pro-B argument is the power
  migrator's concrete Tahoe tinted/clear shortfall above.
- "We already ship `trailer-dark.icns`, so just point CMake at it." — rejected as
  a category error, not a preference with a user-failure, but recorded here
  because it will otherwise be re-raised: macOS has no mechanism to auto-select
  between two sibling `.icns` files by appearance (that is problem #2 in the
  backlog). Adopting the orphaned dark assets is real, but only *through* an
  Asset Catalog or `.icon` (Options A/B), never as a second bare `.icns`.
- "Just keep it simple, the light icon is fine." — rejected: taste dressed as
  minimalism; the admissible version is the office/older-careful Dark-mode
  "looks wrong" failure, which points the other way.

## Checkable threshold this record would establish

Two halves, both required; the visual half is a **real-Mac tier** check because
the surface is native Dock/Finder chrome and `QWidget::grab()` under
`QT_QPA_PLATFORM=offscreen` cannot observe the Dock (per the ux-evidence ruling
cited in `docs/research/2026-07-13-ux-research-agenda.md:19-27` and the backlog's
own "Evidence tier: real-Mac required" note).

1. **Visual (real-Mac tier — `grab()` is insufficient for Dock chrome).** On a
   Mac running the target appearance, build the bundle and observe: Dark-mode Dock
   shows the dark icon; Light-mode Dock shows the light icon; toggling System
   Settings → Appearance swaps them live; Finder matches. On macOS 26 Tahoe,
   additionally: the Icon & widget style tinted/clear settings render the
   corresponding variant (Option B only — Option A cannot pass this sub-line).
   The pass must be recorded on real hardware with the macOS version stated,
   because the mechanism's availability is version-dependent.
2. **Build-integration (inspectable on the artifact).** The shipped
   `Trailer.app/Contents/Resources/Assets.car` exists and contains an `AppIcon`
   with the expected appearance variants (verifiable with
   `assetutil --info Assets.car`), and `Contents/Info.plist` sets
   `CFBundleIconName` to that set's name. If a fallback `.icns` is retained for
   pre-catalog macOS, it is present alongside `Assets.car`, not instead of it.
   This half is checkable without a Mac appearance toggle and gates that the
   visual half is even *possible*.

Option C establishes neither and is the fail baseline. Option A would establish
half 2 and the light/dark portion of half 1 **iff** the pre-Tahoe Dock-swap
premise holds (open). Option B would establish half 2 and the full half 1
including the Tahoe tinted/clear sub-line.

**needs-live-verification (must be closed during the arbiter cycle on real
hardware / a pinned Xcode CLT — not adjudicable from docs):**

- Whether a luminosity `AppIcon` Asset Catalog drives the **macOS Dock** light/dark
  swap on **pre-Tahoe** macOS (11–15), or whether appearance-adaptive Dock icons
  are effectively macOS-26-only. (needs-live-verification)
- The exact `actool` invocation and flag set that compiles a **plain `.xcassets`
  AppIcon** (Option A) vs. a **`.icon`** (Option B) for `--platform macosx` under
  the repo's pinned Xcode Command Line Tools, and whether
  `--minimum-deployment-target 11.0` bundles the icon *into* `Assets.car` vs.
  emitting loose files. (needs-live-verification)
- Whether `Assets.car` alone suffices on Tahoe and whether the backward-compat
  `.icns` is required for the `11.0` floor, i.e. exactly which macOS versions read
  which artifact. (needs-live-verification)
- Whether `CFBundleIconName` must **replace** or may **coexist with** the current
  `CFBundleIconFile` (`resources/macos/Info.plist.in:10-11`), and any interaction
  with the existing `CFBundleDocumentTypes` block. (needs-live-verification)
- How `Assets.car` + the `actool` `--output-partial-info-plist` merge slots into
  `scripts/build-macos.sh` **after** `macdeployqt` and before DMG creation
  without an Xcode project. (needs-live-verification)
- Whether the owner's shipped v0.3.0 build actually contained `trailer-dark.icns`
  (the backlog provenance note flags that current CMake would not include it).
  (needs-live-verification)

## Arbiter verdict + rationale

**Verdict: Option A — Asset Catalog + `actool` luminosity light/dark →
`Assets.car`, wired by `CFBundleIconName` — is the mechanism to implement now.**

Running the persona/arbiter cycle over the four personas and the admissible
objections above, Option A wins the *implement-now* decision on four grounds:

1. **It adopts the assets the tree already ships.** The orphaned
   `trailer-dark_{16..1024}.png` become the `luminosity: dark` images and the
   existing `trailer_*` PNGs the `luminosity: light` images, with no icon
   redraw. Option B requires re-authoring the icon as layered Liquid Glass
   artwork in a Mac-only GUI (Icon Composer) — real new tooling and design work
   this backlog item does not fund.
2. **Lowest tooling cost.** A hand-written `.xcassets` `AppIcon` set is plain
   file authoring that lands in the repo with no Mac-only editor in the loop;
   only the `actool` *compile* step is Mac-only, and that is shared by both A
   and B (the options converge on identical build wiring — `actool` →
   `Assets.car`, `CFBundleIconName`, copied into `Resources`).
3. **Fully CI-safe off-Mac.** All `actool`/`Assets.car` logic sits behind
   `if(APPLE)` in CMake and the Darwin-only `scripts/build-macos.sh`; the
   `.xcassets` files are inert data on Linux/Windows. The Linux `linux-build`
   and `windows-cross` lanes never evaluate `actool`. The build-integration half
   of the threshold (`assetutil --info` shows `AppIcon` with the appearance
   variants; `Info.plist` carries `CFBundleIconName`) is checkable on the
   `macos-14` runner without an appearance toggle.
4. **The real-Mac pass is the gate, not a blocker to landing.** The one point A
   cannot settle from docs — whether a luminosity `AppIcon` actually drives the
   *pre-Tahoe* Dock light/dark swap (11–15), vs. appearance-adaptive Dock icons
   being effectively macOS-26-only — is precisely what the real-Mac verification
   step confirms. Landing A now wires the fully-inspectable build half and lets
   that live pass close the visual half; it does not gamble the CI lanes on the
   unproven premise.

**Documented escalation to Option B.** Option B (Icon Composer `.icon`) is the
recorded escalation, not a rejected path. Escalate to B when either trigger
fires: (a) the six needs-live-verification points below show that A's luminosity
`Assets.car` does **not** drive the pre-Tahoe Dock swap on the target install
base (A would then ship a correct-looking build whose Dark-mode Dock still
renders light — the "built green but icon didn't change" failure), or (b) the
owner prioritises the macOS 26 Tahoe tinted/clear Liquid Glass variants, which
A structurally cannot produce and only B's layered `.icon` delivers (the power
migrator's admissible objection). Because A and B share the build wiring,
escalating is swapping the *authoring source* feeding the same `actool` step —
not re-architecting the bundle.

Option C (do nothing) stays the rejected null baseline: it ships the permanent
light-chip-in-a-dark-Dock failure that opened this record.

## Evidence required to reopen

Reopen (and escalate to Option B, or revise A) if any of the six
needs-live-verification points recorded above under **Checkable threshold →
needs-live-verification** resolves against Option A on real hardware / a pinned
Xcode CLT. Restated as the concrete reopen triggers:

1. A luminosity `AppIcon` Asset Catalog does **not** drive the macOS **Dock**
   light/dark swap on **pre-Tahoe** macOS (11–15) — i.e. appearance-adaptive
   Dock icons prove effectively macOS-26-only. (Escalate to B for the pre-Tahoe
   base, or accept dark-Dock only on 26+.)
2. The `actool` invocation/flags that compile a plain `.xcassets` `AppIcon` for
   `--platform macosx` under the pinned CLT differ from those assumed here, or
   `--minimum-deployment-target 11.0` emits loose files rather than bundling the
   icon into `Assets.car`.
3. `Assets.car` alone does **not** suffice on Tahoe, or the backward-compat
   `.icns` is required for the `11.0` floor in a way A's coexisting
   `CFBundleIconFile` fallback does not already cover.
4. `CFBundleIconName` must **replace** rather than **coexist with**
   `CFBundleIconFile` (`resources/macos/Info.plist.in:10-11`), or it interacts
   badly with the existing `CFBundleDocumentTypes` block.
5. The `Assets.car` + `--output-partial-info-plist` merge cannot slot into
   `scripts/build-macos.sh` around `macdeployqt`/DMG without an Xcode project.
6. The owner's shipped v0.3.0 build is found to have already contained
   `trailer-dark.icns` (changing the provenance the backlog assumed).

Until one of these fires on real hardware, Option A stands as accepted.
