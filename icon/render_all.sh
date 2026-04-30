#!/usr/bin/env bash
# Render the full set of icon sizes in one go.
#
# Sample counts scale with output size — small icons get fewer samples since
# denoising hides noise at low resolution.

set -euo pipefail
HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

# Parallel arrays (kept compatible with macOS Bash 3 — no `declare -A`).
SIZES=(64 128 256 512 1024)
SAMPLES=(32 64 96 160 256)

for i in "${!SIZES[@]}"; do
  size="${SIZES[$i]}"
  samples="${SAMPLES[$i]}"
  out="output/icon_${size}.png"
  echo "→ rendering ${size}px (${samples} samples) → ${out}"
  "${HERE}/render.sh" \
    --size "${size}" \
    --samples "${samples}" \
    --out "${out}"
done

echo "done."
ls -la "${HERE}/output/" | grep "icon_"
