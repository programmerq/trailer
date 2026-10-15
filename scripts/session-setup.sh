#!/usr/bin/env bash
# Trailer build toolchain for Claude Code cloud sessions (Ubuntu 24.04, x86_64).
# Runs as the SessionStart hook (.claude/settings.json); a maintainer's own
# machine is skipped unless TRAILER_SETUP_FORCE=1.
#
# Only conda.anaconda.org and api.nuget.org are contacted, both reachable
# under the default cloud network policy (download.qt.io and GitHub release
# assets are not). Installs Qt 6.11 + QtPdf, qpdf, a matching GCC, cmake and
# ninja from conda-forge into one prefix, and the ONNX Runtime C++ SDK from
# Microsoft's nuget package. Idempotent: a re-run with everything present
# takes about a second. A clean install took 22 s on 2026-10-15.
#
# CI builds against Qt's official binaries instead (.github/actions); the
# Qt minor version matches.
set -euo pipefail

if [ "${CLAUDE_CODE_REMOTE:-}" != "true" ] && [ "${TRAILER_SETUP_FORCE:-}" != "1" ]; then
    echo "[trailer-setup] skipped (not a Claude Code cloud session; set TRAILER_SETUP_FORCE=1 to force)"
    exit 0
fi
if [ "$(uname -m)" != "x86_64" ]; then
    echo "[trailer-setup] unsupported architecture $(uname -m); skipping" >&2
    exit 0
fi

PREFIX="${TRAILER_TOOLCHAIN:-/opt/trailer-toolchain}"   # Qt, qpdf, cmake, ninja, gcc
ORT_VERSION=1.25.0
ORT_DIR="${TRAILER_ORT_DIR:-/opt/onnxruntime-${ORT_VERSION}}"
ORT_SHA256=c12a4e043c7ae1cee4ef99a347193eceabe2f6c828eb04f6d58ab21bcccddd8d
MM_DIR="${PREFIX}-micromamba"

start=$(date +%s)

# 1. micromamba (single static binary from conda-forge itself)
if [ ! -x "${MM_DIR}/bin/micromamba" ]; then
    mkdir -p "${MM_DIR}"
    curl -fsSL https://conda.anaconda.org/conda-forge/linux-64/micromamba-2.9.0-0.tar.bz2 \
        | tar -xj -C "${MM_DIR}" bin/micromamba
fi

# 2. Qt 6.11 (+ QtPdf, which conda-forge ships in qt6-webengine), qpdf,
#    a matching GCC, cmake, ninja. Everything links against this prefix's
#    own libstdc++, so use its compiler, not the system g++.
if [ ! -e "${PREFIX}/lib/cmake/Qt6Pdf/Qt6PdfConfig.cmake" ]; then
    "${MM_DIR}/bin/micromamba" create -y -q -r "${MM_DIR}/root" -p "${PREFIX}" -c conda-forge \
        qt6-main=6.11.2 qt6-webengine=6.11.2 qpdf cmake ninja cxx-compiler pkg-config
fi

# 3. ONNX Runtime C++ SDK, assembled from Microsoft's nuget package
#    (identical binary to the GitHub release tarball, SHA-256 pinned).
if [ ! -e "${ORT_DIR}/lib/cmake/onnxruntime/onnxruntimeConfig.cmake" ]; then
    tmp="$(mktemp)"
    curl -fsSL -o "${tmp}" \
        "https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime/${ORT_VERSION}/microsoft.ml.onnxruntime.${ORT_VERSION}.nupkg"
    echo "${ORT_SHA256}  ${tmp}" | sha256sum -c - >/dev/null
    mkdir -p "${ORT_DIR}/include" "${ORT_DIR}/lib/cmake/onnxruntime"
    python3 - "${tmp}" "${ORT_DIR}" "${ORT_VERSION}" <<'PY'
import os, shutil, sys, zipfile
nupkg, root, ver = sys.argv[1:4]
with zipfile.ZipFile(nupkg) as z:
    for n in z.namelist():
        if n.startswith("build/native/include/") and n.endswith(".h"):
            dst = os.path.join(root, "include", os.path.basename(n))
        elif n == "runtimes/linux-x64/native/libonnxruntime.so":
            dst = os.path.join(root, "lib", f"libonnxruntime.so.{ver}")
        else:
            continue
        with z.open(n) as s, open(dst, "wb") as d:
            shutil.copyfileobj(s, d)
PY
    rm -f "${tmp}"
    test -s "${ORT_DIR}/include/onnxruntime_cxx_api.h"
    ln -sf "libonnxruntime.so.${ORT_VERSION}" "${ORT_DIR}/lib/libonnxruntime.so.1"
    ln -sf libonnxruntime.so.1 "${ORT_DIR}/lib/libonnxruntime.so"
    cat > "${ORT_DIR}/lib/cmake/onnxruntime/onnxruntimeConfig.cmake" <<CFG
get_filename_component(_ort_root "\${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
if(NOT TARGET onnxruntime::onnxruntime)
  add_library(onnxruntime::onnxruntime SHARED IMPORTED)
  set_target_properties(onnxruntime::onnxruntime PROPERTIES
    IMPORTED_LOCATION "\${_ort_root}/lib/libonnxruntime.so.${ORT_VERSION}"
    IMPORTED_SONAME "libonnxruntime.so.1"
    INTERFACE_INCLUDE_DIRECTORIES "\${_ort_root}/include")
endif()
set(onnxruntime_FOUND TRUE)
CFG
    cat > "${ORT_DIR}/lib/cmake/onnxruntime/onnxruntimeConfigVersion.cmake" <<VER
set(PACKAGE_VERSION "${ORT_VERSION}")
if(PACKAGE_FIND_VERSION VERSION_GREATER PACKAGE_VERSION)
  set(PACKAGE_VERSION_COMPATIBLE FALSE)
else()
  set(PACKAGE_VERSION_COMPATIBLE TRUE)
endif()
VER
fi

# 4. Make a plain `cmake -S . -B build -G Ninja` pick all of this up.
cat > /etc/profile.d/trailer-toolchain.sh <<ENV
export PATH="${PREFIX}/bin:\$PATH"
export CC="${PREFIX}/bin/x86_64-conda-linux-gnu-gcc"
export CXX="${PREFIX}/bin/x86_64-conda-linux-gnu-g++"
export CMAKE_PREFIX_PATH="${PREFIX}:${ORT_DIR}"
export CMAKE_GENERATOR=Ninja
export QT_QPA_PLATFORM=offscreen
ENV
if [ -n "${CLAUDE_ENV_FILE:-}" ] && ! grep -qsF "${PREFIX}/bin" "${CLAUDE_ENV_FILE}"; then
    cat /etc/profile.d/trailer-toolchain.sh >> "${CLAUDE_ENV_FILE}"
fi

echo "[trailer-setup] ready in $(( $(date +%s) - start ))s: ${PREFIX}, ${ORT_DIR}"
