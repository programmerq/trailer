#!/usr/bin/env bash
# Build a Windows MSI installer for Trailer using Docker + wixl (msitools).
#
# Prerequisites:
#   - Docker installed and running
#   - The repo checked out (this script is in scripts/)
#
# Output: dist/Trailer-<version>-Windows.msi (version from the VERSION file)
#
# Usage:
#   scripts/build-windows-msi.sh              # build inside Docker
#   scripts/build-windows-msi.sh --rebuild    # force a clean image rebuild
#   scripts/build-windows-msi.sh --no-docker  # build directly on this host
#
# The Docker image (trailer-windows-build) is built from
# docker/windows/Dockerfile, which includes msitools/wixl.  On first
# run the image build takes several minutes (Qt download).  Subsequent
# runs use the cached image.
#
# Usage:
#   scripts/build-windows-msi.sh              # build image if needed, produce MSI
#   scripts/build-windows-msi.sh --rebuild    # force a clean image rebuild first
#
# Not wired into GitHub Actions — invoke manually (signing cert required
# before CI integration makes sense).

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE=trailer-windows-build
DOCKERFILE="$REPO_ROOT/docker/windows/Dockerfile"
# Same --platform arg as build-windows.sh: ensures x86_64 image on ARM hosts.
PLATFORM_ARG=(--platform=linux/amd64)

PROJECT_VERSION=$(grep -oE '[0-9]+\.[0-9]+\.[0-9]+' "$REPO_ROOT/VERSION" | head -1)
if [[ -z "$PROJECT_VERSION" ]]; then
    echo "ERROR: could not extract project version from $REPO_ROOT/VERSION" >&2
    exit 1
fi

# ── Host (no-Docker) build path ─────────────────────────────────────────────
#
# Runs the mingw cross build + wixl directly on the Linux runner (mirroring the
# `windows-cross` release job). Requires the mingw-w64 + Qt(win64_mingw) + qpdf
# toolchain on the host (installed by .github/actions/setup-windows-cross).
# Set SKIP_CROSS_BUILD=1 to reuse an already-populated build-windows/ tree.
run_host_build() {
    echo "==> Building MSI directly on host (no Docker)"
    if ! command -v wixl >/dev/null 2>&1; then
        echo "ERROR: wixl not found. Install the 'wixl' apt package (msitools on some distros)." >&2
        exit 127
    fi
    mkdir -p "$REPO_ROOT/dist"
    SRC="$REPO_ROOT" \
    OUTPUT_DIR="$REPO_ROOT/dist" \
    SKIP_CROSS_BUILD="${SKIP_CROSS_BUILD:-0}" \
        bash "$REPO_ROOT/scripts/build-windows-msi-inner.sh"
    MSI="$REPO_ROOT/dist/Trailer-${PROJECT_VERSION}-Windows.msi"
    if [[ -f "$MSI" ]]; then
        echo
        echo "==> MSI built successfully:"
        ls -lh "$MSI"
        command -v file >/dev/null 2>&1 && file "$MSI"
    else
        echo "ERROR: Expected output not found: $MSI" >&2
        exit 1
    fi
    exit 0
}

if [[ "${1:-}" == "--no-docker" ]]; then
    run_host_build
fi

# ── Checks ─────────────────────────────────────────────────────────────────

if ! command -v docker >/dev/null 2>&1; then
    echo "docker not found — falling back to host build (pass --no-docker to force)." >&2
    run_host_build
fi

# ── Docker image ────────────────────────────────────────────────────────────

case "${1:-}" in
    --rebuild)
        echo "==> Rebuilding Docker image $IMAGE (no-cache) ..."
        docker build "${PLATFORM_ARG[@]}" --no-cache \
            --progress=plain \
            -t "$IMAGE" \
            -f "$DOCKERFILE" \
            "$REPO_ROOT"
        ;;
    "")
        if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
            echo "==> Docker image $IMAGE not found. Building ..."
            docker build "${PLATFORM_ARG[@]}" \
                --progress=plain \
                -t "$IMAGE" \
                -f "$DOCKERFILE" \
                "$REPO_ROOT"
        else
            echo "==> Using cached Docker image $IMAGE"
        fi
        ;;
    *)
        echo "Unknown argument: $1" >&2
        echo "Usage: $0 [--rebuild]" >&2
        exit 2
        ;;
esac

# ── Output directory ────────────────────────────────────────────────────────

mkdir -p "$REPO_ROOT/dist"

# ── Run the inner build script inside Docker ────────────────────────────────

echo "==> Running MSI build inside Docker ..."
docker run "${PLATFORM_ARG[@]}" --rm \
    -v "$REPO_ROOT":/src \
    -v "$REPO_ROOT/dist":/output \
    -w /src \
    "$IMAGE" \
    bash /src/scripts/build-windows-msi-inner.sh

# ── Report ──────────────────────────────────────────────────────────────────

MSI="$REPO_ROOT/dist/Trailer-${PROJECT_VERSION}-Windows.msi"
if [[ -f "$MSI" ]]; then
    echo
    echo "==> MSI built successfully:"
    ls -lh "$MSI"
    if command -v file >/dev/null 2>&1; then
        file "$MSI"
    fi
else
    echo "ERROR: Expected output not found: $MSI" >&2
    exit 1
fi
