# Trailer

Cross-platform PDF and image workbench. See [DESIGN.md](DESIGN.md) for the
full specification.

This repository is at **Phase 0 — Foundations**: a runnable skeleton with
window/tab/sidebar shell, settings and recent-files persistence, a command-
line open pipeline, and a stub document adapter. PDF/image rendering starts
in Phase 1.

## Requirements

- CMake 3.24+
- Qt 6.5+ (Core, Gui, Widgets, Test, **Pdf**, **PdfWidgets**, **PrintSupport**)
- [qpdf](https://qpdf.sourceforge.io/) 11+ (lossless PDF page editing)
- A C++20 compiler (MSVC 2022, GCC 11+, or Clang 14+)

Qt PDF is not bundled in all distribution packages. If `find_package(Qt6
COMPONENTS Pdf)` fails, install it via the Qt online installer (check the
"Qt PDF" box) or via [`aqtinstall`](https://github.com/miurahr/aqtinstall)
(`aqt install-qt <host> desktop <version> <arch> -m qtpdf`).

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

CI builds with warnings-as-errors **off** by default (both PR CI
and the release pipeline). Too many warnings fire from inside
Qt / libstdc++ / qpdf system headers — false-positive
`-Wnull-dereference` template-instantiation noise, qpdf's
`POINTERHOLDER_TRANSITION` `#warning`, etc. — to make strict
`-Werror` workable. Warnings still print in the build log and
trailer's own source is kept clean by review. Flip the strict bar
on locally when you're chasing a specific regression:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DTRAILER_WERROR=ON
```

### Platform notes

**macOS.** Install Qt via the Qt online installer or Homebrew
(`brew install qt`). Install qpdf and pkg-config via
`brew install qpdf pkg-config`. Point CMake at the Qt install with
`-DCMAKE_PREFIX_PATH=$(brew --prefix qt)` if it isn't auto-detected.

**Windows.** The instructions below avoid the Qt online installer
(which requires a free Qt Account). Both Qt 6 and qpdf are installed
from source via [vcpkg](https://vcpkg.io). Run everything from a
**Developer Command Prompt for VS 2022** so the MSVC toolchain is on
`PATH`.

1. Install prerequisites:
   - Visual Studio 2022 with the "Desktop development with C++"
     workload (MSVC, Windows SDK, CMake).
   - [Git for Windows](https://git-scm.com/download/win).

2. Install vcpkg (once per machine):

   ```bat
   git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
   C:\vcpkg\bootstrap-vcpkg.bat
   ```

3. Build Qt 6 + qpdf from source via vcpkg. **This is slow** — the
   first Qt build takes roughly one to three hours depending on the
   machine; subsequent Trailer configures are cheap because vcpkg
   caches the result.

   ```bat
   C:\vcpkg\vcpkg install ^
       qtbase:x64-windows ^
       qtpdf:x64-windows ^
       qttools:x64-windows ^
       qpdf:x64-windows
   ```

4. Configure and build Trailer, pointing CMake at the vcpkg toolchain:

   ```bat
   cmake -S . -B build ^
       -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake ^
       -DVCPKG_TARGET_TRIPLET=x64-windows ^
       -DCMAKE_BUILD_TYPE=Release
   cmake --build build --config Release --parallel
   ```

*Faster alternative — prebuilt Qt without the installer.* If the
vcpkg Qt source build is too slow, grab the prebuilt Qt mirror via
[`aqtinstall`](https://github.com/miurahr/aqtinstall) (also
account-free):

```bat
pip install aqtinstall
aqt install-qt windows desktop 6.8.0 win64_msvc2022_64 -m qtpdf
```

Then install only qpdf through vcpkg (step 3 shrinks to
`vcpkg install qpdf:x64-windows`) and add `CMAKE_PREFIX_PATH` to the
configure command:

```bat
cmake -S . -B build ^
    -DCMAKE_PREFIX_PATH=C:\Qt\6.8.0\msvc2022_64 ^
    -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake ^
    -DCMAKE_BUILD_TYPE=Release
```

**Linux.** Install Qt via your distribution (`qt6-base-dev` on
Debian/Ubuntu) or the Qt online installer. Install qpdf and pkg-config via
`sudo apt-get install pkg-config libqpdf-dev` (or the equivalent for your
distribution).

**Cross-compile Windows from Linux.** A Docker-based mingw-w64 setup
lives at [docker/windows/Dockerfile](docker/windows/Dockerfile)
(Ubuntu 24.04 + aqtinstall Qt 6 MinGW + qpdf built from source
against the same toolchain). Run `scripts/build-windows.sh` — the
script builds the image on first use, cross-compiles `trailer.exe`,
and drops it plus required Qt and runtime DLLs into `build-windows/`.
Intended for local use; not wired into CI.

## Run

```sh
./build/trailer                       # empty window
./build/trailer path/to/file.pdf      # opens the file in a stub view
```

Drag files onto the running window to open them.

## Test

```sh
ctest --test-dir build --output-on-failure
```

CI runs the build + unit tests on Linux on every push and pull
request (`.github/workflows/ci.yml`).

## Release process

Heavy artifact builds (Linux native, Windows cross-build via
mingw-w64 in `docker/windows/Dockerfile`, and a macOS Apple Silicon
`.app` packaged as a DMG) plus the full UAT suite live in
`.github/workflows/release.yml`. They are intentionally **not**
triggered on every PR — macOS runner minutes bill at 10× Linux, and
UAT is slow. Instead they are gated on a `release-candidate` label:

1. Open a release PR that bumps `VERSION` off its `-dev` suffix
   (e.g. `0.2.0-dev` → `0.2.0`). The CMake configure regenerates
   `TrailerVersion.h` so `setApplicationVersion()` and the About
   dialog pick up the new string.
2. **Run a local sanity-check** of the release build before
   labeling. The same scripts CI uses are runnable as `make` targets,
   so a green local run is a strong signal CI will succeed:

   ```sh
   make release          # host platform: macOS host → arm64 DMG
   make release-windows  # Windows cross-build via Docker (any host)
   make release-uat      # UAT suite via Docker
   ```

   `make release` honours whatever the `VERSION` file says — a
   `0.1.0-dev` value bakes that string into the About dialog and the
   bundle's `CFBundleShortVersionString`, so the build is clearly
   marked as a dev one when you smoke-test it. Bump `VERSION` once
   the build is green if you didn't already.

3. Add the `release-candidate` label. The label event re-triggers
   the workflow; the precheck job:
   - reads `VERSION` and emits `is-release-ready=false` if it still
     carries `-dev` / `-rc` — this **skips** the heavy build jobs
     (precheck ✅, Linux/Windows/macOS/UAT ⊘ skipped) with a yellow
     `::warning::` + step-summary note. The PR page stays green so
     you can label-then-bump without a noisy ❌.
   - hard-fails (red ❌) if `v$VERSION` already has a corresponding
     git tag or GitHub Release upstream — that's a real duplicate-
     release bug worth surfacing immediately.

4. When the workflow is green, merge the PR with a policy that
   preserves the PR HEAD SHA (fast-forward or merge-commit — **not
   squash**, which would discard the SHA the artifacts were built
   against).

5. `release-autotag.yml` fires on the merged-and-labeled PR: it
   tags PR HEAD as `v$VERSION` and dispatches `release-publish.yml`.
   The publish workflow looks up the prior successful Release run
   for the tagged SHA, downloads its artifacts (Linux tarball,
   Windows zip, macOS DMG), renames them to versioned filenames
   (`trailer-$VERSION-<platform>.<ext>`), and creates the GitHub
   Release. No rebuild — the bytes shipped to users are exactly the
   bytes the release-candidate PR validated.

The macOS DMG contains an Apple Silicon (arm64) self-contained
`trailer.app` (Qt frameworks bundled via `macdeployqt`; qpdf
statically linked from a source build inside CI). Intel-Mac support
is deferred — ONNX Runtime, which powers Trailer's ML features, no
longer publishes macOS x86_64 / universal2 prebuilts upstream, so a
universal binary isn't viable while ML features are link-time deps.
Intel-Mac users can build from source via `scripts/build-macos.sh`
on their host. For 0.1.x the `.app` ships **unsigned** — the
release body documents the one-time Gatekeeper quarantine bypass
users need to run.

### Recovering from a missing prior build

If a tag is pushed manually and no prior Release run exists for its
SHA (e.g. a maintainer tagged a non-PR commit, or an unattended
squash merge dropped the PR HEAD), `release-publish.yml` fails with
a clear recovery message. Run

```sh
gh workflow run Release --ref=<SHA>
```

to seed artifacts for that commit, wait for it to finish, then
re-run the failed Publish Release job from the Actions tab.

## Philosophy

Trailer is built around a few hard-edged constraints — no ads, no
telemetry, no accounts, no premium tier, no cloud sync, local-first.
[PHILOSOPHY.md](PHILOSOPHY.md) spells them out and explains how the
project intends to stay that way.

## License

Trailer is MIT-licensed — see [LICENSE](LICENSE).

Third-party components that ship with the binary (Qt, ONNX Runtime,
qpdf, toml++, the PaddleOCR English dictionary) and the ONNX model
weights Trailer downloads on first use (U²-Net, MobileSAM, PP-OCRv3)
each carry their own license. They are enumerated in
[THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).
