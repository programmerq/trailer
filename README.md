# Trailer

Cross-platform PDF and image workbench. See [DESIGN.md](DESIGN.md) for the
full specification.

This repository is at **Phase 0 — Foundations**: a runnable skeleton with
window/tab/sidebar shell, settings and recent-files persistence, a command-
line open pipeline, and a stub document adapter. PDF/image rendering starts
in Phase 1.

## Requirements

- CMake 3.24+
- Qt 6.5+ (Core, Gui, Widgets, Test)
- A C++20 compiler (MSVC 2022, GCC 11+, or Clang 14+)

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

### Platform notes

**macOS.** Install Qt via the Qt online installer or Homebrew
(`brew install qt`). Point CMake at the Qt install with
`-DCMAKE_PREFIX_PATH=$(brew --prefix qt)` if it isn't auto-detected.

**Windows.** Use the Qt online installer; open a Developer Command Prompt
for VS 2022 and set `CMAKE_PREFIX_PATH` to your Qt install
(e.g. `C:\Qt\6.5.3\msvc2022_64`).

**Linux.** Install Qt via your distribution (`qt6-base-dev` on
Debian/Ubuntu) or the Qt online installer.

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
