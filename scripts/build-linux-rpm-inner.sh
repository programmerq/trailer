#!/usr/bin/env bash
# Inner build script — performs the actual RPM build.
#
# Runs in two contexts:
#   * inside the trailer-linux-rpm-build Docker container (fedora:40)
#   * directly on a Linux host, invoked by `build-linux-rpm.sh --no-docker`
#     (rpmbuild works fine on Ubuntu via the `rpm` apt package)
#
# Strategy: cmake configure + build first, then call rpmbuild -bb with the
# build directory already populated so the spec's %build section is a no-op
# and %install can just run cmake --install.
#
# Environment-specific paths are overridable via env vars (defaults match the
# historical Docker layout):
#   SRC          repo root                (default: /src)
#   OUTPUT_DIR   where the .rpm is copied  (default: /output)
#   QT_PREFIX    Qt install prefix        (default: /opt/Qt/6.8.0/gcc_64)
#   ORT_PREFIX   onnxruntime prefix       (optional; added to CMAKE_PREFIX_PATH)
#   RPMBUILD_TOP rpmbuild topdir          (default: /root/rpmbuild)

set -euo pipefail

SRC="${SRC:-/src}"
OUTPUT_DIR="${OUTPUT_DIR:-/output}"
QT_PREFIX="${QT_PREFIX:-/opt/Qt/6.8.0/gcc_64}"
ORT_PREFIX="${ORT_PREFIX:-}"
RPMBUILD_TOP="${RPMBUILD_TOP:-$HOME/rpmbuild}"

BUILD_DIR="$RPMBUILD_TOP/BUILD/build-trailer"
SOURCE_DIR="$RPMBUILD_TOP/BUILD/trailer-source"

PROJECT_VERSION=$(grep -oE '[0-9]+\.[0-9]+\.[0-9]+' "$SRC/VERSION" | head -1)
if [[ -z "$PROJECT_VERSION" ]]; then
    echo "ERROR: could not extract project version from $SRC/VERSION" >&2
    exit 1
fi

# Assemble CMAKE_PREFIX_PATH: Qt plus, optionally, a prebuilt onnxruntime.
CMAKE_PREFIX="$QT_PREFIX"
if [[ -n "$ORT_PREFIX" ]]; then
    CMAKE_PREFIX="${CMAKE_PREFIX};${ORT_PREFIX}"
fi

# rpmdev-setuptree may not exist on non-Fedora hosts; create the tree by hand.
if command -v rpmdev-setuptree >/dev/null 2>&1; then
    HOME="$(dirname "$RPMBUILD_TOP")" rpmdev-setuptree
fi
mkdir -p "$RPMBUILD_TOP"/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS}

# Copy source into rpmbuild's BUILD area so cmake artifacts don't pollute the
# working tree (which may be a bind-mounted /src under Docker).
echo "Copying source to rpmbuild BUILD area..."
rm -rf "$SOURCE_DIR"
cp -r "$SRC" "$SOURCE_DIR"

echo "Configuring (prefix: $CMAKE_PREFIX)..."
# CMAKE_INSTALL_DOCDIR is forced lowercase so the bundled license texts land in
# share/doc/trailer (matching the rpm package name) rather than share/doc/Trailer.
cmake -B "$BUILD_DIR" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH="$CMAKE_PREFIX" \
      -DCMAKE_INSTALL_DOCDIR=share/doc/trailer \
      -S "$SOURCE_DIR"

echo "Building..."
cmake --build "$BUILD_DIR" -j"$(nproc)"

cp "$SOURCE_DIR/packaging/rpm/trailer.spec" "$RPMBUILD_TOP/SPECS/trailer.spec"

# Reconcile the spec's Version with the VERSION file (single source of truth).
# The tracked spec keeps a literal 0.3.0 as a sane default; patch the copy in the
# rpmbuild tree so a future VERSION bump can't leave stale internal metadata.
sed -i "s/^Version:.*/Version:        ${PROJECT_VERSION}/" \
    "$RPMBUILD_TOP/SPECS/trailer.spec"

# Qt lives outside the standard lib dirs; onnxruntime lives under its own prefix.
# The bundler (invoked from %install) needs the Qt prefix for the xcb plugin and
# both lib dirs on LD_LIBRARY_PATH so ldd can resolve the dependency graph.
LIB_SEARCH_PATH="$QT_PREFIX/lib"
if [[ -n "$ORT_PREFIX" ]]; then
    LIB_SEARCH_PATH="${LIB_SEARCH_PATH}:${ORT_PREFIX}/lib"
fi

# The bundled binary carries an RPATH of /opt/trailer/lib. Fedora's
# check-rpaths brp script treats non-standard absolute rpaths as build-fatal;
# QA_RPATHS downgrades that to a warning (self-contained /opt bundle is intended).
export QA_RPATHS=$(( 0x0001 | 0x0002 | 0x0004 | 0x0008 | 0x0010 ))

# --define _builddir so the spec's %install can reference the pre-built artifacts.
# --nodeps: the cmake configure+build already ran above, so %build is a no-op and
# the spec's Fedora-named BuildRequires (cmake, gcc-c++, qpdf-devel, …) are only
# advisory here. They aren't resolvable via rpm's DB on a Debian/Ubuntu host (or
# any host where the toolchain came from apt / a Qt prefix), so skip the check.
echo "Packaging RPM..."
rpmbuild -bb \
    --nodeps \
    --define "_topdir $RPMBUILD_TOP" \
    --define "_builddir $RPMBUILD_TOP/BUILD" \
    --define "qt_prefix $QT_PREFIX" \
    --define "lib_search_path $LIB_SEARCH_PATH" \
    "$RPMBUILD_TOP/SPECS/trailer.spec"

RPM_FILE=$(find "$RPMBUILD_TOP/RPMS" -name 'trailer-*.rpm' | head -1)
if [[ -z "$RPM_FILE" ]]; then
    echo "ERROR: No RPM found under $RPMBUILD_TOP/RPMS" >&2
    exit 1
fi
ARCH=$(rpm --eval '%{_arch}')
RPM_OUT="${OUTPUT_DIR}/trailer-${PROJECT_VERSION}-1.${ARCH}.rpm"
mkdir -p "$OUTPUT_DIR"
cp "$RPM_FILE" "$RPM_OUT"
echo "RPM written to $RPM_OUT"
