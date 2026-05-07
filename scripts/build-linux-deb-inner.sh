#!/usr/bin/env bash
# Inner build script — runs inside the Docker container built by build-linux-deb.sh.
# Do not invoke directly; use scripts/build-linux-deb.sh instead.

set -euo pipefail

SRC=/src
BUILD_DIR=/tmp/build-linux
STAGING=/tmp/deb-staging
QT_PREFIX=/opt/Qt/6.8.0/gcc_64
PROJECT_VERSION=$(grep -E '^\s*VERSION [0-9]+\.[0-9]+\.[0-9]+' "$SRC/CMakeLists.txt" \
    | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)
if [[ -z "$PROJECT_VERSION" ]]; then
    echo "ERROR: could not extract project version from $SRC/CMakeLists.txt" >&2
    exit 1
fi
DEB_REVISION=1
PKG_VERSION="${PROJECT_VERSION}-${DEB_REVISION}"
DEB_ARCH=$(dpkg --print-architecture)
DEB_OUT="/output/trailer_${PKG_VERSION}_${DEB_ARCH}.deb"

echo "==> Configuring CMake"
# CMAKE_INSTALL_PREFIX must be /usr so that GNUInstallDirs places
# the binary at $STAGING/usr/bin/trailer (matching TRAILER_BIN
# below) and the desktop / metainfo / icon files under
# $STAGING/usr/share/. Without this, CMake defaults to /usr/local
# on Unix and every later mv/patchelf/ldd against TRAILER_BIN
# fails under `set -e`. Caught by Cursor in PR #4 review.
cmake -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DCMAKE_PREFIX_PATH="$QT_PREFIX" \
    -S "$SRC"

echo "==> Building ($(nproc) jobs)"
cmake --build "$BUILD_DIR" -j"$(nproc)"

echo "==> Installing into staging tree"
rm -rf "$STAGING"
mkdir -p "$STAGING"
# cmake --install populates usr/bin/trailer, usr/share/applications,
# usr/share/metainfo, and usr/share/icons via the GNUInstallDirs rules in CMakeLists.txt
DESTDIR="$STAGING" cmake --install "$BUILD_DIR"

echo "==> Copying DEBIAN control files"
mkdir -p "$STAGING/DEBIAN"
cp "$SRC/packaging/deb/DEBIAN/control"   "$STAGING/DEBIAN/"
cp "$SRC/packaging/deb/DEBIAN/copyright" "$STAGING/DEBIAN/"
cp "$SRC/packaging/deb/DEBIAN/postinst"  "$STAGING/DEBIAN/"
cp "$SRC/packaging/deb/DEBIAN/prerm"     "$STAGING/DEBIAN/"
chmod 755 "$STAGING/DEBIAN/postinst" "$STAGING/DEBIAN/prerm"

echo "==> Bundling Qt libs into /opt/trailer/lib/"
# Qt 6.8.0 is not in Ubuntu 22.04's repos; bundle the shared libs so
# the package is self-contained.

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
dpkg-deb --build "$STAGING" "$DEB_OUT"

echo "==> Done: $DEB_OUT"
dpkg --info "$DEB_OUT"
