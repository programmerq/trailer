# macOS Packaging

## Prerequisites

- **Qt 6.8+** with the `qtpdf` module — install from [qt.io/download](https://www.qt.io/download)
- **CMake 3.24+** — `brew install cmake` or from [cmake.org](https://cmake.org/download/)
- **Xcode Command Line Tools** — `xcode-select --install`

## Building the DMG

```bash
bash scripts/build-macos.sh
```

The script auto-detects Qt under `~/Qt/6.*/macos`. To specify a different
installation, set `QTDIR` before running:

```bash
export QTDIR=~/Qt/6.8.0/macos
bash scripts/build-macos.sh
```

On success the DMG is written to `dist/Trailer-0.1.0-macOS.dmg`.

## TODO

- [ ] Add Apple Developer Team ID and signing identity to `scripts/build-macos.sh`
- [ ] Add `notarytool` call to `scripts/build-macos.sh` for notarization
- [ ] Integrate Sparkle framework for auto-updates (see DESIGN.md §12)
- [ ] Add macOS packaging step to CI once signing secrets are configured
