#!/usr/bin/env bash
# Build a Linux DEB package for Trailer.
#
# Usage:
#   scripts/build-linux-deb.sh              # build inside Docker (ubuntu:22.04 + aqt Qt)
#   scripts/build-linux-deb.sh --rebuild    # force a clean image rebuild first
#   scripts/build-linux-deb.sh --no-docker  # build directly on this host (no Docker)
#
# Output: dist/trailer_<version>-1_amd64.deb (version derived from CMakeLists.txt)
#
# Two build paths:
#
#   Docker path (default): installs Qt via aqtinstall inside ubuntu:22.04 to
#   match the target distribution's libc. Requires Docker. Kept for local dev.
#
#   Host path (--no-docker, or auto-selected when Docker is absent): runs the
#   inner packaging logic directly on the Linux runner, mirroring how the
#   `windows-cross` release job cross-builds host-side without Docker. Expects
#   a Qt install (QT_PREFIX, default /opt/qt/6.11.0/gcc_64) and, optionally, a
#   prebuilt onnxruntime (ORT_PREFIX, default /opt/onnxruntime-1.25.0) present
#   on the runner.
#
# Not wired into GitHub Actions — invoke manually.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE=trailer-linux-deb-build
DIST_DIR="$REPO_ROOT/dist"

# Host-path defaults (overridable via env). These point at the toolchain the
# self-hosted Linux runner ships; the Docker path ignores them.
HOST_QT_PREFIX="${QT_PREFIX:-/opt/qt/6.11.0/gcc_64}"
HOST_ORT_PREFIX="${ORT_PREFIX:-/opt/onnxruntime-1.25.0}"

# Derive version from the VERSION file (the project's single source of truth;
# CMakeLists.txt reads the same file into project(VERSION ...)).
PROJECT_VERSION=$(grep -oE '[0-9]+\.[0-9]+\.[0-9]+' "$REPO_ROOT/VERSION" | head -1)
if [[ -z "$PROJECT_VERSION" ]]; then
    echo "ERROR: could not extract project version from $REPO_ROOT/VERSION" >&2
    exit 1
fi
PKG_VERSION="${PROJECT_VERSION}-1"

# ── Host (no-Docker) build path ─────────────────────────────────────────────
run_host_build() {
    echo "==> Building DEB directly on host (no Docker)"
    echo "    Qt prefix:  $HOST_QT_PREFIX"
    echo "    ORT prefix: $HOST_ORT_PREFIX"
    if [[ ! -d "$HOST_QT_PREFIX" ]]; then
        echo "ERROR: Qt not found at $HOST_QT_PREFIX (set QT_PREFIX to override)." >&2
        exit 1
    fi
    mkdir -p "$DIST_DIR"
    SRC="$REPO_ROOT" \
    OUTPUT_DIR="$DIST_DIR" \
    QT_PREFIX="$HOST_QT_PREFIX" \
    ORT_PREFIX="$HOST_ORT_PREFIX" \
    BUILD_DIR="${BUILD_DIR:-/tmp/trailer-deb-build}" \
    STAGING="${STAGING:-/tmp/trailer-deb-staging}" \
        bash "$REPO_ROOT/scripts/build-linux-deb-inner.sh"
    echo
    echo "==> Success!"
    echo "    Package: $DIST_DIR/trailer_${PKG_VERSION}_amd64.deb"
    exit 0
}

if [[ "${1:-}" == "--no-docker" ]]; then
    run_host_build
fi

# Auto-fall back to the host path when Docker is unavailable (e.g. the
# self-hosted runner pods have no Docker daemon).
if ! command -v docker >/dev/null 2>&1; then
    echo "docker not found — falling back to host build (pass --no-docker to force)." >&2
    run_host_build
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
