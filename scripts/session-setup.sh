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
#       Microsoft.ML.OnnxRuntime nupkg (api.nuget.org is allowed),
#       SHA-256-pinned, and add a minimal CMake package config so
#       OnnxRuntime.cmake takes its find_package() fast path and never
#       tries to download.
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
# Pinned: 3.3.0 validated with the linux 6.11 CDN layout. (It does NOT
# handle the 6.11 *Windows* layout — see scripts/install-windows-deps.ps1.)
AQT_VERSION=3.3.0
# ORT_VERSION is interpolated everywhere, including the generated CMake
# config below — a bump here stays consistent. Update ORT_NUPKG_SHA256
# alongside it (nupkgs are immutable on nuget.org).
ORT_VERSION=1.25.0
ORT_DIR="/opt/onnxruntime-${ORT_VERSION}"
ORT_NUPKG_URL="https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime/${ORT_VERSION}/microsoft.ml.onnxruntime.${ORT_VERSION}.nupkg"
ORT_NUPKG_SHA256=c12a4e043c7ae1cee4ef99a347193eceabe2f6c828eb04f6d58ab21bcccddd8d

# Only auto-run inside Claude Code web containers; a maintainer's local
# machine should not get /opt installs from a session hook. Override
# with TRAILER_SETUP_FORCE=1.
if [ "${CLAUDE_CODE_REMOTE:-}" != "true" ] && [ "${TRAILER_SETUP_FORCE:-}" != "1" ]; then
    echo "[trailer-setup] skipped (not a Claude Code web session; set TRAILER_SETUP_FORCE=1 to force)"
    exit 0
fi

# Everything below fetches x86_64-only binaries (Qt linux_gcc_64, ORT
# linux-x64); bail out early instead of failing at link time.
if [ "$(uname -m)" != "x86_64" ]; then
    echo "[trailer-setup] unsupported architecture '$(uname -m)': this script installs x86_64-only binaries (Qt linux_gcc_64, ONNX Runtime linux-x64); skipping" >&2
    exit 0
fi

# Root vs sudo: the web container runs us as root; elsewhere require
# passwordless sudo (probed with -n so we fail fast instead of hanging
# on a password prompt in a no-tty hook).
SUDO=""
if [ "$(id -u)" -ne 0 ]; then
    if command -v sudo >/dev/null 2>&1 && sudo -n true 2>/dev/null; then
        SUDO="sudo"
    else
        echo "[trailer-setup] error: not root and no passwordless sudo available; re-run as root or configure sudo" >&2
        exit 1
    fi
fi

# ---------------------------------------------------------------- apt
# Exact list validated 2026-07-09 (docker/uat/Dockerfile list + git for
# the tomlplusplus FetchContent clone + python3-pip for aqtinstall).
# dpkg-query guard (not `dpkg -s`, which passes for removed-but-not-
# purged packages) keeps the re-run fast; on a miss, try install
# straight off the cached index and only apt-get update on failure.
# sudo's env_reset strips a plain DEBIAN_FRONTEND export, hence `env`.
APT_PKGS=(
    build-essential ca-certificates cmake ninja-build g++ git pkg-config
    python3 python3-pip libglib2.0-0t64 libqpdf-dev
    libgl1-mesa-dev libglu1-mesa-dev libxkbcommon-dev
    libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-randr0
    libxcb-render-util0 libxcb-shape0 libxcb-sync1 libxcb-xfixes0
    libxcb-xinerama0 libxcb-xkb1 libxkbcommon-x11-0
    libfontconfig1 libdbus-1-3 libcups2-dev
)
apt_pkgs_installed() {
    local status
    status="$(dpkg-query -W -f='${Status}\n' "${APT_PKGS[@]}" 2>/dev/null)" || return 1
    ! grep -qvx 'install ok installed' <<<"${status}"
}
if ! apt_pkgs_installed; then
    $SUDO env DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends "${APT_PKGS[@]}" >/dev/null 2>&1 || {
        # Stale/absent package index (a third-party PPA on this image
        # errors during update; tolerate it — everything we need is in
        # Ubuntu main).
        $SUDO env DEBIAN_FRONTEND=noninteractive apt-get update -qq --allow-releaseinfo-change >/dev/null 2>&1 || true
        $SUDO env DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends "${APT_PKGS[@]}" >/dev/null
    }
fi

# --------------------------------------------------------- aqtinstall
# Install and import-check through $SUDO so pip's target site-packages
# is the same interpreter environment `$SUDO python3 -m aqt` runs in
# (a plain pip on a non-root run would land in the user site, invisible
# under sudo). Keep the --break-system-packages fallback for images
# where python3 is the PEP 668-managed distro interpreter.
if ! $SUDO python3 -c 'import aqt' >/dev/null 2>&1; then
    $SUDO python3 -m pip install -q "aqtinstall==${AQT_VERSION}" \
        || $SUDO python3 -m pip install -q --break-system-packages "aqtinstall==${AQT_VERSION}"
fi

# ----------------------------------------------------------------- Qt
# Guard on Qt6PdfConfig.cmake too, not just qmake: aqt extracts qtbase
# (which contains qmake) before the qtpdf module, so an install killed
# mid-way would otherwise pass the check forever. aqt re-extracts fine
# over a dirty target dir.
if [ ! -x "${QT_DIR}/bin/qmake" ] || [ ! -e "${QT_DIR}/lib/cmake/Qt6Pdf/Qt6PdfConfig.cmake" ]; then
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
    # ~130 MB; validated ~6 s through the session proxy. Pinned SHA-256
    # keeps the same reproducibility bar as cmake/OnnxRuntime.cmake's
    # canonical FetchContent path (set -e makes a mismatch fatal).
    curl -fsSL -o "${TMP_NUPKG}" "${ORT_NUPKG_URL}"
    echo "${ORT_NUPKG_SHA256}  ${TMP_NUPKG}" | sha256sum -c - >/dev/null
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
    # Fail fast if the nupkg layout ever changes: the extraction loop
    # above matches paths by prefix and silently extracts nothing on a
    # miss, which would otherwise surface much later as a compile or
    # link error instead of here.
    for expected in "${ORT_DIR}/include/onnxruntime_cxx_api.h" \
                    "${ORT_DIR}/lib/libonnxruntime.so.${ORT_VERSION}"; do
        if [ ! -s "${expected}" ]; then
            echo "[trailer-setup] error: '${expected}' missing/empty after extracting the ${ORT_VERSION} nupkg — did its internal layout change? (${ORT_NUPKG_URL})" >&2
            exit 1
        fi
    done
    $SUDO chmod 755 "${ORT_DIR}/lib/libonnxruntime.so.${ORT_VERSION}"
    # SONAME of the MS build is libonnxruntime.so.1; provide the usual
    # symlink chain so both link-time (-lonnxruntime) and runtime work.
    $SUDO ln -sf "libonnxruntime.so.${ORT_VERSION}" "${ORT_DIR}/lib/libonnxruntime.so.1"
    $SUDO ln -sf "libonnxruntime.so.1" "${ORT_DIR}/lib/libonnxruntime.so"
    # Unquoted heredocs so ${ORT_VERSION} interpolates; CMake's own
    # ${...} references are backslash-escaped.
    $SUDO tee "${ORT_DIR}/lib/cmake/onnxruntime/onnxruntimeConfig.cmake" >/dev/null <<CFGEOF
# Minimal package config for the ONNX Runtime SDK assembled from the
# official Microsoft.ML.OnnxRuntime nupkg (same MS-built linux-x64
# binary as the GitHub release tarball, which the egress proxy blocks).
# Lets Trailer's cmake/OnnxRuntime.cmake take its "already-installed
# SDK" fast path via find_package(onnxruntime CONFIG).
get_filename_component(_ort_root "\${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
if(NOT TARGET onnxruntime::onnxruntime)
  add_library(onnxruntime::onnxruntime SHARED IMPORTED)
  set_target_properties(onnxruntime::onnxruntime PROPERTIES
    IMPORTED_LOCATION "\${_ort_root}/lib/libonnxruntime.so.${ORT_VERSION}"
    IMPORTED_SONAME "libonnxruntime.so.1"
    INTERFACE_INCLUDE_DIRECTORIES "\${_ort_root}/include")
endif()
set(onnxruntime_FOUND TRUE)
unset(_ort_root)
CFGEOF
    $SUDO tee "${ORT_DIR}/lib/cmake/onnxruntime/onnxruntimeConfigVersion.cmake" >/dev/null <<VEREOF
set(PACKAGE_VERSION "${ORT_VERSION}")
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
# the same env for this session via the hook-provided env file. The
# hook fires on startup/resume/clear/compact, so guard on a versioned
# begin-marker: an unchanged version is a no-op (file never grows),
# while a Qt/ORT version bump drops the stale block and writes a fresh
# one instead of leaving old paths in CMAKE_PREFIX_PATH.
TRAILER_ENV_BEGIN="# trailer-setup: build env begin (qt ${QT_VERSION}, ort ${ORT_VERSION})"
TRAILER_ENV_END="# trailer-setup: build env end"
if [ -n "${CLAUDE_ENV_FILE:-}" ] && ! grep -qsxF "${TRAILER_ENV_BEGIN}" "${CLAUDE_ENV_FILE}"; then
    if [ -f "${CLAUDE_ENV_FILE}" ]; then
        # Strip any previous trailer-setup block: the begin/end form
        # (other versions) and the legacy single-marker form (marker
        # line + its export lines) written by earlier revisions.
        TMP_ENV="$(mktemp)"
        awk '
            /^# trailer-setup: build env begin / { skip = 1; next }
            skip && /^# trailer-setup: build env end$/ { skip = 0; next }
            skip { next }
            /^# trailer-setup: build env$/ { legacy = 3; next }
            legacy > 0 && /^export (CMAKE_PREFIX_PATH|PATH|QT_QPA_PLATFORM)=/ { legacy--; next }
            { legacy = 0; print }
        ' "${CLAUDE_ENV_FILE}" > "${TMP_ENV}"
        cat "${TMP_ENV}" > "${CLAUDE_ENV_FILE}"
        rm -f "${TMP_ENV}"
    fi
    {
        echo "${TRAILER_ENV_BEGIN}"
        echo "export CMAKE_PREFIX_PATH=\"${TRAILER_PREFIX_PATH}\${CMAKE_PREFIX_PATH:+:\$CMAKE_PREFIX_PATH}\""
        echo "export PATH=\"${QT_DIR}/bin:\$PATH\""
        echo "export QT_QPA_PLATFORM=offscreen"
        echo "${TRAILER_ENV_END}"
    } >> "${CLAUDE_ENV_FILE}"
fi

echo "[trailer-setup] OK in $(( $(date +%s) - START ))s: Qt ${QT_VERSION} (${QT_DIR}), onnxruntime ${ORT_VERSION} (${ORT_DIR}), apt deps present — configure with: cmake -S . -B build -G Ninja"
