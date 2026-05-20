# Cross-Platform Stabilisation Sprint

A grouped piece of work, not a feature. The goal is to stop paying
cross-platform packaging tax in small irregular increments and pay it
in one focused stretch, so the next user-facing feature can ship to
all three platforms without dragging packaging fixes along with it.

This document defines scope, items, sequence, and exit criteria. It
is a planning artifact; once the sprint is in flight or done, this
doc gets either deleted or rewritten as a retrospective note in
`TODO.md`.

## Why now

Three signals point at the same problem at once:

- Multiple branches on the maintainer's local clone exist *only* to
  fix platform-specific friction: `claude/macos-adhoc-sign` (Qt 6.8
  needs adhoc signing to produce a proper .app),
  `claude/release-flag-default` (CI Qt was on the wrong minor
  version), `claude/priceless-jang-50feec` (Retina icon-size checks),
  `claude/ci-harden-dev-phase` (qpdf 11.0 compat + CI timeouts). Each
  is a small change; together they represent real cost.
- Linux's `PowerSource` query is a stub that returns `Unknown`,
  which means battery-aware policy (e.g. throttling speculative ML
  on a laptop) silently doesn't honour itself on Linux.
- The Windows native MSVC path landed on main (`37bb4cb` series) but
  the three Windows build scripts (`scripts/build-windows.sh` MinGW,
  `scripts/build-windows-native.ps1` MSVC, `scripts/build-windows-msi.sh`
  installer) have not yet been reconciled into one decision about
  which is the supported path.

Each item is small. The combined cost is hidden, because it shows up
as "why is this feature branch also touching CI / packaging /
icons / qpdf?" Bundle them and the cost is visible, scopeable, and
finishable.

## Scope

**In scope:**

- Land or rebase every pending cross-platform-only branch on main.
- Reconcile the Windows build-script trio into a documented "this
  is the supported path" decision.
- Implement Linux `PowerSource` so the existing ML policy contract
  is honoured on every platform that ships.
- Fix the macOS Dock-icon / adhoc-sign / Qt-6.11 trio so a fresh
  `make release` on Apple Silicon produces a working notarisation-
  ready bundle without per-release tweaks.
- Either delete `cmake/toolchain-mingw-w64.cmake` or explicitly
  document its role next to the native-MSVC path.

**Out of scope (revisit after sprint):**

- **Intel-Mac ML-disabled mode.** `docs/packaging-macos.md` already
  documents why this is deferred (ONNX Runtime doesn't publish
  macOS x86_64 prebuilts upstream). The sprint should not solve this
  but should leave a single-paragraph "what would re-open this" note
  somewhere durable.
- **Notarisation + signing with a paid Developer ID.** Per the
  maintainer's standing instruction, the Apple Developer Program
  ($99/yr) is deferred without a funding plan; adhoc signing is the
  ceiling for now. The sprint targets "adhoc-signed bundle that
  works," not "Apple-notarised bundle."
- **Windows installer signing.** Same shape as macOS — defer until
  there's a code-signing certificate budget.
- **Auto-updater work** (`claude/friendly-haslett-8bdd21`, Sigstore
  keyless updates). Out of scope; conceptually adjacent but its own
  workstream.

## Items

Items are ordered by dependency, not by importance. Subsections call
out which existing branch (if any) is the starting point.

### macOS

1. **Adhoc-sign the released bundle.**
   Start point: `claude/macos-adhoc-sign` (3 commits) +
   `claude/release-flag-default` (which already contains the
   adhoc-sign commits plus a Qt 6.11 bump and a Dock-icon fix).
   Land both onto main. Verify a `make release` on Apple Silicon
   produces a launchable bundle whose Dock tile is the bundled
   .icns and which doesn't trip Gatekeeper's "damaged" check on a
   fresh download.

2. **Retina icon-size check.**
   Start point: `claude/priceless-jang-50feec`. The unit test for
   icon sizes was comparing raw pixel sizes and failing on Retina
   displays; the fix is to compare `deviceIndependentSize()`. Land.

3. **Confirm the bundle name + identifier.** The
   `claude/release-flag-default` branch renames the bundle to
   Title Case `Trailer.app`. Verify nothing else (LaunchServices
   document associations, deep-link handlers, the `info.plist`
   `CFBundleDocumentTypes` arrays) still references the old name.

### Windows

4. **Choose and document the supported Windows build path.**
   Three scripts coexist on main:
   - `scripts/build-windows.sh` — MinGW path (older).
   - `scripts/build-windows-native.ps1` — MSVC path (newer, landed
     in the `37bb4cb` series).
   - `scripts/build-windows-msi.sh` — installer wrapper.

   Pick one production path. If MSVC native is the production path,
   either delete the MinGW script + `cmake/toolchain-mingw-w64.cmake`
   or explicitly mark them as "developer convenience, not shipped."
   Write `docs/packaging-windows.md` mirroring `packaging-macos.md`:
   prerequisites, the source-of-truth script, what gets deployed
   alongside `trailer.exe`, and where qpdf + Qt DLLs come from.

5. **AnnotationOverlay translucent-background regression.**
   `origin/copilot/fix-ci-cmake-qpdf-discovery` carries a fix:
   `WA_TranslucentBackground` was crashing on Windows offscreen.
   Verify the fix is on main (it should be, as part of the offscreen
   UAT work) and the regression test exists; if not, port it forward.

### Linux

6. **Implement `PowerSource` for Linux.**
   The `PowerSource` interface landed in
   `src/platform/PowerSource.{h,cpp}` with PR #24 (wave 2). On Linux
   the implementation currently stubs to `PowerState::OnAC`, which
   the ML scheduler policy treats as "no constraint." On a laptop
   this means speculative ML runs at full tilt on battery. Implement
   using `/sys/class/power_supply/*/online`, falling through to
   `OnAC` when the path doesn't exist on a desktop. The test seam is
   already in place. Now unblocked; tracked as a follow-up in
   `TODO.md ## 2026-05-19 HITL pass` (ML scheduler section).

7. **DEB + RPM staging-path sanity check.**
   `worktree-agent-ade3233dcaecec9c7` (May 6) noted that staged
   paths weren't lining up because `CMAKE_INSTALL_PREFIX` wasn't
   being passed as `/usr`. Confirm both scripts (`build-linux-deb.sh`
   and `build-linux-rpm.sh`) pass it now and the resulting packages
   place files where their respective distros expect.

### CI / cross

8. **Match CI Qt to local-development Qt.**
   `claude/release-flag-default` includes `ci: bump Linux + macOS
   Qt to 6.11.0 (match maintainer's local dev env)`. The reason this
   matters: when CI is on an older Qt, build failures on the
   maintainer's machine don't reproduce in CI and vice versa, which
   shreds the trust contract of green CI. Land.

9. **qpdf 11.0 compat sweep.**
   `claude/ci-harden-dev-phase` carries `PdfEditor: replace
   QPDFFormFieldObjectHelper::isChecked() with a 11.0-compatible
   inline check`. Land. Then grep the source for other qpdf-version-
   sensitive call sites (`QPDFFormFieldObjectHelper`, `QPDFAcroForm*`,
   anything where qpdf has documented API changes between 10.x and
   11.x) and add a single comment block in `src/pdf/` listing the
   compat-relevant calls and which qpdf version each was last
   verified against. This is the "magic-number with reasons-in-code"
   convention applied to API-version pins.

10. **`-Werror` advisory job.**
    `claude/ci-harden-dev-phase` drops the advisory `-Werror` job
    in favour of a tightened-timeout fast-fail. The trade-off was
    correct under deadline pressure but the advisory job exists for
    a reason: it catches latent warnings before they become errors
    when the toolchain updates. Re-add as a *non-blocking* job that
    posts a comment but doesn't fail the build, so the signal is
    still visible.

## Sequence

The dependency graph is:

```
8. CI Qt bump  ────────┐
                       ├──► 1. Adhoc-sign ──► 2. Retina check
9. qpdf compat sweep ──┘                      3. Bundle name verify

4. Windows path decision ──► docs/packaging-windows.md
5. Translucent-bg regression test (verify on main)
6. Linux PowerSource (interface landed with PR #24; sysfs read pending)
7. DEB/RPM staging check
10. Advisory -Werror job (anywhere; independent)
```

Items 8 and 9 land first because every other CI-touching item is
easier to validate once CI is on the right Qt + the qpdf compat
breakage is fixed. The macOS chain (1 → 2 → 3) sits on top. Windows
(4, 5) and Linux (6, 7) are parallel tracks. The advisory `-Werror`
re-add (10) is independent and can land at any point.

## Exit criteria

The sprint is done when, on a freshly-cloned checkout:

- `make release` on Apple Silicon produces a launchable Trailer.app
  with the bundled .icns showing in the Dock, no Gatekeeper
  complaint on download (adhoc-sign acceptance), and a Title-Case
  bundle name.
- `scripts/build-windows-native.ps1` on a clean Windows machine
  produces `trailer.exe` that launches and opens a PDF; the deployed
  Qt + qpdf DLLs are alongside it; `docs/packaging-windows.md`
  describes the path.
- `scripts/build-linux-deb.sh` and `build-linux-rpm.sh` produce
  installable packages whose files end up under `/usr` on a clean
  Debian and a clean Fedora respectively.
- The `PowerSource` Linux implementation reports `OnBattery` on a
  laptop running on battery and `OnAcPower` plugged in (verified
  manually).
- CI is on Qt 6.11.0 for Linux and macOS, on the documented MSVC
  toolchain for Windows native, and the advisory `-Werror` job is
  posting comments without blocking.
- Each of the source-of-truth branches has been landed or formally
  closed.

## Notes for the implementer

- **Order branches by minimum-conflict, not by importance.** Item 8
  (CI Qt bump) touches `.github/workflows/`; item 9 (qpdf compat)
  touches `src/pdf/`; landing 8 before 9 minimises rebase churn.
  Items 1–3 (macOS) all touch the bundle, so land them together as
  one merge if practical.
- **Don't skip writing `docs/packaging-windows.md`.** The mac doc
  exists because future-maintainer-coming-back-to-this thanked
  past-maintainer-for-writing-it-down. Windows deserves the same.
- **`PowerSource` Linux is the smallest of the bunch.** The
  interface landed with PR #24; the Linux sysfs read is a few
  dozen lines.
