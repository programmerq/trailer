# macOS Packaging

The macOS release artifact is an Apple Silicon (`arm64`),
self-contained `trailer.app` shipped inside a drag-to-Applications
DMG. `scripts/build-macos.sh` is the source of truth for the build —
both `make release` (locally) and `.github/workflows/release.yml`
(in CI) invoke it.

Intel-Mac support is deferred: ONNX Runtime (which powers Trailer's
ML features and is on the binary's link path) no longer publishes
macOS `x86_64` or `universal2` prebuilts upstream, so the .app can't
include an x86_64 slice without building ORT from source — a heavy
extra step. Intel-Mac users can run this same script on their host
to build a working arm64-or-x86_64 .app for their machine.

## Prerequisites

- **macOS** (Apple Silicon or Intel). Linux→macOS cross-compile is
  intentionally not supported — see `.github/workflows/release.yml`'s
  header for the SDK-licensing rationale.
- **Qt 6.8+** with the `qtpdf` module — install from
  [qt.io/download](https://www.qt.io/download) (drops it at
  `~/Qt/6.x.y/macos`) or via `brew install qt`.
- **CMake 3.24+ and Ninja** — `brew install cmake ninja`.
- **Xcode Command Line Tools** — `xcode-select --install`.

`qpdf` is **not** required to be installed locally — the script
builds it from source as a static universal lib so the resulting
`.app` has no external dylib dependency.

## Building the DMG

```sh
make release            # convenience wrapper — calls the script below
# or, equivalently:
scripts/build-macos.sh                  # incremental (reuses qpdf install)
scripts/build-macos.sh --rebuild        # wipes build-macos/ and rebuilds qpdf
```

The script auto-detects Qt under `~/Qt/6.*/macos`, then
`brew --prefix qt`. To pin a different install, set `QT_ROOT_DIR`:

```sh
export QT_ROOT_DIR=~/Qt/6.8.0/macos
scripts/build-macos.sh
```

On success:

```
build-macos/trailer.app                 arm64, self-contained
dist/trailer-macos-arm64.dmg            drag-to-Applications DMG
```

The version string baked into the `.app` (About dialog +
`CFBundleShortVersionString`) comes from the top-level `VERSION`
file. Bumping it requires no `scripts/build-macos.sh` change — just
edit `VERSION` and rebuild. See README's "Release process" for the
full release flow.

### Environment overrides

| Variable                   | Default       | Purpose                                                  |
|----------------------------|---------------|----------------------------------------------------------|
| `QPDF_VERSION`             | `12.3.2`      | qpdf release tag built from source                       |
| `MACOSX_DEPLOYMENT_TARGET` | `11.0`        | Min macOS supported (lowest version arm64 runs on)        |
| `WERROR`                   | `ON`          | `-DTRAILER_WERROR=ON/OFF` — keep ON to match CI's bar    |
| `QT_ROOT_DIR` / `QTDIR`    | auto-detected | Path to Qt install (the dir with `bin/macdeployqt`)      |
| `BUILD_DIR`                | `build-macos` | Trailer build tree                                       |
| `DEPS_DIR`                 | `build-macos-deps` | qpdf source + build + install (cached across runs)  |
| `DIST_DIR`                 | `dist`        | DMG output dir                                           |

## Verification

The script runs two post-build checks before zipping:

1. `lipo -archs` confirms both `arm64` and `x86_64` slices are present.
2. `otool -L` confirms no dylib references leak outside `/usr/lib/`,
   `/System/`, `@executable_path`, `@rpath`, or `@loader_path` —
   anything pointing at Homebrew or `/opt` would mean the `.app`
   isn't actually self-contained.

If either check fails the script exits non-zero (and CI does too).

## Signing and notarization (deferred)

For 0.1.x the `.app` is intentionally unsigned. The release body on
GitHub Releases documents the one-time
`xattr -dr com.apple.quarantine /Applications/trailer.app` users
need to run to bypass Gatekeeper.

When Apple Developer ID signing + notarization is wired up, it'll
plug into `scripts/build-macos.sh` between the macdeployqt step and
the DMG creation step. The stub at
`platform/macos/entitlements.plist` is the hardened-runtime config
the eventual `codesign` call will reference.

Tracked TODOs:
- [ ] Add Apple Developer Team ID + signing identity to
      `scripts/build-macos.sh`
- [ ] Add `notarytool` submission + `stapler staple` to the script
- [ ] Integrate Sparkle for auto-updates (see DESIGN.md §12)
