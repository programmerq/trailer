#!/usr/bin/env bash
# Trailer UAT runner.
#
# Usage:
#   scripts/run-uat.sh              # build the image if needed, run UAT inside it
#   scripts/run-uat.sh --host       # run against a host-native build (skip docker)
#   scripts/run-uat.sh --rebuild    # force rebuild of the docker image
#   scripts/run-uat.sh --in-container  # internal: run the UAT suite assuming we are already inside the container
#
# Intended to be invoked manually, via workflow_dispatch (.github/workflows/uat-
# dispatch.yml), or by the tag-triggered release pipeline — not by push/PR CI.

set -euo pipefail

# Always operate from the repo root regardless of where the caller
# invokes this from.
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

IMAGE=trailer-uat
DOCKERFILE=docker/uat/Dockerfile
# Default build dir when invoked via Docker. --host overrides to
# `build` so local devs can reuse their dev build.
BUILD_DIR="${BUILD_DIR:-build-uat}"

run_suite() {
    # Expects to be run with CWD = repo root. Uses $BUILD_DIR;
    # configures + builds if it doesn't exist yet. Prefer Ninja when
    # available (baked into the Docker image); fall back to the
    # platform default so --host works on a fresh macOS box.
    local gen_arg=()
    if command -v ninja >/dev/null 2>&1; then
        gen_arg=(-G Ninja)
    fi
    if [[ ! -d "$BUILD_DIR" ]]; then
        cmake -S . -B "$BUILD_DIR" ${gen_arg[@]+"${gen_arg[@]}"} -DCMAKE_BUILD_TYPE=Release
    fi
    cmake --build "$BUILD_DIR" -j
    QT_QPA_PLATFORM=offscreen ctest --test-dir "$BUILD_DIR" \
        -L uat --output-on-failure
}

case "${1:-}" in
    --in-container)
        # Build outside the mounted workspace. Otherwise the build
        # dir gets reused across host and container runs and
        # CMakeCache.txt pins itself to whichever side ran first,
        # erroring on the other (the paths — /work/... vs
        # /Users/... — don't match).
        BUILD_DIR="${BUILD_DIR:-/tmp/trailer-build-uat}"
        run_suite
        ;;
    --host)
        # Point at the dev's existing build/ dir by default so they
        # don't pay a second CMake configure just for UAT.
        if [[ "$BUILD_DIR" == "build-uat" ]]; then
            BUILD_DIR=build
        fi
        run_suite
        ;;
    --rebuild)
        docker build --no-cache -t "$IMAGE" -f "$DOCKERFILE" .
        exec "$0"  # run again without --rebuild
        ;;
    "")
        if ! command -v docker >/dev/null 2>&1; then
            echo "docker not found. Install Docker or run with --host." >&2
            exit 127
        fi
        if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
            docker build -t "$IMAGE" -f "$DOCKERFILE" .
        fi
        docker run --rm -v "$PWD":/work -w /work "$IMAGE" \
            bash scripts/run-uat.sh --in-container
        ;;
    *)
        echo "Unknown argument: $1" >&2
        echo "See header of $0 for usage." >&2
        exit 2
        ;;
esac
