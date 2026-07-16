#!/usr/bin/env bash
# Shared Qt-runtime bundling for the .deb and .rpm packagers.
#
# Qt 6.11 (with qtpdf) is not available in the target distros' repos, so both
# the .deb and the .rpm ship a self-contained /opt/trailer bundle: the non-system
# shared libraries the binary transitively needs, the xcb platform plugin, an
# RPATH pointing at the bundled libs, and a thin launcher wrapper that sets
# QT_PLUGIN_PATH. This is the single implementation both packagers call so the
# two package formats stay in lockstep.
#
# Usage: bundle-qt-runtime.sh <staging_root> <qt_prefix> [lib_search_path]
#
#   <staging_root>    DESTDIR-style staging tree; <staging_root>/usr/bin/trailer
#                     must already exist (installed by `cmake --install`).
#   <qt_prefix>       Qt install prefix (for the platforms/libqxcb.so plugin).
#   [lib_search_path] Colon-separated dirs added to LD_LIBRARY_PATH while ldd
#                     resolves the dependency graph. The cmake-installed binary
#                     carries a relative RUNPATH ($ORIGIN/../lib), so ldd cannot
#                     locate the out-of-tree Qt/onnxruntime libs without this.
#                     Defaults to "<qt_prefix>/lib".
#
# Result (all paths relative to <staging_root>):
#   opt/trailer/lib/*.so*                     bundled non-system libs
#   opt/trailer/plugins/platforms/libqxcb.so  Qt xcb platform plugin (+ its libs)
#   opt/trailer/bin/trailer                   the real binary, RPATH=/opt/trailer/lib
#   usr/bin/trailer                           launcher wrapper (sets QT_PLUGIN_PATH)

set -euo pipefail

STAGING="${1:?usage: bundle-qt-runtime.sh <staging_root> <qt_prefix> [lib_search_path]}"
QT_PREFIX="${2:?usage: bundle-qt-runtime.sh <staging_root> <qt_prefix> [lib_search_path]}"
LIB_SEARCH_PATH="${3:-$QT_PREFIX/lib}"

TRAILER_BIN="$STAGING/usr/bin/trailer"
BUNDLE_LIB="$STAGING/opt/trailer/lib"

if [[ ! -f "$TRAILER_BIN" ]]; then
    echo "ERROR: $TRAILER_BIN not found — run 'cmake --install' first." >&2
    exit 1
fi

echo "==> Bundling Qt libs into /opt/trailer/lib/"
mkdir -p "$BUNDLE_LIB"

# Copy non-system shared libraries (those not under /lib or /usr/lib) that a
# given binary transitively needs. Iterative BFS over the ldd dependency graph;
# $BUNDLE_LIB doubles as the seen-set via the -f check.
bundle_libs() {
    local -a queue=("$1")
    while (( ${#queue[@]} )); do
        local current="${queue[0]}"
        queue=("${queue[@]:1}")
        local lib
        while IFS= read -r lib; do
            [[ -f "$lib" ]] || continue
            local name
            name="$(basename "$lib")"
            [[ -f "$BUNDLE_LIB/$name" ]] && continue
            cp "$lib" "$BUNDLE_LIB/$name"
            queue+=("$lib")
        done < <(LD_LIBRARY_PATH="$LIB_SEARCH_PATH${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
            ldd "$current" 2>/dev/null \
            | awk '/=>/ { print $3 }' \
            | grep -Ev '^(/lib|/usr/lib|not$)')
    done
}

bundle_libs "$TRAILER_BIN"

# libqxcb.so is loaded at runtime via QT_PLUGIN_PATH, not linked directly, so
# ldd on the main binary won't find it; add it (and its private deps) explicitly.
QT_PLATFORM_PLUGIN="$QT_PREFIX/plugins/platforms/libqxcb.so"
if [[ -f "$QT_PLATFORM_PLUGIN" ]]; then
    mkdir -p "$STAGING/opt/trailer/plugins/platforms"
    cp "$QT_PLATFORM_PLUGIN" "$STAGING/opt/trailer/plugins/platforms/"
    bundle_libs "$QT_PLATFORM_PLUGIN"
else
    echo "WARNING: $QT_PLATFORM_PLUGIN not found — bundle will lack the xcb platform plugin." >&2
fi

# Patch RPATH before moving the binary so the installed binary resolves bundled
# libs without requiring LD_LIBRARY_PATH.
if command -v patchelf >/dev/null 2>&1; then
    patchelf --set-rpath '/opt/trailer/lib' "$TRAILER_BIN"
else
    echo "ERROR: patchelf not found — cannot set RPATH for the bundled binary." >&2
    exit 1
fi

# Replace the installed binary with a thin wrapper that sets QT_PLUGIN_PATH so
# Qt discovers the bundled platform plugin at runtime.
REAL_BIN="$STAGING/opt/trailer/bin/trailer"
mkdir -p "$STAGING/opt/trailer/bin"
mv "$TRAILER_BIN" "$REAL_BIN"
cat > "$TRAILER_BIN" <<'WRAP'
#!/bin/sh
export QT_PLUGIN_PATH=/opt/trailer/plugins${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}
export LD_LIBRARY_PATH="/opt/trailer/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec /opt/trailer/bin/trailer "$@"
WRAP
chmod 755 "$TRAILER_BIN" "$REAL_BIN"

echo "==> Bundled $(find "$BUNDLE_LIB" -type f | wc -l) libs into /opt/trailer/lib/"
