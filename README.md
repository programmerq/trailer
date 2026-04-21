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

### Platform notes

**macOS.** Install Qt via the Qt online installer or Homebrew
(`brew install qt`). Install qpdf via `brew install qpdf`. Point CMake at
the Qt install with `-DCMAKE_PREFIX_PATH=$(brew --prefix qt)` if it isn't
auto-detected.

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
aqt install-qt windows desktop 6.5.3 win64_msvc2022_64 -m qtpdf
```

Then install only qpdf through vcpkg (step 3 shrinks to
`vcpkg install qpdf:x64-windows`) and add `CMAKE_PREFIX_PATH` to the
configure command:

```bat
cmake -S . -B build ^
    -DCMAKE_PREFIX_PATH=C:\Qt\6.5.3\msvc2022_64 ^
    -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake ^
    -DCMAKE_BUILD_TYPE=Release
```

**Linux.** Install Qt via your distribution (`qt6-base-dev` on
Debian/Ubuntu) or the Qt online installer. Install qpdf via
`sudo apt-get install libqpdf-dev` (or equivalent).

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

CI runs the same matrix (Ubuntu / Windows / macOS) on every push and PR.

## License

MIT. See [LICENSE](LICENSE).
