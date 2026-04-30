#!/usr/bin/env bash
# Wrapper that runs the parametric Blender scene inside a Docker container.
#
# Usage:
#   ./render.sh                                   # 1024px, 256 samples
#   ./render.sh --size 512 --samples 128
#   ./render.sh --size 64  --samples 64 --out output/icon_64.png
#
# All flags after the script name are forwarded to render.py.

set -euo pipefail

IMAGE="${BLENDER_IMAGE:-linuxserver/blender:latest}"
HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

# Bind-mount only the icon/ directory (one level above is the worktree, which
# is more than Blender needs). Working dir inside container matches host.
docker run --rm \
  --entrypoint /usr/bin/blender \
  -v "${HERE}":"${HERE}" \
  -w "${HERE}" \
  "${IMAGE}" \
  --background \
  --python render.py \
  -- "$@"
