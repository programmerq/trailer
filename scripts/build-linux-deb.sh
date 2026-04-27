#!/usr/bin/env bash
# Build a Linux DEB package for Trailer using Docker.
#
# Usage:
#   scripts/build-linux-deb.sh              # build image if needed, package inside it
#   scripts/build-linux-deb.sh --rebuild    # force a clean image rebuild first
#
# Output: dist/trailer_<version>-1_amd64.deb (version derived from CMakeLists.txt)
#
# Requires Docker. The Docker image installs Qt 6.8.0 via aqtinstall
# (including the qtpdf module) — Qt is not available in standard Ubuntu
# repos at this version. The build runs inside ubuntu:22.04 to match the
# target distribution's libc and system libraries.
#
# Not wired into GitHub Actions — invoke manually.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE=trailer-linux-deb-build
DIST_DIR="$REPO_ROOT/dist"

# Derive version from CMakeLists.txt so it stays in sync with the project.
PROJECT_VERSION=$(grep -E '^\s*VERSION [0-9]+\.[0-9]+\.[0-9]+' "$REPO_ROOT/CMakeLists.txt" \
    | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)
if [[ -z "$PROJECT_VERSION" ]]; then
    echo "ERROR: could not extract project version from $REPO_ROOT/CMakeLists.txt" >&2
    exit 1
fi
PKG_VERSION="${PROJECT_VERSION}-1"

if ! command -v docker >/dev/null 2>&1; then
    echo "docker not found. Install Docker to use this script." >&2
    exit 127
fi

build_image() {
    docker build --platform=linux/amd64 -t "$IMAGE" -f - "$REPO_ROOT" <<'DOCKERFILE'
FROM --platform=linux/amd64 ubuntu:22.04

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        ninja-build \
        python3-pip \
        python3-venv \
        libqpdf-dev \
        libgl1-mesa-dev \
        libglib2.0-dev \
        dpkg-dev \
        patchelf \
        file \
    && rm -rf /var/lib/apt/lists/*

# aqtinstall in a venv (avoids PEP 668 "externally-managed-environment" error)
RUN python3 -m venv /opt/aqt-venv \
    && /opt/aqt-venv/bin/pip install --no-cache-dir aqtinstall
ENV PATH=/opt/aqt-venv/bin:$PATH

# Qt 6.8.0 with qtpdf module — not in standard Ubuntu 22.04 repos
RUN aqt install-qt linux desktop 6.8.0 gcc_64 -m qtpdf -O /opt/Qt

DOCKERFILE
}

case "${1:-}" in
    --rebuild)
        echo "==> Rebuilding Docker image: $IMAGE"
        docker rmi "$IMAGE" 2>/dev/null || true
        build_image
        ;;
    "")
        if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
            echo "==> Building Docker image: $IMAGE"
            build_image
        else
            echo "==> Using cached Docker image: $IMAGE"
        fi
        ;;
    *)
        echo "Unknown argument: $1" >&2
        echo "Usage: $0 [--rebuild]" >&2
        exit 2
        ;;
esac

echo "==> Running DEB build inside container"
mkdir -p "$DIST_DIR"
docker run --rm --platform=linux/amd64 \
    -v "$REPO_ROOT:/src" \
    -v "$DIST_DIR:/output" \
    -w /src \
    "$IMAGE" \
    bash /src/scripts/build-linux-deb-inner.sh

echo
echo "==> Success!"
echo "    Package: $DIST_DIR/trailer_${PKG_VERSION}_amd64.deb"
echo
echo "To inspect:"
echo "    dpkg --info $DIST_DIR/trailer_${PKG_VERSION}_amd64.deb"
echo "    dpkg -c     $DIST_DIR/trailer_${PKG_VERSION}_amd64.deb"
