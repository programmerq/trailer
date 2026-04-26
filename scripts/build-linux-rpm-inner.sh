#!/usr/bin/env bash
# Inner build script — runs inside the trailer-linux-rpm-build Docker container.
# Invoked by scripts/build-linux-rpm.sh; not meant to be run directly on the host.
#
# Strategy: cmake configure + build first, then call rpmbuild -bb with the
# build directory already populated so the spec's %build section is a no-op
# and %install can just run cmake --install.

set -euo pipefail

QT_PREFIX=/opt/Qt/6.8.0/gcc_64
BUILD_DIR=/root/rpmbuild/BUILD/build-trailer
SOURCE_DIR=/root/rpmbuild/BUILD/trailer-source

rpmdev-setuptree

# Copy into rpmbuild's BUILD area so cmake artifacts don't pollute the bind-mounted /src
echo "Copying source to rpmbuild BUILD area..."
cp -r /src "$SOURCE_DIR"

echo "Configuring..."
cmake -B "$BUILD_DIR" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH="$QT_PREFIX" \
      -S "$SOURCE_DIR"

echo "Building..."
cmake --build "$BUILD_DIR" -j"$(nproc)"

cp "$SOURCE_DIR/packaging/rpm/trailer.spec" /root/rpmbuild/SPECS/trailer.spec

# --define _builddir so the spec's %install can reference the pre-built artifacts
echo "Packaging RPM..."
rpmbuild -bb \
    --define "_builddir /root/rpmbuild/BUILD" \
    /root/rpmbuild/SPECS/trailer.spec

RPM_FILE=$(find /root/rpmbuild/RPMS/x86_64 -name 'trailer-*.rpm' | head -1)
if [[ -z "$RPM_FILE" ]]; then
    echo "ERROR: No RPM found under /root/rpmbuild/RPMS/x86_64" >&2
    exit 1
fi
cp "$RPM_FILE" /output/trailer-0.1.0-1.x86_64.rpm
echo "RPM written to /output/trailer-0.1.0-1.x86_64.rpm"
