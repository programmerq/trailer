---
name: trailer-packaging-release-flow
description: Packaging bundle — PRs #57 (release workflow ordering) and #61 (msi/deb/rpm packagers + upstream license vendoring); key finding is all three native packages build on the Linux/no-Docker runner
metadata:
  type: project
---

# Packaging & release flow

Two draft PRs opened 2026-07-15 for the packaging/release bundle off `main`.

## The two PRs

- **PR #57** (branch `feat/release-workflow-ordering`, head `c7a29d5`) — `release.yml` now gates all three per-OS build jobs on the `uat` job (`needs: [..., uat]`), so a UAT failure short-circuits before any artifact build. Also recorded the `.github`-only artifact-reuse backlog item as WON'T-DO: excluding `.github/` from a source-hash cache would ship stale binaries, because Qt 6.11.0 is pinned INLINE in `release.yml` at the macos-build `install-qt-action` step. Cheaper correct alternative is ccache on the macOS job.
- **PR #61** (branch `feat/packaging-release-flow`, head `2efe388`) — wires `.msi`/`.deb`/`.rpm` into the release and vendors upstream license texts. Closes (fully, not partial) `docs/backlog/2026-07-13-wire-msi-deb-rpm-packagers.md` and `docs/backlog/2026-07-13-ship-upstream-license-files.md`.

## Key feasibility finding (hard-won, saves rediscovery)

All three native packages build DIRECTLY on the Linux trailer-k8s runner with NO Docker daemon:
- `.deb` via pre-installed `dpkg-deb`.
- `.rpm` host-side via `rpmbuild` — apt package `rpm` works on Ubuntu, no Fedora/container needed.
- `.msi` via `wixl` — apt package `wixl` is SEPARATE from `msitools` on Ubuntu noble; install BOTH.

Runner needs `apt-get install -y rpm wixl msitools patchelf dpkg-dev file`. The trailer-runner image ships an empty apt index, so `apt-get update` FIRST. The packagers gained a `--no-docker` host mode (Docker path kept for local dev).

## Self-contained bundling

New shared script `scripts/bundle-qt-runtime.sh` makes deb+rpm self-contained: ldd-BFS Qt/ONNX/ICU closure into `/opt/trailer/lib`, the qxcb plugin, RPATH, and a `/usr/bin/trailer` launcher wrapper setting `QT_PLUGIN_PATH` + `LD_LIBRARY_PATH`. The rpm uses `AutoReqProv: no` plus a curated Fedora `Requires:` list mirroring the deb `Depends`.

## Pre-existing bug found + fixed

The `.deb` was silently NON-FUNCTIONAL: CMake defaulted `CMAKE_INSTALL_PREFIX` to `/usr/local`, so the binary never landed in `usr/bin` and the Qt bundling was a no-op. Fixed by passing `-DCMAKE_INSTALL_PREFIX=/usr`.

## Versioning

Version derives from the `VERSION` file everywhere. rpm spec `Version` (sed of the build copy) and WiX `ProductVersion` (`wixl -D Version`) are reconciled at build time. `CMAKE_INSTALL_DOCDIR` is forced lowercase `share/doc/trailer` intrinsically in CMakeLists — the `project()` name is capital-T `Trailer`, so the default would be `share/doc/Trailer`.

## Unvalidated until a real `release-candidate`-labeled release run (in-container limits)

Runner apt-installs, a full Windows `.msi` build (needs the mingw cross tree), rpm Fedora-repo `Requires` resolution, macOS `.dmg` license staging, and any GUI launch (headless).

## Related

[[trailer-030-release-pr50]] (the #50 follow-up that requested wiring these packagers) · [[trailer-review-before-push-policy]] (4 local review passes run before pushing these) · [[trailer-ci-on-k8s-runners]]
