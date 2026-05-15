#!/usr/bin/env bash
# Build trailer.app for macOS — universal, self-contained, packaged as
# a drag-to-Applications DMG.
#
# Mirrors .github/workflows/release.yml's macos-build job. The script
# is the source of truth; the workflow installs Qt + ninja and then
# invokes this. Running locally produces the same DMG the release
# pipeline does (modulo the toolchain pins).
#
# Usage:
#   scripts/build-macos.sh              # incremental: reuse qpdf deps
#   scripts/build-macos.sh --rebuild    # wipe build-macos/ + build-macos-deps/
#   make release                        # convenience wrapper (same thing)
#
# Output:
#   build-macos/trailer.app                 universal, self-contained
#   dist/trailer-macos-universal.dmg        drag-to-Applications DMG
#
# Configurable via env vars:
#   QPDF_VERSION                qpdf release tag to build (default 12.3.2)
#   MACOSX_DEPLOYMENT_TARGET    minimum macOS version (default 11.0)
#   WERROR                      ON/OFF for -DTRAILER_WERROR (default ON)
#   QT_ROOT_DIR / QTDIR         path to Qt install (auto-detects from
#                               install-qt-action's $QT_ROOT_DIR, then
#                               ~/Qt/6.*/macos, then `brew --prefix qt`)
#   BUILD_DIR                   trailer build dir (default: build-macos)
#   DEPS_DIR                    qpdf build/install dir (default: build-macos-deps)
#   DIST_DIR                    final DMG output dir (default: dist)
#
# Code signing / notarization is intentionally deferred for 0.1.x —
# see release body for the Gatekeeper bypass users run once.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

if [[ "$(uname)" != "Darwin" ]]; then
    echo "scripts/build-macos.sh only runs on macOS hosts." >&2
    echo "Linux→macOS cross-compile is intentionally not supported — see" >&2
    echo ".github/workflows/release.yml's header for the SDK-licensing rationale." >&2
    exit 1
fi

QPDF_VERSION="${QPDF_VERSION:-12.3.2}"
MACOSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-11.0}"
WERROR="${WERROR:-ON}"
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build-macos}"
DEPS_DIR="${DEPS_DIR:-$REPO_ROOT/build-macos-deps}"
DIST_DIR="${DIST_DIR:-$REPO_ROOT/dist}"

REBUILD=0
case "${1:-}" in
    --rebuild)
        REBUILD=1
        ;;
    "")
        ;;
    *)
        echo "Unknown argument: $1" >&2
        echo "See the header of $0 for usage." >&2
        exit 2
        ;;
esac

if (( REBUILD )); then
    rm -rf "$BUILD_DIR" "$DEPS_DIR"
fi

# ---------------------------------------------------------------------
# Read VERSION (single source of truth, populated by CMakeLists.txt
# at configure time too). Used for the DMG's CFBundle version display
# title; the bundle internals get the same string baked in via the
# generated TrailerVersion.h.
# ---------------------------------------------------------------------
if [[ ! -f "$REPO_ROOT/VERSION" ]]; then
    echo "VERSION file missing at $REPO_ROOT/VERSION" >&2
    exit 1
fi
VERSION="$(tr -d '[:space:]' < "$REPO_ROOT/VERSION")"
echo "Building Trailer $VERSION (macOS universal)"

# ---------------------------------------------------------------------
# Resolve Qt.
#
# In CI, install-qt-action exports $QT_ROOT_DIR pointing at the install
# (e.g. .../Qt/6.8.0/macos). Locally, the maintainer convention is
# ~/Qt/6.*/macos (where Qt's online installer drops things). $QTDIR is
# accepted as a legacy alias for $QT_ROOT_DIR.
# ---------------------------------------------------------------------
if [[ -z "${QT_ROOT_DIR:-}" && -n "${QTDIR:-}" ]]; then
    QT_ROOT_DIR="$QTDIR"
fi
if [[ -z "${QT_ROOT_DIR:-}" ]]; then
    # Search common installation locations.
    for CANDIDATE in "$HOME"/Qt/6.*/macos; do
        if [[ -x "$CANDIDATE/bin/macdeployqt" ]]; then
            QT_ROOT_DIR="$CANDIDATE"
            break
        fi
    done
fi
if [[ -z "${QT_ROOT_DIR:-}" ]] && command -v brew >/dev/null 2>&1; then
    if BREW_QT="$(brew --prefix qt 2>/dev/null)" && [[ -x "$BREW_QT/bin/macdeployqt" ]]; then
        QT_ROOT_DIR="$BREW_QT"
    fi
fi
if [[ -z "${QT_ROOT_DIR:-}" || ! -x "$QT_ROOT_DIR/bin/macdeployqt" ]]; then
    cat >&2 <<EOF
Qt 6 not found. macdeployqt is required to bundle Qt frameworks into
the .app. Either:
  - install Qt via the Qt installer (drops it at ~/Qt/6.x.y/macos), or
  - \`brew install qt\`, or
  - set QT_ROOT_DIR to your Qt install root (dir containing bin/macdeployqt).
EOF
    exit 1
fi
echo "Using Qt at: $QT_ROOT_DIR"

for TOOL in cmake ninja hdiutil curl tar lipo otool; do
    if ! command -v "$TOOL" >/dev/null 2>&1; then
        echo "Required tool '$TOOL' not found." >&2
        case "$TOOL" in
            cmake)   echo "  Install: brew install cmake (or https://cmake.org/download/)" >&2 ;;
            ninja)   echo "  Install: brew install ninja" >&2 ;;
            hdiutil|lipo|otool)
                     echo "  Should ship with macOS / Xcode Command Line Tools (xcode-select --install)." >&2 ;;
        esac
        exit 1
    fi
done

# ---------------------------------------------------------------------
# Build qpdf as a static universal library.
#
# Homebrew qpdf is single-arch (matches the host) and dylib-only, so it
# can't feed a universal, self-contained .app. We build qpdf from source
# once and cache the install in $DEPS_DIR — subsequent runs skip this
# step. Pass --rebuild to force a clean qpdf build.
# ---------------------------------------------------------------------
QPDF_CONFIG="$DEPS_DIR/qpdf-prefix/lib/cmake/qpdf/qpdfConfig.cmake"
if [[ ! -f "$QPDF_CONFIG" ]]; then
    echo "==> Building qpdf $QPDF_VERSION static-universal"
    mkdir -p "$DEPS_DIR"
    if [[ ! -f "$DEPS_DIR/qpdf-$QPDF_VERSION.tar.gz" ]]; then
        curl -fsSL \
          "https://github.com/qpdf/qpdf/releases/download/v${QPDF_VERSION}/qpdf-${QPDF_VERSION}.tar.gz" \
          -o "$DEPS_DIR/qpdf-$QPDF_VERSION.tar.gz"
    fi
    rm -rf "$DEPS_DIR/qpdf-$QPDF_VERSION" "$DEPS_DIR/qpdf-build"
    tar -xf "$DEPS_DIR/qpdf-$QPDF_VERSION.tar.gz" -C "$DEPS_DIR"
    cmake -S "$DEPS_DIR/qpdf-$QPDF_VERSION" -B "$DEPS_DIR/qpdf-build" -G Ninja \
        -DCMAKE_INSTALL_PREFIX="$DEPS_DIR/qpdf-prefix" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
        -DCMAKE_OSX_DEPLOYMENT_TARGET="$MACOSX_DEPLOYMENT_TARGET" \
        -DBUILD_STATIC_LIBS=ON \
        -DBUILD_SHARED_LIBS=OFF \
        -DREQUIRE_CRYPTO_OPENSSL=0 \
        -DREQUIRE_CRYPTO_GNUTLS=0 \
        -DUSE_IMPLICIT_CRYPTO=1 \
        -DREQUIRE_LIBJPEG=0 \
        -DBUILD_DOC=0
    cmake --build "$DEPS_DIR/qpdf-build" --parallel
    cmake --install "$DEPS_DIR/qpdf-build"
else
    echo "==> Reusing cached qpdf install at $DEPS_DIR/qpdf-prefix"
    echo "    (run with --rebuild to force a clean qpdf build)"
fi

# ---------------------------------------------------------------------
# Build trailer.
# ---------------------------------------------------------------------
echo "==> Configuring trailer"
rm -rf "$BUILD_DIR"
cmake -S . -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$MACOSX_DEPLOYMENT_TARGET" \
    -DCMAKE_PREFIX_PATH="$QT_ROOT_DIR;$DEPS_DIR/qpdf-prefix" \
    -DTRAILER_WERROR="$WERROR"

echo "==> Building trailer.app"
cmake --build "$BUILD_DIR" --parallel

# Resolve the .app path. CMake builds an Application Bundle whose
# directory name follows the target name (`trailer`) by default, but
# MACOSX_BUNDLE_BUNDLE_NAME ("Trailer") can flip the display name. Use
# whichever directory CMake actually produced so this script doesn't
# need to track that detail.
if   [[ -d "$BUILD_DIR/trailer.app" ]]; then APP_PATH="$BUILD_DIR/trailer.app"
elif [[ -d "$BUILD_DIR/Trailer.app" ]]; then APP_PATH="$BUILD_DIR/Trailer.app"
else
    echo "ERROR: no .app bundle found under $BUILD_DIR" >&2
    exit 1
fi
echo "    built: $APP_PATH"

# ---------------------------------------------------------------------
# Bundle Qt frameworks via macdeployqt.
# Combined with the static-linked qpdf above, this produces a
# drag-to-Applications .app with no external Homebrew / Qt dependency.
# ---------------------------------------------------------------------
echo "==> Bundling Qt frameworks via macdeployqt"
"$QT_ROOT_DIR/bin/macdeployqt" "$APP_PATH" -verbose=1 -always-overwrite

# ---------------------------------------------------------------------
# Verify: both arches present, no external dylib refs leaking out of
# the bundle. These guard against a misconfigured runner image or a
# silent CMake flag drift shipping a single-arch or
# Homebrew-dependent binary under the "universal" filename.
# ---------------------------------------------------------------------
TRAILER_BIN="$APP_PATH/Contents/MacOS/trailer"
echo "==> Verifying universal arches"
ARCHES=$(lipo -archs "$TRAILER_BIN")
echo "    arches: $ARCHES"
echo "$ARCHES" | grep -q arm64  || { echo "ERROR: missing arm64 slice" >&2;  exit 1; }
echo "$ARCHES" | grep -q x86_64 || { echo "ERROR: missing x86_64 slice" >&2; exit 1; }

echo "==> Verifying no external dylib references"
EXT=$(otool -L "$TRAILER_BIN" \
      | awk 'NR>1 {print $1}' \
      | grep -vE '^(/usr/lib/|/System/|@executable_path|@rpath|@loader_path)' \
      || true)
if [[ -n "$EXT" ]]; then
    echo "ERROR: external dylib references inside the .app:" >&2
    echo "$EXT" >&2
    exit 1
fi
echo "    no external dylib references"

# ---------------------------------------------------------------------
# Package as DMG.
#
# Filename is fixed (trailer-macos-universal.dmg) so CI's
# upload-artifact step has a stable target. The bundle version is
# already baked into the .app via TrailerVersion.h + CMake's
# MACOSX_BUNDLE_BUNDLE_VERSION, so the version is visible inside the
# DMG even though the filename doesn't carry it.
# ---------------------------------------------------------------------
mkdir -p "$DIST_DIR"
DMG_PATH="$DIST_DIR/trailer-macos-universal.dmg"
rm -f "$DMG_PATH"

STAGING_DIR="$(mktemp -d)"
trap 'rm -rf "$STAGING_DIR"' EXIT
# Stage the .app + an /Applications symlink so the mounted DMG opens
# into a drag-target Finder window. UDZO = compressed, read-only.
cp -R "$APP_PATH" "$STAGING_DIR/"
ln -s /Applications "$STAGING_DIR/Applications"

echo "==> Creating DMG"
hdiutil create \
    -volname "Trailer $VERSION" \
    -srcfolder "$STAGING_DIR" \
    -ov \
    -format UDZO \
    -fs HFS+ \
    "$DMG_PATH" >/dev/null

echo
echo "Built:   $APP_PATH"
echo "Bundled: $DMG_PATH ($(du -h "$DMG_PATH" | awk '{print $1}'))"
echo
echo "Smoke-test the DMG:"
echo "  open '$DMG_PATH'"
