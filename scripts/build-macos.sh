#!/usr/bin/env bash
# Build Trailer.app for macOS — Apple Silicon (arm64), self-contained,
# packaged as a drag-to-Applications DMG.
#
# Mirrors .github/workflows/release.yml's macos-build job. The script
# is the source of truth; the workflow installs Qt + ninja and then
# invokes this. Running locally produces the same DMG the release
# pipeline does (modulo the toolchain pins).
#
# Why arm64-only and not a universal binary: ONNX Runtime stopped
# shipping macOS x86_64 / universal2 prebuilts (only the
# `onnxruntime-osx-arm64-X.Y.Z.tgz` tarball exists upstream as of
# 2026-05). Trailer's ML features (background removal, OCR, SAM)
# depend on ORT, so an x86_64 slice of the .app has nothing to link
# against. Intel-Mac users can build from source via this same
# script on their host (the cmake + qpdf + libjpeg paths all work
# for x86_64 hosts too).
#
# Usage:
#   scripts/build-macos.sh              # incremental: reuse qpdf deps
#   scripts/build-macos.sh --rebuild    # wipe build-macos/ + build-macos-deps/
#   scripts/build-macos.sh --skip-adaptive-icon
#                                       # dev build: skip the actool app-icon
#                                       # compile (static non-adaptive .icns icon), so a host
#                                       # with only the Command Line Tools (no
#                                       # full Xcode / no actool) still builds a
#                                       # DMG. Same as TRAILER_SKIP_ADAPTIVE_ICON=1.
#   make release                        # convenience wrapper (same thing)
#
# Output:
#   build-macos/Trailer.app                 arm64, self-contained
#   dist/trailer-macos-arm64.dmg            drag-to-Applications DMG
#
# Configurable via env vars:
#   QPDF_VERSION                qpdf release tag to build (default 12.3.2)
#   MACOSX_DEPLOYMENT_TARGET    minimum macOS version (default 14.0)
#   WERROR                      ON/OFF for -DTRAILER_WERROR (default OFF;
#                               flip ON to chase regressions locally)
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
LIBJPEG_VERSION="${LIBJPEG_VERSION:-3.0.3}"
MACOSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-14.0}"
# WERROR defaults OFF: AppleClang on Qt 6.11 + libc++ also surfaces
# system-header warnings (-Wdouble-promotion, -Wshorten-64-to-32) that
# CI's GCC on Qt 6.8 doesn't trip. Pass WERROR=ON to opt back in
# locally if you're chasing a specific regression.
WERROR="${WERROR:-OFF}"
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build-macos}"
DEPS_DIR="${DEPS_DIR:-$REPO_ROOT/build-macos-deps}"
DIST_DIR="${DIST_DIR:-$REPO_ROOT/dist}"

REBUILD=0
# Skip the adaptive light/dark app icon (actool). Enabled by either the
# --skip-adaptive-icon flag or TRAILER_SKIP_ADAPTIVE_ICON=1 in the env (so
# `TRAILER_SKIP_ADAPTIVE_ICON=1 make release-macos` works with no Makefile
# change). actool ships ONLY with full Xcode, so this lets a machine with just
# the Command Line Tools still produce a runnable app/DMG with the static
# (non-adaptive) .icns icon. Guarded with :- so it's safe under `set -u`.
SKIP_ADAPTIVE_ICON=0
if [[ "${TRAILER_SKIP_ADAPTIVE_ICON:-}" == "1" ]]; then
    SKIP_ADAPTIVE_ICON=1
fi
for ARG in "$@"; do
    case "$ARG" in
        --rebuild)
            REBUILD=1
            ;;
        --skip-adaptive-icon)
            SKIP_ADAPTIVE_ICON=1
            ;;
        *)
            echo "Unknown argument: $ARG" >&2
            echo "See the header of $0 for usage." >&2
            exit 2
            ;;
    esac
done

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
echo "Building Trailer $VERSION (macOS arm64)"

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
# Fail-fast preflight for the adaptive app icon.
#
# The icon is compiled by a POST_BUILD `xcrun actool` step in
# CMakeLists.txt. actool ships ONLY with FULL Xcode — the Command Line
# Tools alone do not carry it — and a stale `xcode-select` (e.g. after a
# macOS upgrade) can point at a developer dir that has no actool. Without
# this check the build runs for minutes and then either dies mid-build at
# `xcrun actool` or, worse, produces no Assets.car and only trips the late
# "Assets.car missing" verify with a misleading message. Detect it up front
# and hand the user actool's real fix (or the dev-build opt-out).
# ---------------------------------------------------------------------
if (( SKIP_ADAPTIVE_ICON )); then
    echo "==> Adaptive app icon: SKIPPED (--skip-adaptive-icon / TRAILER_SKIP_ADAPTIVE_ICON=1)"
elif ! xcrun --find actool >/dev/null 2>&1; then
    DEVELOPER_DIR_CUR="$(xcode-select -p 2>/dev/null || echo '<unknown>')"
    cat >&2 <<EOF
ERROR: actool not found. The adaptive app icon requires FULL Xcode (Command Line Tools alone do not ship actool).
  Current developer dir: $DEVELOPER_DIR_CUR
  Fix:  sudo xcode-select -s /Applications/Xcode.app/Contents/Developer
        sudo xcodebuild -license accept
        xcrun actool --version   # confirm
  Or, for a local dev build without the polished icon:  ./scripts/build-macos.sh --skip-adaptive-icon
EOF
    exit 1
fi

# ---------------------------------------------------------------------
# Build libjpeg-turbo as a static arm64 library.
#
# qpdf 12.x hard-requires libjpeg (no opt-out: missing libjpeg is a
# SEND_ERROR in libqpdf/CMakeLists.txt). Homebrew's jpeg-turbo is fine
# in terms of arch (matches the host arm64) but only ships a dylib,
# and we want a static lib so the resulting .app has no external
# libjpeg dependency at runtime. Build from source — same approach
# the Windows Dockerfile uses. Cached in $DEPS_DIR; --rebuild forces
# a clean build.
#
# SIMD stays disabled because libjpeg-turbo's nasm-based SIMD adds
# another toolchain dep without a meaningful win for the kind of
# JPEG work qpdf does (mostly preserving streams, occasional decode).
# ---------------------------------------------------------------------
JPEG_PREFIX="$DEPS_DIR/jpeg-prefix"
JPEG_CONFIG="$JPEG_PREFIX/lib/pkgconfig/libjpeg.pc"
if [[ ! -f "$JPEG_CONFIG" ]]; then
    echo "==> Building libjpeg-turbo $LIBJPEG_VERSION static-arm64"
    mkdir -p "$DEPS_DIR"
    if [[ ! -f "$DEPS_DIR/libjpeg-turbo-$LIBJPEG_VERSION.tar.gz" ]]; then
        curl -fsSL \
          "https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/${LIBJPEG_VERSION}/libjpeg-turbo-${LIBJPEG_VERSION}.tar.gz" \
          -o "$DEPS_DIR/libjpeg-turbo-$LIBJPEG_VERSION.tar.gz"
    fi
    rm -rf "$DEPS_DIR/libjpeg-turbo-$LIBJPEG_VERSION" "$DEPS_DIR/jpeg-build" "$JPEG_PREFIX"
    tar -xf "$DEPS_DIR/libjpeg-turbo-$LIBJPEG_VERSION.tar.gz" -C "$DEPS_DIR"
    cmake -S "$DEPS_DIR/libjpeg-turbo-$LIBJPEG_VERSION" -B "$DEPS_DIR/jpeg-build" -G Ninja \
        -DCMAKE_INSTALL_PREFIX="$JPEG_PREFIX" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_OSX_ARCHITECTURES="arm64" \
        -DCMAKE_OSX_DEPLOYMENT_TARGET="$MACOSX_DEPLOYMENT_TARGET" \
        -DENABLE_SHARED=OFF \
        -DENABLE_STATIC=ON \
        -DWITH_SIMD=OFF \
        -DWITH_TURBOJPEG=OFF
    cmake --build "$DEPS_DIR/jpeg-build" --parallel
    cmake --install "$DEPS_DIR/jpeg-build"
else
    echo "==> Reusing cached libjpeg-turbo install at $JPEG_PREFIX"
fi

# ---------------------------------------------------------------------
# Build qpdf as a static arm64 library.
#
# Homebrew qpdf is dylib-only (and we want a static lib so the
# resulting .app has no external qpdf dep at runtime). We build qpdf
# from source once and cache the install in $DEPS_DIR — subsequent
# runs skip this step. Pass --rebuild to force a clean qpdf build.
#
# qpdf discovers libjpeg via pkg_check_modules first, falling back to
# find_path/find_library. We point both at $DEPS_DIR/jpeg-prefix (the
# static lib built above) by:
#   - setting PKG_CONFIG_LIBDIR to ONLY that prefix (overriding the
#     runner's /opt/homebrew/lib/pkgconfig) so pkg_check_modules picks
#     our static libjpeg, and
#   - including the same prefix in CMAKE_PREFIX_PATH for the
#     find_path/find_library fallback.
# This is the same shape the Windows Dockerfile uses.
# ---------------------------------------------------------------------
QPDF_CONFIG="$DEPS_DIR/qpdf-prefix/lib/cmake/qpdf/qpdfConfig.cmake"
if [[ ! -f "$QPDF_CONFIG" ]]; then
    echo "==> Building qpdf $QPDF_VERSION static-arm64"
    mkdir -p "$DEPS_DIR"
    if [[ ! -f "$DEPS_DIR/qpdf-$QPDF_VERSION.tar.gz" ]]; then
        curl -fsSL \
          "https://github.com/qpdf/qpdf/releases/download/v${QPDF_VERSION}/qpdf-${QPDF_VERSION}.tar.gz" \
          -o "$DEPS_DIR/qpdf-$QPDF_VERSION.tar.gz"
    fi
    rm -rf "$DEPS_DIR/qpdf-$QPDF_VERSION" "$DEPS_DIR/qpdf-build"
    tar -xf "$DEPS_DIR/qpdf-$QPDF_VERSION.tar.gz" -C "$DEPS_DIR"
    # USE_IMPLICIT_CRYPTO=OFF + REQUIRE_CRYPTO_NATIVE=ON forces qpdf
    # to use its built-in (header-only) crypto and skip the openssl /
    # gnutls detection paths entirely. Just disabling find_package()
    # for those isn't enough — qpdf uses raw pkg_check_modules and
    # find_library calls, which would otherwise locate Homebrew's
    # arm64-only crypto dylibs and break the x86_64 link.
    #
    # PKG_CONFIG_LIBDIR (not PATH) replaces pkg-config's entire search
    # space with our jpeg-prefix, so pkg_check_modules(libjpeg) picks
    # our static libjpeg.a and pkg_check_modules(openssl /
    # gnutls) returns FALSE.
    #
    # BUILD_TESTING=OFF skips ~150 qpdf test binaries.
    PKG_CONFIG_LIBDIR="$DEPS_DIR/jpeg-prefix/lib/pkgconfig" \
    cmake -S "$DEPS_DIR/qpdf-$QPDF_VERSION" -B "$DEPS_DIR/qpdf-build" -G Ninja \
        -DCMAKE_INSTALL_PREFIX="$DEPS_DIR/qpdf-prefix" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_OSX_ARCHITECTURES="arm64" \
        -DCMAKE_OSX_DEPLOYMENT_TARGET="$MACOSX_DEPLOYMENT_TARGET" \
        -DCMAKE_PREFIX_PATH="$DEPS_DIR/jpeg-prefix" \
        -DBUILD_STATIC_LIBS=ON \
        -DBUILD_SHARED_LIBS=OFF \
        -DBUILD_TESTING=OFF \
        -DUSE_IMPLICIT_CRYPTO=OFF \
        -DREQUIRE_CRYPTO_NATIVE=ON \
        -DREQUIRE_CRYPTO_OPENSSL=OFF \
        -DREQUIRE_CRYPTO_GNUTLS=OFF \
        -DBUILD_DOC=0
    cmake --build "$DEPS_DIR/qpdf-build" --parallel
    cmake --install "$DEPS_DIR/qpdf-build"
    # qpdf's pkg_check_modules-based discovery records the bare lib
    # name `jpeg` (not an absolute path) in its exported
    # INTERFACE_LINK_LIBRARIES — so downstream consumers get `-ljpeg`
    # without a corresponding `-L<jpeg-prefix>/lib` flag and the link
    # fails. Rewrite the bare name to the absolute path so trailer's
    # link finds our libjpeg.a without any extra dance.
    # (Compare libz, which qpdf finds via find_package(ZLIB) and
    # already exports as an absolute /.../libz.tbd path.)
    QPDF_TARGETS="$DEPS_DIR/qpdf-prefix/lib/cmake/qpdf/libqpdfTargets.cmake"
    sed -i.bak "s|;jpeg\"|;$JPEG_PREFIX/lib/libjpeg.a\"|g" "$QPDF_TARGETS"
    rm -f "$QPDF_TARGETS.bak"
    # Post-condition: if qpdf's export format ever changes (different
    # quoting, an extra token, etc.) the sed silently no-ops and the
    # downstream trailer link fails later with a cryptic "library
    # 'jpeg' not found". Catch the drift here instead.
    if ! grep -q "$JPEG_PREFIX/lib/libjpeg.a" "$QPDF_TARGETS"; then
        echo "ERROR: failed to rewrite libqpdfTargets.cmake's jpeg reference." >&2
        echo "       qpdf's exported INTERFACE_LINK_LIBRARIES format may have" >&2
        echo "       changed; inspect $QPDF_TARGETS and update the sed pattern." >&2
        exit 1
    fi
else
    echo "==> Reusing cached qpdf install at $DEPS_DIR/qpdf-prefix"
    echo "    (run with --rebuild to force a clean qpdf build)"
fi

# ---------------------------------------------------------------------
# Build trailer.
# ---------------------------------------------------------------------
echo "==> Configuring trailer"
rm -rf "$BUILD_DIR"
# When skipping the adaptive icon, turn the actool POST_BUILD step off at
# the CMake level so the build never invokes actool (the app gets the
# static (non-adaptive) .icns icon). Otherwise leave the option at its ON default.
ADAPTIVE_ICON_FLAG="-DTRAILER_ADAPTIVE_ICON=ON"
if (( SKIP_ADAPTIVE_ICON )); then
    ADAPTIVE_ICON_FLAG="-DTRAILER_ADAPTIVE_ICON=OFF"
fi
# CMAKE_PREFIX_PATH must include qpdf-prefix (for find_package(qpdf
# CONFIG)) and jpeg-prefix (so the linker can resolve libjpeg, which
# qpdf's patched INTERFACE_LINK_LIBRARIES points at as an absolute
# path).
cmake -S . -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="arm64" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$MACOSX_DEPLOYMENT_TARGET" \
    -DCMAKE_PREFIX_PATH="$QT_ROOT_DIR;$DEPS_DIR/qpdf-prefix;$DEPS_DIR/jpeg-prefix" \
    -DTRAILER_WERROR="$WERROR" \
    "$ADAPTIVE_ICON_FLAG"

echo "==> Building Trailer.app"
cmake --build "$BUILD_DIR" --parallel

# Resolve the .app path. CMake builds an Application Bundle whose
# directory name follows the target name (`trailer`) by default, but
# MACOSX_BUNDLE_BUNDLE_NAME ("Trailer") can flip the display name. Use
# whichever directory CMake actually produced so this script doesn't
# need to track that detail.
if   [[ -d "$BUILD_DIR/Trailer.app" ]]; then APP_PATH="$BUILD_DIR/Trailer.app"
elif [[ -d "$BUILD_DIR/trailer.app" ]]; then APP_PATH="$BUILD_DIR/trailer.app"
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
# Adhoc-sign the bundle.
#
# Some macdeployqt versions (notably Qt 6.11+) adhoc-sign the whole
# bundle automatically; Qt 6.8's macdeployqt does not, leaving the
# bundle in a half-signed state — the linker's automatic adhoc
# signature covers the executable, but the bundle's Info.plist isn't
# bound and Resources/ aren't sealed, and the signature identifier
# stays as the binary name ("trailer") instead of the CFBundleIdentifier
# ("org.trailer.Trailer"). macOS treats that as a non-standard
# bundle and behaves oddly with the Dock icon and Gatekeeper.
#
# Explicit `codesign --deep --force --sign -` produces a proper adhoc
# signature on both Qt versions. No identity required (no `--timestamp`
# either, since adhoc signatures don't have a trust chain to anchor).
# Notarization is still deferred — when wired up, swap `-` for a
# Developer ID Application identity and add `--options runtime`.
# ---------------------------------------------------------------------
echo "==> Adhoc-signing the bundle"
codesign --deep --force --sign - "$APP_PATH"
codesign --verify --verbose "$APP_PATH" 2>&1
codesign -dv "$APP_PATH" 2>&1 | grep -E '^(Identifier|Sealed Resources|Info\.plist|Format|Signature)'

# ---------------------------------------------------------------------
# Verify: arm64 present, no external dylib refs leaking out of the
# bundle. These guard against a misconfigured runner image or a silent
# CMake flag drift shipping a wrong-arch or Homebrew-dependent binary.
#
# Derive the executable name from Info.plist's CFBundleExecutable so
# this script doesn't need to track CMakeLists.txt's target/OUTPUT_NAME
# choice (`Trailer` on macOS, `trailer` everywhere else).
# ---------------------------------------------------------------------
EXEC_NAME=$(/usr/libexec/PlistBuddy -c "Print :CFBundleExecutable" "$APP_PATH/Contents/Info.plist")
TRAILER_BIN="$APP_PATH/Contents/MacOS/$EXEC_NAME"
if [[ ! -x "$TRAILER_BIN" ]]; then
    echo "ERROR: executable not found at $TRAILER_BIN" >&2
    echo "       Info.plist CFBundleExecutable=$EXEC_NAME but no matching file." >&2
    exit 1
fi
echo "==> Verifying arch"
ARCHES=$(lipo -archs "$TRAILER_BIN")
echo "    arches: $ARCHES"
echo "$ARCHES" | grep -q arm64 || { echo "ERROR: missing arm64 slice" >&2; exit 1; }

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
# Verify the adaptive app icon landed (ADR 0009 Option A). Cheap
# build-integration check that runs only here / on the macos-14 runner:
#   - Contents/Resources/Assets.car exists (actool's POST_BUILD step in
#     CMakeLists.txt produced it during `cmake --build`),
#   - Info.plist declares CFBundleIconName=AppIcon (the key that makes
#     macOS read the catalog), and
#   - assetutil --info confirms the compiled catalog carries an AppIcon.
# The visual light/dark Dock swap is a separate real-Mac verification
# (the surface is native Dock/Finder chrome; `grab()` can't see it).
# ---------------------------------------------------------------------
if (( SKIP_ADAPTIVE_ICON )); then
    echo "⚠️  Adaptive app icon skipped (--skip-adaptive-icon / dev build) — DMG will ship the static (non-adaptive) .icns icon."
else
    echo "==> Verifying adaptive app icon (Assets.car + CFBundleIconName)"
    ASSETS_CAR="$APP_PATH/Contents/Resources/Assets.car"
    if [[ ! -f "$ASSETS_CAR" ]]; then
        echo "ERROR: $ASSETS_CAR missing — actool ran but did not emit Assets.car." >&2
        echo "       (The actool POST_BUILD step needs FULL Xcode and the" >&2
        echo "       --output-partial-info-plist arg, which is already wired in" >&2
        echo "       the APPLE-guarded command in CMakeLists.txt.) Ensure full" >&2
        echo "       Xcode is selected and re-run:" >&2
        echo "         sudo xcode-select -s /Applications/Xcode.app/Contents/Developer" >&2
        echo "         xcrun actool --version   # confirm" >&2
        echo "       Or, for a local dev build without the polished icon:" >&2
        echo "         ./scripts/build-macos.sh --skip-adaptive-icon" >&2
        exit 1
    fi
    ICON_NAME=$(/usr/libexec/PlistBuddy -c "Print :CFBundleIconName" \
                "$APP_PATH/Contents/Info.plist" 2>/dev/null || true)
    if [[ "$ICON_NAME" != "AppIcon" ]]; then
        echo "ERROR: Info.plist CFBundleIconName is '$ICON_NAME', expected 'AppIcon'." >&2
        exit 1
    fi
    if ! assetutil --info "$ASSETS_CAR" | grep -qi "AppIcon"; then
        echo "ERROR: assetutil --info does not mention AppIcon in $ASSETS_CAR." >&2
        exit 1
    fi
    echo "    Assets.car present; CFBundleIconName=$ICON_NAME; assetutil confirms AppIcon"
fi

# ---------------------------------------------------------------------
# Package as DMG.
#
# Filename is fixed (trailer-macos-arm64.dmg) so CI's
# upload-artifact step has a stable target. The bundle version is
# already baked into the .app via TrailerVersion.h + CMake's
# MACOSX_BUNDLE_BUNDLE_VERSION, so the version is visible inside the
# DMG even though the filename doesn't carry it.
# ---------------------------------------------------------------------
mkdir -p "$DIST_DIR"
DMG_PATH="$DIST_DIR/trailer-macos-arm64.dmg"
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
