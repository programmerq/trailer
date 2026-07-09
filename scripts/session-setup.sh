#!/usr/bin/env bash
# Trailer — Claude Code web session setup (Ubuntu 24.04 / noble, x86_64).
#
# Installs everything `cmake -S . -B build` needs in a fresh remote
# session container:
#   * Qt 6.11.0 (aqtinstall, arch linux_gcc_64, module qtpdf) -> /opt/qt
#     - apt's Qt on noble is 6.4.2; the code needs >= 6.6
#       (QPdfView::setCurrentSearchResultIndex). 6.11.0 matches CI
#       (.github/actions/setup-linux-build default).
#   * apt build deps (list mirrors docker/uat/Dockerfile + CI action).
#   * ONNX Runtime 1.25.0 C++ SDK -> /opt/onnxruntime-1.25.0
#     - cmake/OnnxRuntime.cmake normally FetchContent-downloads the
#       GitHub release tarball, but the session egress proxy blocks
#       github.com release-asset URLs (403). We assemble the identical
#       official MS linux-x64 binary + headers from the
#       Microsoft.ML.OnnxRuntime nupkg (api.nuget.org is allowed) and
#       add a minimal CMake package config so OnnxRuntime.cmake takes
#       its find_package() fast path and never tries to download.
#   * /etc/profile.d/trailer-qt.sh + $CLAUDE_ENV_FILE exporting
#     CMAKE_PREFIX_PATH so a plain `cmake -S . -B build -G Ninja` works
#     with no -D flags.
#
# Idempotent: every step is guarded; a re-run when everything is
# present completes in ~a second. Run tests with:
#   QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
set -euo pipefail

START=$(date +%s)

QT_VERSION=6.11.0
QT_BASE=/opt/qt
QT_DIR="${QT_BASE}/${QT_VERSION}/gcc_64"   # aqt arch linux_gcc_64 installs into gcc_64/
ORT_VERSION=1.25.0
ORT_DIR="/opt/onnxruntime-${ORT_VERSION}"
ORT_NUPKG_URL="https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime/${ORT_VERSION}/microsoft.ml.onnxruntime.${ORT_VERSION}.nupkg"

# Only auto-run inside Claude Code web containers; a maintainer's local
# machine should not get /opt installs from a session hook. Override
# with TRAILER_SETUP_FORCE=1.
if [ "${CLAUDE_CODE_REMOTE:-}" != "true" ] && [ "${TRAILER_SETUP_FORCE:-}" != "1" ]; then
    echo "[trailer-setup] skipped (not a Claude Code web session; set TRAILER_SETUP_FORCE=1 to force)"
    exit 0
fi

# Root vs sudo: the web container runs us as root; elsewhere fall back
# to sudo for the privileged bits (apt, /opt, /etc/profile.d).
SUDO=""
if [ "$(id -u)" -ne 0 ]; then
    SUDO="sudo"
fi

# ---------------------------------------------------------------- apt
# Exact list validated 2026-07-09 (docker/uat/Dockerfile list + git for
# the tomlplusplus FetchContent clone). dpkg -s guard keeps the re-run
# fast; on a miss, try install straight off the cached index and only
# apt-get update on failure.
APT_PKGS=(
    build-essential ca-certificates cmake ninja-build g++ git pkg-config
    python3 libglib2.0-0t64 libqpdf-dev
    libgl1-mesa-dev libglu1-mesa-dev libxkbcommon-dev
    libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-randr0
    libxcb-render-util0 libxcb-shape0 libxcb-sync1 libxcb-xfixes0
    libxcb-xinerama0 libxcb-xkb1 libxkbcommon-x11-0
    libfontconfig1 libdbus-1-3 libcups2-dev
)
if ! dpkg -s "${APT_PKGS[@]}" >/dev/null 2>&1; then
    export DEBIAN_FRONTEND=noninteractive
    $SUDO apt-get install -y -qq --no-install-recommends "${APT_PKGS[@]}" >/dev/null 2>&1 || {
        # Stale/absent package index (a third-party PPA on this image
        # errors during update; tolerate it — everything we need is in
        # Ubuntu main).
        $SUDO apt-get update -qq --allow-releaseinfo-change >/dev/null 2>&1 || true
        $SUDO apt-get install -y -qq --no-install-recommends "${APT_PKGS[@]}" >/dev/null
    }
fi

# --------------------------------------------------------- aqtinstall
# python3 here is a /usr/local CPython (3.11), so plain pip works; keep
# the --break-system-packages fallback for images where python3 is the
# PEP 668-managed distro interpreter.
if ! python3 -c 'import aqt' >/dev/null 2>&1; then
    python3 -m pip install -q aqtinstall \
        || python3 -m pip install -q --break-system-packages aqtinstall
fi

# ----------------------------------------------------------------- Qt
if [ ! -x "${QT_DIR}/bin/qmake" ]; then
    $SUDO mkdir -p "${QT_BASE}"
    # Validated: ~30 s, ~1.5 GB installed. Arch is linux_gcc_64 for
    # Qt >= 6.8; qtsvg ships in the base desktop install, qtpdf is the
    # only extra module Trailer needs.
    $SUDO python3 -m aqt install-qt linux desktop "${QT_VERSION}" linux_gcc_64 -m qtpdf -O "${QT_BASE}"
fi

# --------------------------------------------------- ONNX Runtime SDK
if [ ! -e "${ORT_DIR}/include/onnxruntime_cxx_api.h" ] \
   || [ ! -e "${ORT_DIR}/lib/libonnxruntime.so" ] \
   || [ ! -e "${ORT_DIR}/lib/cmake/onnxruntime/onnxruntimeConfig.cmake" ]; then
    $SUDO mkdir -p "${ORT_DIR}/include" "${ORT_DIR}/lib/cmake/onnxruntime"
    TMP_NUPKG="$(mktemp /tmp/ort-nupkg.XXXXXX)"
    trap 'rm -f "${TMP_NUPKG}"' EXIT
    # ~130 MB; validated ~6 s through the session proxy.
    curl -fsSL -o "${TMP_NUPKG}" "${ORT_NUPKG_URL}"
    $SUDO env TMP_NUPKG="${TMP_NUPKG}" ORT_DIR="${ORT_DIR}" ORT_VERSION="${ORT_VERSION}" python3 - <<'PYEOF'
import os, shutil, zipfile
nupkg, root, ver = os.environ["TMP_NUPKG"], os.environ["ORT_DIR"], os.environ["ORT_VERSION"]
z = zipfile.ZipFile(nupkg)
for n in z.namelist():
    if n.startswith("build/native/include/") and n.endswith(".h"):
        with z.open(n) as src, open(os.path.join(root, "include", os.path.basename(n)), "wb") as dst:
            shutil.copyfileobj(src, dst)
    if n == "runtimes/linux-x64/native/libonnxruntime.so":
        with z.open(n) as src, open(os.path.join(root, "lib", f"libonnxruntime.so.{ver}"), "wb") as dst:
            shutil.copyfileobj(src, dst)
print("extracted onnxruntime", ver)
PYEOF
    $SUDO chmod 755 "${ORT_DIR}/lib/libonnxruntime.so.${ORT_VERSION}"
    # SONAME of the MS build is libonnxruntime.so.1; provide the usual
    # symlink chain so both link-time (-lonnxruntime) and runtime work.
    $SUDO ln -sf "libonnxruntime.so.${ORT_VERSION}" "${ORT_DIR}/lib/libonnxruntime.so.1"
    $SUDO ln -sf "libonnxruntime.so.1" "${ORT_DIR}/lib/libonnxruntime.so"
    $SUDO tee "${ORT_DIR}/lib/cmake/onnxruntime/onnxruntimeConfig.cmake" >/dev/null <<'CFGEOF'
# Minimal package config for the ONNX Runtime SDK assembled from the
# official Microsoft.ML.OnnxRuntime nupkg (same MS-built linux-x64
# binary as the GitHub release tarball, which the egress proxy blocks).
# Lets Trailer's cmake/OnnxRuntime.cmake take its "already-installed
# SDK" fast path via find_package(onnxruntime CONFIG).
get_filename_component(_ort_root "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
if(NOT TARGET onnxruntime::onnxruntime)
  add_library(onnxruntime::onnxruntime SHARED IMPORTED)
  set_target_properties(onnxruntime::onnxruntime PROPERTIES
    IMPORTED_LOCATION "${_ort_root}/lib/libonnxruntime.so.1.25.0"
    IMPORTED_SONAME "libonnxruntime.so.1"
    INTERFACE_INCLUDE_DIRECTORIES "${_ort_root}/include")
endif()
set(onnxruntime_FOUND TRUE)
unset(_ort_root)
CFGEOF
    $SUDO tee "${ORT_DIR}/lib/cmake/onnxruntime/onnxruntimeConfigVersion.cmake" >/dev/null <<'VEREOF'
set(PACKAGE_VERSION "1.25.0")
if(PACKAGE_FIND_VERSION VERSION_GREATER PACKAGE_VERSION)
  set(PACKAGE_VERSION_COMPATIBLE FALSE)
else()
  set(PACKAGE_VERSION_COMPATIBLE TRUE)
endif()
if(PACKAGE_FIND_VERSION STREQUAL PACKAGE_VERSION)
  set(PACKAGE_VERSION_EXACT TRUE)
endif()
VEREOF
    rm -f "${TMP_NUPKG}"
    trap - EXIT
fi

# ------------------------------------------------------- environment
TRAILER_PREFIX_PATH="${QT_DIR}:${ORT_DIR}"
$SUDO tee /etc/profile.d/trailer-qt.sh >/dev/null <<PROFEOF
# Written by scripts/session-setup.sh — Trailer build environment.
export CMAKE_PREFIX_PATH="${TRAILER_PREFIX_PATH}\${CMAKE_PREFIX_PATH:+:\$CMAKE_PREFIX_PATH}"
export PATH="${QT_DIR}/bin:\$PATH"
PROFEOF
# Claude Code sessions don't source profile.d for tool shells; persist
# the same env for this session via the hook-provided env file.
if [ -n "${CLAUDE_ENV_FILE:-}" ]; then
    {
        echo "export CMAKE_PREFIX_PATH=\"${TRAILER_PREFIX_PATH}\""
        echo "export PATH=\"${QT_DIR}/bin:\$PATH\""
        echo "export QT_QPA_PLATFORM=offscreen"
    } >> "${CLAUDE_ENV_FILE}"
fi

echo "[trailer-setup] OK in $(( $(date +%s) - START ))s: Qt ${QT_VERSION} (${QT_DIR}), onnxruntime ${ORT_VERSION} (${ORT_DIR}), apt deps present — configure with: cmake -S . -B build -G Ninja"
