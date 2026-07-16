---
id: 2026-07-13-wire-msi-deb-rpm-packagers
title: Wire up the .msi / .deb / .rpm packagers and attach them to GitHub Releases
priority: TBD
status: done
source: release 0.3.0 artifact reconciliation 2026-07-13
created: 2026-07-13
---

## Resolved 2026-07-15

**Full close.** All three native packagers are now wired into the release
pipeline as steps inside the existing `.github/workflows/release.yml` jobs
(reusing the already-built binaries rather than adding new build jobs):

- **`.deb`** and **`.rpm`** build in the `linux-build` job via
  `scripts/build-linux-deb.sh --no-docker` / `scripts/build-linux-rpm.sh
  --no-docker`. The `.deb` reuses the job's cmake tree (`BUILD_DIR=build`,
  no recompile); the `.rpm` builds host-side on the Ubuntu runner via
  `rpmbuild` (from the `rpm` apt package) with **no Docker and no Fedora
  container needed** — which is why this is a full close, not a partial one.
  Uploaded as artifacts `trailer-linux-deb` / `trailer-linux-rpm`.
- **`.msi`** builds in the `windows-cross` job via
  `scripts/build-windows-msi.sh --no-docker SKIP_CROSS_BUILD=1`, reusing the
  mingw cross-build tree (`wixl`/`msitools` from apt). Uploaded as
  `trailer-windows-msi`.

`release-publish.yml`'s rename map now promotes the three to versioned,
lowercase-`trailer-` public filenames (`trailer-<ver>-x86_64.deb` /
`.rpm`, `trailer-<ver>-windows-x64.msi`) so they match the
`files: dist/trailer-*` release glob, and the release-body install prose
again documents `apt/dpkg`, `dnf/rpm`, and `msiexec`/double-click install.

The recurring **CMake `install()` / binary-path-mismatch** nit is addressed
via the install-rule additions: `cmake --install` (with
`-DCMAKE_INSTALL_DOCDIR=share/doc/trailer`) now stages the binary + desktop
/ metainfo / icon files + license texts into the deb/rpm doc dir, so the
packagers no longer pick the binary up from an ad-hoc path.

AppImage / Flatpak distribution remains separate future work (unchanged).

## Threshold

A tagged release run of `.github/workflows/release-publish.yml` builds and
attaches all three native packages in addition to the current portable
artifacts:

- Windows `.msi` (WiX, from `platform/windows/trailer.wxs`),
- Linux `.deb` (from `packaging/deb/`), and
- Linux `.rpm` (from `packaging/rpm/trailer.spec`),

each installing a runnable Trailer on a clean target (Windows, Debian/Ubuntu,
Fedora). Once they ship, the release-body install notes may again mention
them — until then the notes must not promise them.

## Context

The published v0.3.0 release body previously promised a Windows `.msi` and a
Debian `.deb` / Fedora `.rpm` in its install notes, but the release workflow
only builds and attaches three artifacts:

- `trailer-<version>-linux-x86_64.tar.gz` (portable tarball),
- `trailer-<version>-macos-arm64.dmg` (Apple Silicon), and
- `trailer-<version>-windows-x86_64.zip` (portable zip).

The install-notes boilerplate in `.github/workflows/release-publish.yml`
(the release-body heredoc, ~lines 200-221) was trimmed to the honest trio on
2026-07-13 so the notes stop promising packages that don't exist — Trailer's
no-lying-controls philosophy applied to release notes. This item is the other
half of that fix: either ship the native packages or keep not promising them.

The packaging metadata already exists and was refreshed to 0.3.0 in the
release PR — it just isn't wired into the release pipeline:

- WiX source: `platform/windows/trailer.wxs` (+ `scripts/build-windows-msi.sh` /
  `scripts/build-windows-msi-inner.sh`).
- Debian control tree: `packaging/deb/DEBIAN/` (+ `scripts/build-linux-deb.sh`
  / `scripts/build-linux-deb-inner.sh`; see also
  `platform/linux/TODO-linux-packaging.md`).
- RPM spec: `packaging/rpm/trailer.spec` (+ `scripts/build-linux-rpm.sh` /
  `scripts/build-linux-rpm-inner.sh`; see `packaging/rpm/TODO-rpm-packaging.md`).

Do **not** delete this packaging metadata — this item is the plan to wire it
up. Scope: add build steps to the release workflow, glob the resulting
`.msi` / `.deb` / `.rpm` into `action-gh-release`'s `files:`, and restore the
per-platform install prose once the artifacts are actually attached. This item
also absorbs the recurring CMake `install()`-rules / binary-path-mismatch nit
raised repeatedly on PRs #4/#5 (no `install()` targets → packagers pick up the
binary from the wrong path), so that fix lands here rather than falling between
this and the license-shipping item. AppImage / Flatpak distribution remains
separate future work.

Priority: none stated by the source — recorded as `TBD` per the "don't invent
a priority" rule.
