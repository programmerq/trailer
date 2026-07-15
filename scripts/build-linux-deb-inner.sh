#!/usr/bin/env bash
# Inner build script — performs the actual DEB build.
#
# Runs in two contexts:
#   * inside the Docker container built by build-linux-deb.sh (legacy /src /output paths)
#   * directly on a Linux host, invoked by `build-linux-deb.sh --no-docker`
#
# All environment-specific paths are overridable via env vars so the same
# logic works in both. Defaults match the historical Docker layout so the
# containerised path keeps working unchanged.
#
#   SRC          repo root                (default: /src)
#   OUTPUT_DIR   where the .deb is written (default: /output)
#   QT_PREFIX    Qt install prefix        (default: /opt/Qt/6.8.0/gcc_64)
#   ORT_PREFIX   onnxruntime prefix       (optional; added to CMAKE_PREFIX_PATH if set)
#   BUILD_DIR    cmake build tree         (default: /tmp/build-linux)
#   STAGING      DESTDIR staging tree     (default: /tmp/deb-staging)
#
# Do not invoke directly unless you set the vars; use scripts/build-linux-deb.sh.

set -euo pipefail

SRC="${SRC:-/src}"
OUTPUT_DIR="${OUTPUT_DIR:-/output}"
BUILD_DIR="${BUILD_DIR:-/tmp/build-linux}"
STAGING="${STAGING:-/tmp/deb-staging}"
QT_PREFIX="${QT_PREFIX:-/opt/Qt/6.8.0/gcc_64}"
ORT_PREFIX="${ORT_PREFIX:-}"

PROJECT_VERSION=$(grep -oE '[0-9]+\.[0-9]+\.[0-9]+' "$SRC/VERSION" | head -1)
if [[ -z "$PROJECT_VERSION" ]]; then
    echo "ERROR: could not extract project version from $SRC/VERSION" >&2
    exit 1
fi
DEB_REVISION=1
PKG_VERSION="${PROJECT_VERSION}-${DEB_REVISION}"
DEB_ARCH=$(dpkg --print-architecture)
DEB_OUT="${OUTPUT_DIR}/trailer_${PKG_VERSION}_${DEB_ARCH}.deb"

# Assemble CMAKE_PREFIX_PATH: Qt plus, optionally, a prebuilt onnxruntime.
CMAKE_PREFIX="$QT_PREFIX"
if [[ -n "$ORT_PREFIX" ]]; then
    CMAKE_PREFIX="${CMAKE_PREFIX};${ORT_PREFIX}"
fi

echo "==> Configuring CMake (prefix: $CMAKE_PREFIX)"
# CMAKE_INSTALL_DOCDIR is forced to the lowercase package name so the
# bundled license texts land in share/doc/trailer (matching Debian's
# package-name convention) rather than the CMake default share/doc/Trailer.
cmake -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$CMAKE_PREFIX" \
    -DCMAKE_INSTALL_DOCDIR=share/doc/trailer \
    -S "$SRC"

echo "==> Building ($(nproc) jobs)"
cmake --build "$BUILD_DIR" -j"$(nproc)"

echo "==> Installing into staging tree"
rm -rf "$STAGING"
mkdir -p "$STAGING"
# cmake --install populates usr/bin/trailer, usr/share/applications,
# usr/share/metainfo, usr/share/icons, and the license texts under
# usr/share/doc/trailer/ via the GNUInstallDirs rules in CMakeLists.txt
DESTDIR="$STAGING" cmake --install "$BUILD_DIR"

echo "==> Copying DEBIAN control files"
mkdir -p "$STAGING/DEBIAN"
cp "$SRC/packaging/deb/DEBIAN/control"   "$STAGING/DEBIAN/"
cp "$SRC/packaging/deb/DEBIAN/copyright" "$STAGING/DEBIAN/"
cp "$SRC/packaging/deb/DEBIAN/postinst"  "$STAGING/DEBIAN/"
cp "$SRC/packaging/deb/DEBIAN/prerm"     "$STAGING/DEBIAN/"
chmod 755 "$STAGING/DEBIAN/postinst" "$STAGING/DEBIAN/prerm"

echo "==> Bundling Qt libs into /opt/trailer/lib/"
# Qt at this version is not in Ubuntu 22.04's repos; bundle the shared
# libs so the package is self-contained.

TRAILER_BIN="$STAGING/usr/bin/trailer"
BUNDLE_LIB="$STAGING/opt/trailer/lib"
mkdir -p "$BUNDLE_LIB"

# Copy non-system shared libraries (those not under /lib or /usr/lib)
# that a given binary transitively needs. Uses iterative BFS over the
# ldd dependency graph; $BUNDLE_LIB acts as the seen-set via -f checks.
bundle_libs() {
    local -a queue=("$1")
    while (( ${#queue[@]} )); do
        local current="${queue[0]}"
        queue=("${queue[@]:1}")
        local lib
        while IFS= read -r lib; do
            [[ -f "$lib" ]] || continue
            local name
            name="$(basename "$lib")"
            [[ -f "$BUNDLE_LIB/$name" ]] && continue
            cp "$lib" "$BUNDLE_LIB/$name"
            queue+=("$lib")
        done < <(ldd "$current" 2>/dev/null \
            | awk '/=>/ { print $3 }' \
            | grep -Ev '^(/lib|/usr/lib|not$)')
    done
}

bundle_libs "$TRAILER_BIN"

# libqxcb.so is loaded at runtime via QT_PLUGIN_PATH, not linked directly,
# so ldd on the main binary won't find it; add it explicitly.
QT_PLATFORM_PLUGIN="$QT_PREFIX/plugins/platforms/libqxcb.so"
if [[ -f "$QT_PLATFORM_PLUGIN" ]]; then
    mkdir -p "$STAGING/opt/trailer/plugins/platforms"
    cp "$QT_PLATFORM_PLUGIN" "$STAGING/opt/trailer/plugins/platforms/"
    bundle_libs "$QT_PLATFORM_PLUGIN"
fi

# Patch RPATH before moving the binary so the installed binary resolves
# bundled libs without requiring LD_LIBRARY_PATH.
patchelf --set-rpath '/opt/trailer/lib' "$TRAILER_BIN" 2>/dev/null || \
    echo "  (patchelf not available or failed — skipping RPATH patch)"

# Replace the installed binary with a thin wrapper that sets QT_PLUGIN_PATH
# so Qt discovers the bundled platform plugin at runtime.
REAL_BIN="$STAGING/opt/trailer/bin/trailer"
mkdir -p "$STAGING/opt/trailer/bin"
mv "$TRAILER_BIN" "$REAL_BIN"
cat > "$TRAILER_BIN" <<'WRAP'
#!/bin/sh
export QT_PLUGIN_PATH=/opt/trailer/plugins${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}
exec /opt/trailer/bin/trailer "$@"
WRAP
chmod 755 "$TRAILER_BIN"

echo "==> Fixing permissions"
find "$STAGING" -type d -exec chmod 755 {} +
find "$STAGING/usr" -type f -exec chmod 644 {} +
chmod 755 "$STAGING/usr/bin/trailer"
chmod 755 "$STAGING/opt/trailer/bin/trailer"
chmod 755 "$STAGING/DEBIAN/postinst" "$STAGING/DEBIAN/prerm"

echo "==> Building DEB package"
mkdir -p "$OUTPUT_DIR"
dpkg-deb --build "$STAGING" "$DEB_OUT"

echo "==> Done: $DEB_OUT"
dpkg --info "$DEB_OUT"
