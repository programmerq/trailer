#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# ux-walkthrough drive harness — Tier-1 Linux/Xvfb runner.
#
# Drives the REAL built `trailer` binary through the four ux-walkthrough
# golden paths under a REAL X server (Xvfb), capturing a screenshot after
# every scripted step into a per-run artifact directory a review agent can
# Read and judge with the persona (A) contract in
# .claude/skills/ux-walkthrough/SKILL.md.
#
# This is the DRIVE + CAPTURE half. The judge (persona A) is a separate
# skill that consumes the per-step bundles this produces.
#
# USAGE
#   tools/ux-walkthrough/run.sh [all|01|02|03|04 ...] [options]
#
#   Path selectors (default: all):
#     01  new-from-clipboard
#     02  screenshot-acquire
#     03  open-zoom-navigate
#     04  close-with-unsaved
#
#   Options:
#     --bin PATH     path to the trailer binary (default: <repo>/build/trailer)
#     --out DIR      artifact root (default: <repo>/uat-screenshots/ux-walkthrough/<ts>)
#     --geometry WxH Xvfb screen size (default: 1400x1000)
#     -h|--help
#
# Requires (Tier-1 deps): xvfb-run, xdotool, ImageMagick (import/convert),
# x11-apps (xwd), xclip. On the CI runner image these are baked into
# docker/runner/Dockerfile. Locally: apt-get install xvfb x11-apps xdotool
# imagemagick xclip libxcb-cursor0.
# ---------------------------------------------------------------------------
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"

TRAILER_BIN="$REPO/build/trailer"
GEOMETRY="1400x1000"
TS="$(date +%Y%m%d-%H%M%S)"
OUT_ROOT="$REPO/uat-screenshots/ux-walkthrough/$TS"
SELECT=()

declare -A PATHS=(
    [01]="01-new-from-clipboard"
    [02]="02-screenshot-acquire"
    [03]="03-open-zoom-navigate"
    [04]="04-close-with-unsaved"
)

while [ $# -gt 0 ]; do
    case "$1" in
        all) SELECT=(01 02 03 04) ;;
        01|02|03|04) SELECT+=("$1") ;;
        --bin) TRAILER_BIN="$2"; shift ;;
        --out) OUT_ROOT="$2"; shift ;;
        --geometry) GEOMETRY="$2"; shift ;;
        -h|--help) sed -n '2,40p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
    shift
done
[ ${#SELECT[@]} -gt 0 ] || SELECT=(01 02 03 04)

# Dedup while preserving first-seen order, so e.g. `run.sh all 04` runs each
# path once.
_seen=""
_dedup=()
for _k in "${SELECT[@]}"; do
    case " $_seen " in *" $_k "*) continue ;; esac
    _seen="$_seen $_k"
    _dedup+=("$_k")
done
SELECT=("${_dedup[@]}")

# --- preflight -------------------------------------------------------------
missing=()
for tool in xvfb-run xdotool import xwd xclip convert openbox; do
    command -v "$tool" >/dev/null 2>&1 || missing+=("$tool")
done
if [ ${#missing[@]} -gt 0 ]; then
    echo "ERROR: missing required tools: ${missing[*]}" >&2
    echo "Install: apt-get install xvfb x11-apps xdotool imagemagick xclip libxcb-cursor0 openbox" >&2
    exit 1
fi
if [ ! -x "$TRAILER_BIN" ]; then
    echo "ERROR: trailer binary not found/executable at: $TRAILER_BIN" >&2
    echo "Build it: cmake -S . -B build -G Ninja && cmake --build build -j" >&2
    exit 1
fi

mkdir -p "$OUT_ROOT"
echo "ux-walkthrough drive harness"
echo "  binary:   $TRAILER_BIN"
echo "  out:      $OUT_ROOT"
echo "  geometry: $GEOMETRY"
echo "  paths:    ${SELECT[*]}"
echo

rc_total=0
for key in "${SELECT[@]}"; do
    slug="${PATHS[$key]}"
    script="$HERE/paths/$slug.sh"
    run_dir="$OUT_ROOT/$slug"
    if [ ! -f "$script" ]; then
        echo "SKIP $slug (no script)"; continue
    fi
    echo "=== driving $slug ==="
    mkdir -p "$run_dir"
    # Each path gets its own fresh Xvfb + real X server, with a real window
    # manager (openbox) running inside it. The WM is REQUIRED, not incidental:
    # without _NET_ACTIVE_WINDOW support xdotool cannot activate/focus the app
    # window, and — more to the point — Tier-1 exists to exercise real WM /
    # focus / modal-stacking behaviour (the "vanishing menu / modal" class).
    # The WM is started inside the session, given a moment to come up, then the
    # path script runs; the WM is torn down with the session.
    # SC2016: the single-quoted body is intentional — it must expand $1/$!/$?
    # inside the xvfb session at run time, not when run.sh is parsed.
    # shellcheck disable=SC2016
    TRAILER_BIN="$TRAILER_BIN" RUN_DIR="$run_dir" HARNESS_LIB="$HERE/lib/harness.sh" \
        xvfb-run -a --server-args="-screen 0 ${GEOMETRY}x24" \
        bash -c 'openbox >/dev/null 2>&1 & _wm=$!; sleep 1; bash "$1"; _rc=$?; kill "$_wm" 2>/dev/null; exit $_rc' \
        _ "$script"
    rc=$?
    if [ "$rc" -eq 0 ]; then
        shots="$(find "$run_dir" -maxdepth 1 -name '*.png' | wc -l | tr -d ' ')"
        echo "--- $slug: OK ($shots screenshots)"
    else
        echo "--- $slug: FAILED (rc=$rc)"
        rc_total=1
    fi
    echo
done

echo "Artifacts under: $OUT_ROOT"
echo "Judge them with persona (A) in .claude/skills/ux-walkthrough/SKILL.md;"
echo "each NN-*.png has a sibling NN-*.txt with its label + expected effect."
exit "$rc_total"
