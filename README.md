# Trailer

macOS Preview, cross-platform: open, read, mark up and lightly edit PDFs
and images on macOS, Windows and Linux, with one set of habits everywhere.

Trailer reads PDFs and the common image formats, supports markup,
signatures, form filling, redaction, page operations (rotate / delete /
move / crop / insert), and on-device ML features (OCR, background
removal, Smart Lasso / Instant Alpha). It is local-first by
construction: no accounts, no telemetry, no cloud sync.

[PRODUCT.md](PRODUCT.md) says what Trailer is and the rules it follows,
[todo/](todo/README.md) holds all open work, one file per entry, and [AGENTS.md](AGENTS.md) says
how to work on the code. Releases are tagged `vX.Y.Z`; see
[CHANGELOG.md](CHANGELOG.md) for what shipped when and
[RELEASING.md](RELEASING.md) for the maintainer release runbook.

## Requirements

- CMake 3.24+
- Qt 6.6+ with Core, Concurrent, Gui, Widgets, Network, Svg, Test, **Pdf**, **PdfWidgets** and
  **PrintSupport**, plus DBus on Linux
- [qpdf](https://qpdf.sourceforge.io/) 11+ (lossless PDF page editing)
- ONNX Runtime 1.25 for the on-device ML features. CMake uses an installed copy (or
  `-DTRAILER_ORT_ROOT=<dir>`), and otherwise downloads Microsoft's checksum-pinned prebuilt.
  Microsoft publishes none for Intel Macs, and Trailer ships no Intel macOS build.
- A C++20 compiler (MSVC 2022, GCC 11+, or Clang 14+)

Qt PDF is not bundled in all distribution packages. If `find_package(Qt6
COMPONENTS Pdf)` fails, install it via the Qt online installer (check the
"Qt PDF" box) or via [`aqtinstall`](https://github.com/miurahr/aqtinstall)
(`aqt install-qt <host> desktop <version> <arch> -m qtpdf`).

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel "$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
```

(`--parallel` with no number lets the build tool pick, and GNU Make then runs unbounded; always
pass a job count.)

Warnings-as-errors is off by default, in CI too, because Qt, libstdc++ and qpdf headers raise
warnings Trailer can't fix. Turn it on with `-DTRAILER_WERROR=ON` when chasing a regression.

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
   6.10.3 until aqt gains 6.11 support. The CMake floor is 6.6.

3. Build + test:

   ```powershell
   scripts/build-windows-native.ps1
   ```

   The wrapper auto-detects the deps from the install step (or set
   `$env:TRAILER_DEPS` to point elsewhere). It enters the MSVC dev
   shell, configures CMake with the right `CMAKE_PREFIX_PATH`, builds,
   then runs `ctest -LE 'uat|known-bug|advisory'`. Pass `-RunUat` to
   also run the UAT label. Pass `-Werror` to flip `TRAILER_WERROR=ON`.

4. To do the same by hand (without the wrapper), open a **Developer
   Command Prompt for VS 2022** and run:

   ```bat
   set DEPS=%USERPROFILE%\trailer-deps
   cmake -S . -B build -G Ninja ^
       -DCMAKE_BUILD_TYPE=Release ^
       -DCMAKE_PREFIX_PATH=%DEPS%\Qt\6.10.3\msvc2022_64;%DEPS%\qpdf
   cmake --build build --config Release --parallel %NUMBER_OF_PROCESSORS%
   set PATH=%DEPS%\Qt\6.10.3\msvc2022_64\bin;%DEPS%\qpdf\bin;%PATH%
   set QT_QPA_PLATFORM=offscreen
   set QT_QPA_FONTDIR=%SystemRoot%\Fonts
   cd build && ctest -C Release --output-on-failure --label-exclude 'uat|known-bug|advisory'
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

**Linux.** Ubuntu 24.04 packages Qt 6.4, older than the 6.6 floor. There, install Qt with the
Qt online installer, with aqtinstall (what CI does) or from conda-forge (what
[scripts/session-setup.sh](scripts/session-setup.sh) does for cloud agent sessions). A
distribution's own Qt 6.6+ works if it includes Qt PDF. Install qpdf and pkg-config with
`sudo apt-get install pkg-config libqpdf-dev` or your distribution's equivalent.

**Cross-compile Windows from Linux.** `scripts/build-windows.sh` builds a mingw-w64 toolchain
image from [docker/windows/Dockerfile](docker/windows/Dockerfile) on first use, cross-compiles
`trailer.exe`, and puts it with the Qt and runtime DLLs it needs in `build-windows/`. CI and the
release pipeline run the same cross-build without Docker
(`.github/actions/setup-windows-cross`, then `scripts/build-windows.sh --in-container`), and PR
CI runs the Windows unit tests under Wine. The native MSVC job in `ci.yml` is switched off
([CI tests Windows only under Wine, and most of CI guards process, not the product](todo/2026-10-15-ci-windows-only-under-wine.md)), so the PowerShell scripts above are only exercised by hand.

## Run

```sh
./build/trailer                       # Windows, Linux: an empty window with Open and Recent
./build/trailer path/to/file.pdf      # opens the file
open build/Trailer.app                # macOS: no window, only the menu bar and the Dock icon
```

Drag files onto a window, or onto the Dock icon on macOS, to open them.

To make double-clicking a PDF or image open Trailer instead of your OS's
default viewer, see
[docs/set-as-default-app.md](docs/set-as-default-app.md).

## Test

```sh
QT_QPA_PLATFORM=offscreen ctest --test-dir build -LE 'uat|advisory' --output-on-failure  # unit + known-bug
QT_QPA_PLATFORM=offscreen ctest --test-dir build -L uat --output-on-failure              # UAT: the whole app
QT_QPA_PLATFORM=offscreen ctest --test-dir build -L advisory --output-on-failure         # never gates
```

Known-bug tests (label `known-bug`) assert the correct behaviour for an
open bug in todo/ and expect today's failure; AGENTS.md explains how
they flip when the bug is fixed.

On every pull request, CI (`.github/workflows/ci.yml`) builds on **Linux** (Ubuntu 24.04, GCC)
and runs the unit and known-bug tests plus the two furniture tests (modal census, constancy
sweep), and cross-builds **Windows** with MinGW to run its unit tests under Wine. Pull requests
get no native Windows or macOS run yet ([CI tests Windows only under Wine, and most of CI guards process, not the product](todo/2026-10-15-ci-windows-only-under-wine.md)); the nightly workflow builds and tests all three.

## Releases

A release is cut from a pull request labelled `release-candidate`; [RELEASING.md](RELEASING.md)
is the runbook. Each release ships a Linux tarball, .deb and .rpm, a Windows zip and .msi, and
an Apple Silicon macOS DMG. The macOS app is unsigned and not notarized, because Trailer isn't in
the Apple Developer Program, so its first launch needs
`xattr -dr com.apple.quarantine /Applications/Trailer.app`. Nightly builds also update themselves
over an ed25519-signed feed (Help ▸ Check for Updates…); tagged releases don't yet ([The stable update channel isn't signed](todo/2026-10-15-stable-channel-unsigned.md)).

## Principles

Trailer is built around a few hard-edged constraints — no ads, no
telemetry, no accounts, no premium tier, no cloud sync, local-first.
[PRODUCT.md](PRODUCT.md) spells them out.

## Project docs

| Doc | What it's for |
|---|---|
| [PRODUCT.md](PRODUCT.md) | What Trailer is, who it's for, its priorities, and the rules that decide what it does. |
| [todo/](todo/README.md) | The one tracker: one file per open item, each with a pass/fail bar. |
| [AGENTS.md](AGENTS.md) | How to work on the code, for people and coding agents. `CLAUDE.md` imports it. |
| [CHANGELOG.md](CHANGELOG.md) | What shipped when, per release tag. Keep-a-Changelog format. |
| [RELEASING.md](RELEASING.md) | Maintainer release runbook: VERSION bump, tag, release notes, the update-signing key. |
| [docs/set-as-default-app.md](docs/set-as-default-app.md) | Per-OS steps to make Trailer the default PDF/image handler. |
| [icon/README.md](icon/README.md) | How the app icon is generated. |
| [src/uxrecord/README.md](src/uxrecord/README.md) | The developer-only UX session recorder. |
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
