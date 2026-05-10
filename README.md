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

Continuous integration passes `-DTRAILER_WERROR=ON` so compiler warnings
fail the build (warnings-as-errors). Use the same flag locally when you
want parity with CI:

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
request (`.github/workflows/ci.yml`). The Windows binary is
cross-compiled from Linux via the mingw-w64 toolchain in
`docker/windows/Dockerfile`; that, plus the UAT suite, runs only
on tag pushes (`.github/workflows/release.yml`). macOS builds are
manual on a Mac developer machine — cross-compiling there from
Linux is encumbered by SDK licensing and code-signing constraints,
so it stays out of CI by design.

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
