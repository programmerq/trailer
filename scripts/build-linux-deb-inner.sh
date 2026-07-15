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
    -DCMAKE_INSTALL_PREFIX=/usr \
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

# Qt 6.11 (with qtpdf) is not in Ubuntu 22.04's repos; bundle the shared libs
# so the package is self-contained. Shared with the .rpm packager — see
# scripts/bundle-qt-runtime.sh. The onnxruntime libs live outside the Qt prefix,
# so pass both lib dirs as the ldd search path.
ORT_LIB_PATH="$QT_PREFIX/lib"
if [[ -n "$ORT_PREFIX" ]]; then
    ORT_LIB_PATH="${ORT_LIB_PATH}:${ORT_PREFIX}/lib"
fi
bash "$SRC/scripts/bundle-qt-runtime.sh" "$STAGING" "$QT_PREFIX" "$ORT_LIB_PATH"

echo "==> Copying license/copyright alongside the binary (Debian Policy §12.5)"
# DEBIAN/copyright (above) is the control-area copy; Policy also wants a
# machine-readable copyright at /usr/share/doc/<pkg>/copyright. cmake --install
# already staged LICENSE + third-party texts under usr/share/doc/trailer/.
install -Dm644 "$SRC/packaging/deb/DEBIAN/copyright" \
    "$STAGING/usr/share/doc/trailer/copyright"

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
