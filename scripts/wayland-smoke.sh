#!/usr/bin/env bash
#
# wayland-smoke.sh — deterministic "does Trailer render on native Wayland?" smoke.
#
# This is the launch-and-screenshot Wayland smoke tier (Phase 2 of the Wayland
# CI work). It proves the one thing offscreen unit tests and the Wine tier can
# NEVER prove: that the shipped Qt binary loads the *wayland* platform plugin,
# maps a real surface on a real compositor, and paints actual UI into it.
#
# Recipe follows the Phase 1 investigation exactly (sway 1.9 headless + pixman +
# grim). See docs/ci/wayland-tier.md for the rationale (why sway over weston,
# why launch+screenshot-only, why the unit/UAT suites stay on offscreen).
#
# What it does, in order:
#   1. Create a SHORT XDG_RUNTIME_DIR (sway's IPC socket path must fit
#      sockaddr_un.sun_path ~108 chars — a long scratchpad path segfaults sway).
#   2. Start sway headless (WLR_BACKENDS=headless, WLR_RENDERER=pixman — no GPU),
#      poll for its wayland socket (no blind sleep).
#   3. Launch build/trailer on a sample image under QT_QPA_PLATFORM=wayland with
#      qt.qpa logging on; poll its log for the plugin-loaded line.
#   4. ASSERT platformName is wayland (grep the "Successfully loaded ... wayland"
#      line). Hard-fail if it fell back to xcb/offscreen.
#   5. grim-capture the compositor output to a PNG.
#   6. ASSERT the PNG is 1280x800 AND non-blank (not a single flat colour).
#   7. Clean up (trap kills trailer + sway) and exit 0 only if every assert passed.
#
# Idempotent / CI-safe: each run gets its own runtime dir and cleans up on exit.
#
# Usage:
#   scripts/wayland-smoke.sh                 # uses build/trailer + a bundled sample
#   TRAILER_BIN=path/to/trailer scripts/wayland-smoke.sh
#   WAYLAND_SMOKE_OUT=/tmp/shot.png scripts/wayland-smoke.sh
#
set -euo pipefail

log() { printf '[wayland-smoke] %s\n' "$*"; }
err() { printf '[wayland-smoke] ERROR: %s\n' "$*" >&2; }

# ---------------------------------------------------------------------------
# Paths / config
# ---------------------------------------------------------------------------
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"

WIDTH="${WAYLAND_SMOKE_WIDTH:-1280}"
HEIGHT="${WAYLAND_SMOKE_HEIGHT:-800}"

TRAILER_BIN="${TRAILER_BIN:-$REPO_ROOT/build/trailer}"
# A sample document so Trailer opens a real, content-filled window (Phase 1
# used an image doc — the money shot). Fall back gracefully if it's ever moved.
SAMPLE_DOC="${WAYLAND_SMOKE_DOC:-$REPO_ROOT/docs/perf/corpus/photo.jpg}"
OUT_PNG="${WAYLAND_SMOKE_OUT:-$REPO_ROOT/wayland-smoke.png}"

# Timeouts (seconds) — polled, never a blind sleep.
SOCKET_TIMEOUT="${WAYLAND_SMOKE_SOCKET_TIMEOUT:-15}"
PLUGIN_TIMEOUT="${WAYLAND_SMOKE_PLUGIN_TIMEOUT:-20}"
# Short settle after the surface maps so the first frame is painted before grim.
PAINT_SETTLE="${WAYLAND_SMOKE_PAINT_SETTLE:-2}"

# Non-blank thresholds (see assert_png_nonblank).
MIN_UNIQUE_COLORS="${WAYLAND_SMOKE_MIN_COLORS:-64}"

# ---------------------------------------------------------------------------
# Preflight
# ---------------------------------------------------------------------------
for tool in sway grim; do
  command -v "$tool" >/dev/null 2>&1 || { err "'$tool' not found on PATH — install sway + grim (see docker/runner/Dockerfile)"; exit 2; }
done
[ -x "$TRAILER_BIN" ] || { err "trailer binary not found or not executable: $TRAILER_BIN (build it first)"; exit 2; }
if [ ! -f "$SAMPLE_DOC" ]; then
  log "sample doc '$SAMPLE_DOC' missing — launching Trailer with no file (empty-state window)"
  SAMPLE_DOC=""
fi

# ---------------------------------------------------------------------------
# XDG_RUNTIME_DIR — MUST be short (sway IPC socket path limit). A fresh unique
# dir per run keeps the job idempotent / re-runnable. chmod 700 as XDG requires.
# ---------------------------------------------------------------------------
XDG_RUNTIME_DIR="$(mktemp -d /tmp/wl-smoke.XXXXXX)"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"
export XDG_RUNTIME_DIR
log "XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR"

SWAY_LOG="$XDG_RUNTIME_DIR/sway.log"
TRAILER_LOG="$XDG_RUNTIME_DIR/trailer.log"
SWAY_CFG="$XDG_RUNTIME_DIR/sway.cfg"

SWAY_PID=""
TRAILER_PID=""

cleanup() {
  local rc=$?
  # Kill children first so they release the compositor, then sway.
  [ -n "$TRAILER_PID" ] && kill "$TRAILER_PID" 2>/dev/null || true
  [ -n "$SWAY_PID" ] && kill "$SWAY_PID" 2>/dev/null || true
  # Give them a beat, then hard-kill any stragglers.
  for _ in 1 2 3 4 5; do
    { [ -n "$TRAILER_PID" ] && kill -0 "$TRAILER_PID" 2>/dev/null; } || \
    { [ -n "$SWAY_PID" ] && kill -0 "$SWAY_PID" 2>/dev/null; } || break
    sleep 0.2
  done
  [ -n "$TRAILER_PID" ] && kill -9 "$TRAILER_PID" 2>/dev/null || true
  [ -n "$SWAY_PID" ] && kill -9 "$SWAY_PID" 2>/dev/null || true
  rm -rf "$XDG_RUNTIME_DIR" 2>/dev/null || true
  return "$rc"
}
trap cleanup EXIT INT TERM

# ---------------------------------------------------------------------------
# 1. Start sway headless (pixman renderer — no GPU in CI).
# ---------------------------------------------------------------------------
cat > "$SWAY_CFG" <<EOF
output HEADLESS-1 resolution ${WIDTH}x${HEIGHT}
default_border none
# No wallpaper/bar: a bare output is a flat colour, so a blank capture stays
# blank (and fails the non-blank assert) unless Trailer actually paints.
EOF

log "starting sway headless (${WIDTH}x${HEIGHT}, pixman)"
env -u WAYLAND_DISPLAY \
  WLR_BACKENDS=headless \
  WLR_LIBINPUT_NO_DEVICES=1 \
  WLR_RENDERER=pixman \
  sway -c "$SWAY_CFG" >"$SWAY_LOG" 2>&1 &
SWAY_PID=$!

# Poll for the wayland socket (sway picks the first free wayland-N).
WAYLAND_DISPLAY=""
deadline=$(( $(date +%s) + SOCKET_TIMEOUT ))
while [ "$(date +%s)" -lt "$deadline" ]; do
  if ! kill -0 "$SWAY_PID" 2>/dev/null; then
    err "sway exited before its socket appeared — log follows:"; cat "$SWAY_LOG" >&2; exit 1
  fi
  for sock in "$XDG_RUNTIME_DIR"/wayland-*; do
    case "$sock" in *.lock) continue;; esac
    if [ -S "$sock" ]; then WAYLAND_DISPLAY="$(basename "$sock")"; break; fi
  done
  [ -n "$WAYLAND_DISPLAY" ] && break
  sleep 0.2
done
[ -n "$WAYLAND_DISPLAY" ] || { err "sway wayland socket did not appear within ${SOCKET_TIMEOUT}s"; cat "$SWAY_LOG" >&2; exit 1; }
export WAYLAND_DISPLAY
log "compositor up: WAYLAND_DISPLAY=$WAYLAND_DISPLAY"

# ---------------------------------------------------------------------------
# 2. Locate the Qt plugins dir (best-effort). Phase 1: the wayland platform
#    plugin ships in the aqt Qt bundle; if the binary can't already find its
#    plugin prefix (rpath / QLibraryInfo), QT_PLUGIN_PATH bridges the gap.
#    Harmless when Qt already knows where its plugins are.
# ---------------------------------------------------------------------------
if [ -z "${QT_PLUGIN_PATH:-}" ]; then
  qt_plugins=""
  for q in qmake6 qmake; do
    if command -v "$q" >/dev/null 2>&1; then
      qt_plugins="$("$q" -query QT_INSTALL_PLUGINS 2>/dev/null || true)"
      [ -n "$qt_plugins" ] && break
    fi
  done
  if [ -z "$qt_plugins" ]; then
    for cand in /opt/qt/*/gcc_64/plugins "$HOME"/Qt/*/gcc_64/plugins; do
      [ -d "$cand/platforms" ] && { qt_plugins="$cand"; break; }
    done
  fi
  if [ -n "$qt_plugins" ] && [ -d "$qt_plugins/platforms" ]; then
    export QT_PLUGIN_PATH="$qt_plugins"
    log "QT_PLUGIN_PATH=$QT_PLUGIN_PATH"
  else
    log "QT_PLUGIN_PATH unset and not auto-detected — relying on the binary's baked plugin prefix"
  fi
fi

# ---------------------------------------------------------------------------
# 3. Launch Trailer under the wayland plugin with qt.qpa logging on.
# ---------------------------------------------------------------------------
log "launching Trailer under QT_QPA_PLATFORM=wayland${SAMPLE_DOC:+ on $(basename "$SAMPLE_DOC")}"
set +e
if [ -n "$SAMPLE_DOC" ]; then
  QT_QPA_PLATFORM=wayland \
  QT_LOGGING_RULES='qt.qpa.*=true' \
  "$TRAILER_BIN" "$SAMPLE_DOC" >"$TRAILER_LOG" 2>&1 &
else
  QT_QPA_PLATFORM=wayland \
  QT_LOGGING_RULES='qt.qpa.*=true' \
  "$TRAILER_BIN" >"$TRAILER_LOG" 2>&1 &
fi
TRAILER_PID=$!
set -e

# ---------------------------------------------------------------------------
# 4. ASSERT platformName == wayland. Poll the log for the plugin-loaded line;
#    hard-fail on an xcb/offscreen fallback line or on process death.
# ---------------------------------------------------------------------------
plugin_ok=""
deadline=$(( $(date +%s) + PLUGIN_TIMEOUT ))
while [ "$(date +%s)" -lt "$deadline" ]; do
  if grep -qE 'Successfully loaded Qt platform plugin "wayland"' "$TRAILER_LOG" 2>/dev/null; then
    plugin_ok=1; break
  fi
  if grep -qE 'Successfully loaded Qt platform plugin "(xcb|offscreen|minimal|vnc)"' "$TRAILER_LOG" 2>/dev/null; then
    err "Trailer loaded a NON-wayland platform plugin (fallback) — qt.qpa log:"; cat "$TRAILER_LOG" >&2; exit 1
  fi
  if ! kill -0 "$TRAILER_PID" 2>/dev/null; then
    err "Trailer exited before mapping a wayland surface — qt.qpa log:"; cat "$TRAILER_LOG" >&2; exit 1
  fi
  sleep 0.2
done
[ -n "$plugin_ok" ] || { err "did not observe the wayland-plugin-loaded line within ${PLUGIN_TIMEOUT}s — qt.qpa log:"; cat "$TRAILER_LOG" >&2; exit 1; }
log "ASSERT PASS: platformName is wayland (xdg-shell), no xcb/offscreen fallback"

# Belt-and-suspenders: confirm the compositor actually has a mapped view.
if command -v swaymsg >/dev/null 2>&1; then
  tree="$(swaymsg -t get_tree 2>/dev/null || true)"
  if printf '%s' "$tree" | grep -q '"app_id"'; then
    log "compositor reports a mapped view (swaymsg get_tree)"
  fi
fi

# Let the surface commit its first painted frame before we capture.
sleep "$PAINT_SETTLE"

# ---------------------------------------------------------------------------
# 5. Capture the compositor output with grim.
# ---------------------------------------------------------------------------
log "capturing compositor output with grim -> $OUT_PNG"
grim "$OUT_PNG"
[ -s "$OUT_PNG" ] || { err "grim produced an empty file"; exit 1; }
log "captured $(file -b "$OUT_PNG")"

# ---------------------------------------------------------------------------
# 6. ASSERT the PNG is the expected size AND non-blank (not one flat colour).
#    Prefer ImageMagick `identify` (baked into the CI runner image); fall back
#    to a pure-stdlib Python PNG analyzer so the script is self-contained and
#    runs anywhere python3 exists.
# ---------------------------------------------------------------------------
assert_png_nonblank() {
  local png="$1" w h ncolors
  if command -v identify >/dev/null 2>&1; then
    local dims
    dims="$(identify -format '%wx%h' "$png" 2>/dev/null || true)"
    w="${dims%x*}"; h="${dims#*x}"
    # %k = number of unique colours in the image.
    ncolors="$(identify -format '%k' "$png" 2>/dev/null || echo 0)"
    log "identify: ${dims}, unique-colors=$ncolors"
  else
    # Pure-stdlib fallback: decode the PNG (zlib IDAT + scanline unfilter) and
    # count distinct RGB(A) pixels. Emits "WxH ncolors".
    read -r w h ncolors < <(python3 - "$png" <<'PY'
import sys, zlib, struct
p = sys.argv[1]
data = open(p, "rb").read()
assert data[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
i = 8
width = height = bit_depth = color_type = None
idat = bytearray()
while i < len(data):
    (ln,) = struct.unpack(">I", data[i:i+4]); typ = data[i+4:i+8]
    chunk = data[i+8:i+8+ln]; i += 12 + ln
    if typ == b"IHDR":
        width, height, bit_depth, color_type = struct.unpack(">IIBB", chunk[:10])
    elif typ == b"IDAT":
        idat += chunk
    elif typ == b"IEND":
        break
channels = {0:1, 2:3, 3:1, 4:2, 6:4}[color_type]
assert bit_depth == 8, f"unexpected bit depth {bit_depth}"
bpp = channels
raw = zlib.decompress(bytes(idat))
stride = width * bpp
prev = bytearray(stride)
colors = set()
pos = 0
# Unfilter scanlines per the PNG spec (filter types 0-4).
for _ in range(height):
    ft = raw[pos]; pos += 1
    line = bytearray(raw[pos:pos+stride]); pos += stride
    if ft == 1:      # Sub
        for x in range(bpp, stride):
            line[x] = (line[x] + line[x-bpp]) & 0xFF
    elif ft == 2:    # Up
        for x in range(stride):
            line[x] = (line[x] + prev[x]) & 0xFF
    elif ft == 3:    # Average
        for x in range(stride):
            a = line[x-bpp] if x >= bpp else 0
            line[x] = (line[x] + ((a + prev[x]) >> 1)) & 0xFF
    elif ft == 4:    # Paeth
        for x in range(stride):
            a = line[x-bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x-bpp] if x >= bpp else 0
            pp = a + b - c
            pa = abs(pp - a); pb = abs(pp - b); pc = abs(pp - c)
            pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
            line[x] = (line[x] + pr) & 0xFF
    # Sample every 7th pixel per row into the colour set (fast; enough to
    # distinguish a flat buffer from painted UI). Early-out once clearly rich.
    for x in range(0, stride, bpp*7):
        colors.add(bytes(line[x:x+bpp]))
    prev = line
    if len(colors) > 4096:
        break
print(width, height, len(colors))
PY
)
    log "python png analyzer: ${w}x${h}, unique-colors(sampled)=$ncolors"
  fi

  # Size check.
  if [ "$w" != "$WIDTH" ] || [ "$h" != "$HEIGHT" ]; then
    err "capture is ${w}x${h}, expected ${WIDTH}x${HEIGHT}"; return 1
  fi
  # Non-blank check: a flat single-colour buffer has 1 unique colour; real
  # painted UI has thousands. Require a healthy margin above 1.
  if [ -z "$ncolors" ] || [ "$ncolors" -lt "$MIN_UNIQUE_COLORS" ] 2>/dev/null; then
    err "capture looks blank/flat: unique-colors=$ncolors (need >= $MIN_UNIQUE_COLORS) — Trailer did not paint real UI"
    return 1
  fi
  return 0
}

if assert_png_nonblank "$OUT_PNG"; then
  log "ASSERT PASS: capture is ${WIDTH}x${HEIGHT} and non-blank"
else
  err "non-blank assertion FAILED"
  exit 1
fi

log "ALL ASSERTS PASSED — Trailer renders on native Wayland. Screenshot: $OUT_PNG"
exit 0
