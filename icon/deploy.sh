#!/usr/bin/env bash
# Take the rendered iconset in output/iconset/, pngcrush each PNG, copy them
# to ../resources/icons/, and generate a macOS trailer.icns from them.
#
# Run after `make_iconset.py` has produced output/iconset/. Commits the
# optimized icons into the repo so the build can reference them.

set -euo pipefail

HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${HERE}/.." && pwd)"
SRC_DIR="${HERE}/output/iconset"
DST_DIR="${REPO_ROOT}/resources/icons"
ICONSET_TMP="$(mktemp -d -t trailer-iconset.XXXXXX)"
trap 'rm -rf "${ICONSET_TMP}"' EXIT

# Sizes shipped to the repo. The Qt resource references these by exact name.
PNG_SIZES=(16 32 64 128 256 512 1024)

if [[ ! -d "${SRC_DIR}" ]]; then
  echo "error: ${SRC_DIR} not found — run make_iconset.py first." >&2
  exit 1
fi

mkdir -p "${DST_DIR}"

# 1) pngcrush each size into the repo's resources/icons/ directory.
for size in "${PNG_SIZES[@]}"; do
  src="${SRC_DIR}/icon_${size}.png"
  dst="${DST_DIR}/trailer_${size}.png"
  if [[ ! -f "${src}" ]]; then
    echo "warn: missing ${src}, skipping ${size}px" >&2
    continue
  fi
  pngcrush -ow -rem allb -reduce -brute "${src}" /tmp/pngcrush.tmp >/dev/null 2>&1 || true
  pngcrush -q -rem allb -reduce -brute "${src}" "${dst}" >/dev/null
  echo "  ${size}px → ${dst} ($(stat -f '%z' "${dst}") bytes)"
done

# 2) Generate trailer.icns for the macOS bundle.
# iconutil expects an .iconset/ directory with very specific filenames covering
# the @1x and @2x variants. Map our sizes:
#   icon_16x16.png        ← 16
#   icon_16x16@2x.png     ← 32
#   icon_32x32.png        ← 32
#   icon_32x32@2x.png     ← 64
#   icon_128x128.png      ← 128
#   icon_128x128@2x.png   ← 256
#   icon_256x256.png      ← 256
#   icon_256x256@2x.png   ← 512
#   icon_512x512.png      ← 512
#   icon_512x512@2x.png   ← 1024
ICONSET="${ICONSET_TMP}/trailer.iconset"
mkdir -p "${ICONSET}"
cp "${DST_DIR}/trailer_16.png"   "${ICONSET}/icon_16x16.png"
cp "${DST_DIR}/trailer_32.png"   "${ICONSET}/icon_16x16@2x.png"
cp "${DST_DIR}/trailer_32.png"   "${ICONSET}/icon_32x32.png"
cp "${DST_DIR}/trailer_64.png"   "${ICONSET}/icon_32x32@2x.png"
cp "${DST_DIR}/trailer_128.png"  "${ICONSET}/icon_128x128.png"
cp "${DST_DIR}/trailer_256.png"  "${ICONSET}/icon_128x128@2x.png"
cp "${DST_DIR}/trailer_256.png"  "${ICONSET}/icon_256x256.png"
cp "${DST_DIR}/trailer_512.png"  "${ICONSET}/icon_256x256@2x.png"
cp "${DST_DIR}/trailer_512.png"  "${ICONSET}/icon_512x512.png"
cp "${DST_DIR}/trailer_1024.png" "${ICONSET}/icon_512x512@2x.png"

iconutil --convert icns --output "${DST_DIR}/trailer.icns" "${ICONSET}"
echo "  → ${DST_DIR}/trailer.icns ($(stat -f '%z' "${DST_DIR}/trailer.icns") bytes)"

# 3) Generate trailer.ico for Windows. Multi-resolution ICO containing the
# common Windows icon sizes so Explorer and the taskbar both look right.
if command -v magick >/dev/null 2>&1; then
  magick \
    "${DST_DIR}/trailer_16.png" \
    "${DST_DIR}/trailer_32.png" \
    "${DST_DIR}/trailer_64.png" \
    "${DST_DIR}/trailer_128.png" \
    "${DST_DIR}/trailer_256.png" \
    "${DST_DIR}/trailer.ico"
  echo "  → ${DST_DIR}/trailer.ico ($(stat -f '%z' "${DST_DIR}/trailer.ico") bytes)"
else
  echo "  (skipped trailer.ico — install ImageMagick for Windows .ico generation)"
fi

echo "done."
