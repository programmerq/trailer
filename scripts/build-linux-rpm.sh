#!/usr/bin/env bash
# Build a Trailer RPM package.
#
# Usage:
#   scripts/build-linux-rpm.sh              # build inside a Fedora 40 Docker container
#   scripts/build-linux-rpm.sh --rebuild    # force a clean image rebuild first
#   scripts/build-linux-rpm.sh --no-docker  # build directly on this host (no Docker)
#
# Output: dist/trailer-<version>-1.<arch>.rpm (version derived from CMakeLists.txt)
#
# Two build paths:
#
#   Docker path (default): fedora:40 with Qt installed via aqtinstall. Requires
#   Docker. Kept for local dev / reproducing on the Fedora userland.
#
#   Host path (--no-docker, or auto-selected when Docker is absent): runs
#   rpmbuild directly on the Linux runner. `rpmbuild` works on Ubuntu via the
#   `rpm` apt package. Expects a Qt install (QT_PREFIX, default
#   /opt/qt/6.11.0/gcc_64) and optionally a prebuilt onnxruntime (ORT_PREFIX,
#   default /opt/onnxruntime-1.25.0) present on the runner. Mirrors the
#   dockerless style of the `windows-cross` release job.
#
# Not wired into GitHub Actions — invoke manually.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE=trailer-linux-rpm-build
PLATFORM_ARG=(--platform=linux/amd64)

# Host-path defaults (overridable via env); ignored by the Docker path.
HOST_QT_PREFIX="${QT_PREFIX:-/opt/qt/6.11.0/gcc_64}"
HOST_ORT_PREFIX="${ORT_PREFIX:-/opt/onnxruntime-1.25.0}"

PROJECT_VERSION=$(grep -oE '[0-9]+\.[0-9]+\.[0-9]+' "$REPO_ROOT/VERSION" | head -1)

# ── Host (no-Docker) build path ─────────────────────────────────────────────
run_host_build() {
    echo "==> Building RPM directly on host (no Docker)"
    echo "    Qt prefix:  $HOST_QT_PREFIX"
    echo "    ORT prefix: $HOST_ORT_PREFIX"
    if [[ ! -d "$HOST_QT_PREFIX" ]]; then
        echo "ERROR: Qt not found at $HOST_QT_PREFIX (set QT_PREFIX to override)." >&2
        exit 1
    fi
    if ! command -v rpmbuild >/dev/null 2>&1; then
        echo "ERROR: rpmbuild not found. Install the 'rpm' package." >&2
        exit 127
    fi
    mkdir -p "$REPO_ROOT/dist"
    SRC="$REPO_ROOT" \
    OUTPUT_DIR="$REPO_ROOT/dist" \
    QT_PREFIX="$HOST_QT_PREFIX" \
    ORT_PREFIX="$HOST_ORT_PREFIX" \
    RPMBUILD_TOP="${RPMBUILD_TOP:-/tmp/trailer-rpmbuild}" \
        bash "$REPO_ROOT/scripts/build-linux-rpm-inner.sh"
    ARCH=$(rpm --eval '%{_arch}')
    echo
    echo "==> Success! RPM is at: $REPO_ROOT/dist/trailer-${PROJECT_VERSION}-1.${ARCH}.rpm"
    exit 0
}

if [[ "${1:-}" == "--no-docker" ]]; then
    run_host_build
fi

if ! command -v docker >/dev/null 2>&1; then
    echo "docker not found — falling back to host build (pass --no-docker to force)." >&2
    run_host_build
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

RPM_NAME="trailer-${PROJECT_VERSION}-1.x86_64.rpm"
echo
echo "Success! RPM is at: $REPO_ROOT/dist/${RPM_NAME}"
echo
echo "Inspect with:"
echo "  rpm -qip dist/${RPM_NAME}"
echo "  rpm -qlp dist/${RPM_NAME}"
