#!/usr/bin/env bash
# Inner script: assembles the Windows MSI with wixl (msitools).
#
# Runs in two contexts:
#   * inside the trailer-windows-build Docker container (legacy /src /output)
#   * directly on a Linux host, invoked by `build-windows-msi.sh --no-docker`
#     (wixl runs host-side; the exe tree comes from the mingw cross build)
#
# Environment-specific paths are overridable via env vars (defaults match the
# historical Docker layout):
#   SRC          repo root                 (default: /src)
#   OUTPUT_DIR   where the .msi is written  (default: /output)
#   SKIP_CROSS_BUILD  if set to 1, assume build-windows/ is already populated
#                     and do NOT invoke scripts/build-windows.sh
#
# Steps:
#   1. Cross-compile trailer.exe + DLLs into $SRC/build-windows/ (unless skipped)
#   2. Generate DllComponents.wxs (DLLs + bundled license texts) from that tree
#   3. Run wixl to produce the MSI

set -euo pipefail

SRC="${SRC:-/src}"
OUTPUT_DIR="${OUTPUT_DIR:-/output}"
SKIP_CROSS_BUILD="${SKIP_CROSS_BUILD:-0}"

PROJECT_VERSION=$(grep -oE '[0-9]+\.[0-9]+\.[0-9]+' "$SRC/VERSION" | head -1)
if [[ -z "$PROJECT_VERSION" ]]; then
    echo "ERROR: could not extract project version from $SRC/VERSION" >&2
    exit 1
fi
MSI_OUT="${OUTPUT_DIR}/Trailer-${PROJECT_VERSION}-Windows.msi"

# ── 1. Cross-compile (reuse existing build script) ──────────────────────────

if [[ "$SKIP_CROSS_BUILD" == "1" ]]; then
    echo "==> Skipping cross-build (SKIP_CROSS_BUILD=1); using existing $SRC/build-windows/"
    if [[ ! -f "$SRC/build-windows/trailer.exe" ]]; then
        echo "ERROR: $SRC/build-windows/trailer.exe not found — run scripts/build-windows.sh first." >&2
        exit 1
    fi
else
    echo "==> Cross-compiling trailer.exe ..."
    bash "$SRC/scripts/build-windows.sh" --in-container
fi

# ── 2. Generate DllComponents.wxs (DLLs + license texts) ────────────────────

echo "==> Generating DllComponents.wxs ..."
python3 "$SRC/platform/windows/generate-wix-fragment.py" \
    "$SRC/build-windows"

# ── 3. Build the MSI ────────────────────────────────────────────────────────

echo "==> Running wixl ..."
mkdir -p "$OUTPUT_DIR"
# wixl resolves File Source= paths relative to its working directory, so run
# from the repo root (build-windows/…, resources/…, LICENSE, licenses/… all
# resolve from there).
cd "$SRC"
wixl \
    -o "$MSI_OUT" \
    "$SRC/platform/windows/trailer.wxs" \
    "$SRC/platform/windows/DllComponents.wxs"

echo "==> MSI written to $MSI_OUT"
