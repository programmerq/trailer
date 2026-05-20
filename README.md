# Trailer

Cross-platform PDF and image workbench. See [DESIGN.md](DESIGN.md) for the
full specification, [PHILOSOPHY.md](PHILOSOPHY.md) for the hard
constraints, and [AGENTS.md](AGENTS.md) for the current phase / sprint
state.

Trailer reads PDFs and the common image formats, supports markup,
signatures, form filling, redaction, page operations (rotate / delete /
move / crop / insert), and on-device ML features (background removal,
Smart Lasso / Instant Alpha, OCR). It is local-first by construction:
no accounts, no telemetry, no cloud sync. See `PHILOSOPHY.md` for the
full list of non-negotiables.

Releases are tagged `v0.x.0`; see [CHANGELOG.md](CHANGELOG.md) for
what shipped when, [ROADMAP.md](ROADMAP.md) for what's coming, and
[RELEASING.md](RELEASING.md) for the maintainer release runbook.

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

**Windows.** Native MSVC build. Avoids both the Qt online installer
(needs a free Qt Account) and the vcpkg Qt source build (one-to-three
hours). Instead: `aqtinstall` for Qt's prebuilt MSVC binaries, the
official qpdf MSVC prebuilt zip from GitHub.

**One-shot path** (does everything below + builds + tests):

```powershell
scripts/install-windows-deps.ps1   # one-time, ~1 min
scripts/build-windows-native.ps1   # build + unit tests
scripts/build-windows-native.ps1 -RunUat   # build + unit + UAT
```

Or via the Makefile from Git Bash / a Developer Command Prompt:

```sh
make install-windows-deps
make test           # build + unit tests
make test-uat       # build + unit + UAT
```

**Manual / step-by-step:**

1. Install system prerequisites (one-time):
   - **Visual Studio 2022** with the "Desktop development with C++"
     workload (MSVC, Windows SDK).
   - **CMake 3.24+** ([cmake.org](https://cmake.org/download/) or
     `choco install cmake`).
   - **Ninja** (`choco install ninja` or `winget install Ninja-build.Ninja`).
   - **Python 3.9+** ([python.org](https://www.python.org/downloads/)
     or `winget install Python.Python.3.12`) — only needed for the
     `aqtinstall` step.
   - **Git for Windows** ([git-scm.com](https://git-scm.com/download/win)).

2. Install Qt and qpdf into a deps directory (default
   `%USERPROFILE%\trailer-deps`):

   ```powershell
   scripts/install-windows-deps.ps1
   # or override the install root:
   scripts/install-windows-deps.ps1 -InstallRoot D:\trailer-deps
   ```

   That fetches:
   - Qt 6.10.3 `win64_msvc2022_64` + `qtpdf` module via `aqtinstall`.
   - qpdf 12.3.2 MSVC 64-bit prebuilt from `github.com/qpdf/qpdf`.

   Qt 6.10.3 (not 6.11.x) because aqtinstall 3.3.0 doesn't yet
   handle the toolchain-suffixed directory layout Qt 6.11+ uses on
   `download.qt.io`. Linux/macOS CI is on 6.11.0; Windows is on
   6.10.3 until aqt gains 6.11 support. The CMake floor is 6.5.

3. Build + test:

   ```powershell
   scripts/build-windows-native.ps1
   ```

   The wrapper auto-detects the deps from the install step (or set
   `$env:TRAILER_DEPS` to point elsewhere). It enters the MSVC dev
   shell, configures CMake with the right `CMAKE_PREFIX_PATH`, builds,
   then runs `ctest -LE uat`. Pass `-RunUat` to also run the UAT
   label. Pass `-Werror` to flip `TRAILER_WERROR=ON`.

4. To do the same by hand (without the wrapper), open a **Developer
   Command Prompt for VS 2022** and run:

   ```bat
   set DEPS=%USERPROFILE%\trailer-deps
   cmake -S . -B build -G Ninja ^
       -DCMAKE_BUILD_TYPE=Release ^
       -DCMAKE_PREFIX_PATH=%DEPS%\Qt\6.10.3\msvc2022_64;%DEPS%\qpdf
   cmake --build build --config Release --parallel
   set PATH=%DEPS%\Qt\6.10.3\msvc2022_64\bin;%DEPS%\qpdf\bin;%PATH%
   set QT_QPA_PLATFORM=offscreen
   set QT_QPA_FONTDIR=%SystemRoot%\Fonts
   cd build && ctest -C Release --output-on-failure --label-exclude uat
   ```

*Why `QT_QPA_FONTDIR`?* Under `QT_QPA_PLATFORM=offscreen`, Qt 6 on
Windows doesn't enumerate fonts through GDI the way the windowed
plugin does, so `QPdfWriter` falls back to drawing text as filled
vector paths (no `Tj` operators, no `/ToUnicode` CMap). That looks
fine on screen but breaks search-related UATs and any test that
generates searchable PDF fixtures. Pointing at the system fonts
directory gives Qt access to Arial etc. The variable is harmless on
Linux/macOS, where the offscreen plugin already routes through
fontconfig. `tests/CMakeLists.txt` and `tests/uat/CMakeLists.txt`
set this for you on Windows; you only need to export it manually if
you run the test binaries directly.

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
This path is what the release pipeline uses; the native MSVC
[scripts/build-windows-native.ps1](scripts/build-windows-native.ps1)
above is the fast-feedback path for developers working on a Windows
box. PR CI runs both Linux (native) and Windows (native MSVC); the
release pipeline still cross-compiles Windows via Docker so it can
run on the same Ubuntu runner as the Linux build.

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

CI runs the build + unit tests on **Linux** (Ubuntu 24.04, GCC) and
**Windows** (windows-2022, MSVC 2022) on every push and pull request
(`.github/workflows/ci.yml`). The Linux job uses ccache + mold for
sub-minute warm builds; the Windows job uses ninja + cl.exe and
caches the installed Qt via `jurplel/install-qt-action`.

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
`Trailer.app` (Qt frameworks bundled via `macdeployqt`; qpdf
statically linked from a source build inside CI). An Intel-Mac
binary is tracked as future work — ONNX Runtime no longer
publishes a macOS x86_64 / universal2 prebuilt upstream, so the
choices are either an ML-disabled build that ships everywhere or a
third-party ONNX x86_64 bundle; either is acceptable when someone
picks it up. Intel-Mac users can build from source via
`scripts/build-macos.sh` on their host until then.

The bundled `.app` ships **unsigned and un-notarized** by project
policy — Trailer is not enrolled in the Apple Developer Program.
The release body documents the one-time Gatekeeper quarantine
bypass users need to run on first launch (`xattr -dr
com.apple.quarantine /Applications/Trailer.app`). An ed25519-signed
auto-update channel (Sparkle 2 is the leading candidate) is
tracked separately in [ROADMAP.md](ROADMAP.md) — those signatures
protect the update channel itself and don't require Apple
enrollment.

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

## Project docs

| Doc | What it's for |
|---|---|
| [DESIGN.md](DESIGN.md) | Full product spec — phases, features, scope. |
| [PHILOSOPHY.md](PHILOSOPHY.md) | Hard constraints — what stays in, what's off-limits, the friction-reduction rules. |
| [docs/CONVENTIONS.md](docs/CONVENTIONS.md) | Patterns the code already follows (document adapters, PdfCommand, AnnotationStore, UAT slot naming, vendored deps). Recipe-shaped — for adding new code that fits. |
| [docs/smoke-session.md](docs/smoke-session.md) | Reference-user smoke-session protocol: a non-maintainer opens a fresh build and runs three tasks. Run before minor-version bumps. |
| [docs/cross-platform-sprint.md](docs/cross-platform-sprint.md) | Planning artifact grouping current cross-platform packaging items into one sprint. |
| [docs/in-flight-merge-plan.md](docs/in-flight-merge-plan.md) | Historical: the merge plan for PR #24 (the 4-wave HITL merge — landed 2026-05-20). The three CONVENTIONS sections it drafted are now applied as CONVENTIONS §§11-13; the doc is retained as the dependency / risk record. |
| [docs/audit-2026-05-19.md](docs/audit-2026-05-19.md) | Multi-perspective audit (privacy / cross-platform / accessibility / failure-mode) with findings + an action register grouping what to do with each. |
| [docs/packaging-macos.md](docs/packaging-macos.md) | macOS bundling/release reference. |
| [docs/icon-guidelines.md](docs/icon-guidelines.md) | Icon-family design brief. |
| [docs/uat/](docs/uat/) | UAT case specs by area (foundations, viewer, PDF pages, image editing, annotations, cross-cutting, security). Each pairs 1:1 with a slot in `tests/uat/`. |
| [AGENTS.md](AGENTS.md) | Entry point for AI coding agents. `CLAUDE.md` symlinks here. |
| [TODO.md](TODO.md) | Deferred-work tracker, including dated HITL pass and smoke-session entries. |
| [TODO-packaging.md](TODO-packaging.md) | Packaging-specific deferred work. |
| [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md) | License attribution for shipped components. |

## License

Trailer's source is MIT-licensed — see [LICENSE](LICENSE).

The *binary* Trailer ships dynamically links Qt 6, which is LGPL-3.0.
Downstream packagers redistributing a Trailer build must therefore
also ship Qt's unmodified shared libraries and a copy of the LGPL-3.0
text alongside the executable, satisfying the LGPL's relinking
allowance. The other third-party components — ONNX Runtime, qpdf,
toml++, the PaddleOCR English dictionary — and the ONNX model
weights Trailer downloads on first use (U²-Net, MobileSAM, PP-OCRv3)
each carry their own license. All of them are enumerated, with
attribution and notice requirements, in
[THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).
