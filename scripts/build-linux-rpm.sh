#!/usr/bin/env bash
# Build a Trailer RPM package using a Fedora 40 Docker container.
#
# Usage:
#   scripts/build-linux-rpm.sh              # build image if needed, build RPM inside it
#   scripts/build-linux-rpm.sh --rebuild    # force a clean image rebuild first
#
# Output: dist/trailer-0.1.0-1.x86_64.rpm
#
# Requires: Docker (desktop or engine), x86_64 host or ARM with QEMU emulation.
# Qt 6.8.0 with qtpdf is not in the Fedora 40 repositories; the container
# installs it via aqtinstall. This makes the image build slow (~10 min on
# first run) but subsequent builds reuse the cached image layer.
#
# Not wired into GitHub Actions — invoke manually.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE=trailer-linux-rpm-build
PLATFORM_ARG=(--platform=linux/amd64)

if ! command -v docker >/dev/null 2>&1; then
    echo "docker not found. Install Docker to use this script." >&2
    exit 127
fi

mkdir -p "$REPO_ROOT/dist"

build_image() {
    echo "Building Docker image: $IMAGE"
    docker build "${PLATFORM_ARG[@]}" -t "$IMAGE" -f - "$REPO_ROOT" <<'DOCKERFILE'
FROM fedora:40

# Install build tools and system dependencies
RUN dnf install -y \
        gcc \
        gcc-c++ \
        cmake \
        ninja-build \
        python3-pip \
        qpdf-devel \
        mesa-libGL-devel \
        glib2-devel \
        rpm-build \
        rpmdevtools \
    && dnf clean all

# Install aqtinstall to fetch Qt 6.8.0 (not available in Fedora repos at this version)
RUN pip3 install --no-cache-dir aqtinstall

# Install Qt 6.8.0 for Linux with the qtpdf module
RUN aqt install-qt linux desktop 6.8.0 gcc_64 -m qtpdf -O /opt/Qt

WORKDIR /src
DOCKERFILE
}

case "${1:-}" in
    --rebuild)
        build_image
        ;;
    "")
        if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
            build_image
        fi
        ;;
    *)
        echo "Unknown argument: $1" >&2
        echo "Usage: $0 [--rebuild]" >&2
        exit 2
        ;;
esac

echo "Running RPM build inside container..."
docker run "${PLATFORM_ARG[@]}" --rm \
    -v "$REPO_ROOT:/src" \
    -v "$REPO_ROOT/dist:/output" \
    -w /src \
    "$IMAGE" \
    bash /src/scripts/build-linux-rpm-inner.sh

echo
echo "Success! RPM is at: $REPO_ROOT/dist/trailer-0.1.0-1.x86_64.rpm"
echo
echo "Inspect with:"
echo "  rpm -qip dist/trailer-0.1.0-1.x86_64.rpm"
echo "  rpm -qlp dist/trailer-0.1.0-1.x86_64.rpm"
