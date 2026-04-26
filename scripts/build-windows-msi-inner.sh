#!/usr/bin/env bash
# Inner script: runs INSIDE the Docker container.
# Called by scripts/build-windows-msi.sh via `docker run`.
#
# Assumes:
#   - /src    = repo root (bind-mounted by the outer script)
#   - /output = destination for Trailer-0.1.0-Windows.msi
#   - Working directory: /src
#   - Docker image: trailer-windows-build (docker/windows/Dockerfile)
#
# Steps:
#   1. Cross-compile trailer.exe + DLLs into /src/build-windows/
#      (delegates to the existing build-windows.sh --in-container)
#   2. Generate DllComponents.wxs from the build output
#   3. Run wixl to produce the MSI

set -euo pipefail

REPO_ROOT=/src

# ── 1. Cross-compile (reuse existing build script) ──────────────────────────

echo "==> Cross-compiling trailer.exe ..."
bash "$REPO_ROOT/scripts/build-windows.sh" --in-container

# ── 2. Generate DllComponents.wxs ───────────────────────────────────────────

echo "==> Generating DllComponents.wxs ..."
python3 "$REPO_ROOT/platform/windows/generate-wix-fragment.py" \
    "$REPO_ROOT/build-windows"

# ── 3. Build the MSI ────────────────────────────────────────────────────────

echo "==> Running wixl ..."
wixl \
    -o /output/Trailer-0.1.0-Windows.msi \
    "$REPO_ROOT/platform/windows/trailer.wxs" \
    "$REPO_ROOT/platform/windows/DllComponents.wxs"

echo "==> MSI written to /output/Trailer-0.1.0-Windows.msi"
