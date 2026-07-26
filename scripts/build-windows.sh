#!/usr/bin/env bash
# Cross-compile trailer.exe from Linux via mingw-w64.
#
# Usage:
#   scripts/build-windows.sh              # build image if needed, cross-compile inside it
#   scripts/build-windows.sh --rebuild    # force a clean image rebuild
#   scripts/build-windows.sh --in-container  # internal: do the build assuming we are already inside the container
#
# Output goes to $REPO_ROOT/build-windows/ (gitignored). That
# directory contains trailer.exe alongside every DLL the binary
# transitively loads (including onnxruntime.dll), plus the Qt
# `platforms/qwindows.dll` plugin. Zip it up and run on Windows.
#
# Invoked by GitHub Actions (.github/workflows/ci.yml's
# windows-cross-build job runs this with --in-container directly on the
# trailer-k8s runner; release.yml runs it inside the Docker image) and
# runnable manually on any Linux host with the mingw-w64 toolchain.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

IMAGE=trailer-windows
DOCKERFILE=docker/windows/Dockerfile
# The image is x86_64-only. On ARM hosts (e.g. Apple Silicon) Docker
# emulates via QEMU — the cross-compile still produces a 64-bit
# Windows binary, just slower. On x86_64 Linux hosts this flag is a
# no-op.
PLATFORM_ARG=(--platform=linux/amd64)
# Stage the cross build outside the mounted workspace so its
# CMakeCache.txt (which pins paths like /work/...) doesn't clash with
# the host's native build dirs on subsequent runs.
BUILD_DIR="${BUILD_DIR:-/tmp/trailer-build-win}"
# Staging dir for the final zippable artifact. Lives under the repo
# so the host can see it after the container exits.
OUT_DIR="${OUT_DIR:-$REPO_ROOT/build-windows}"

# Runtime DLL sources inside the container.
#
# Search order matters — we prefer the toolchain's runtime DLLs over
# Qt's bundled ones so there's a single ABI for libgcc_s / libstdc++
# matching what trailer.exe was compiled with. (This only matters if
# the mingw toolchain version differs from Qt's — in the matched
# setup docker/windows/Dockerfile produces, the two are
# interchangeable.)
#
# Ubuntu's mingw packages scatter runtime DLLs across
# /usr/lib/gcc/x86_64-w64-mingw32/<ver>-posix/ (libstdc++, libgcc)
# and /usr/x86_64-w64-mingw32/lib/ (libwinpthread). Other distros
# put them elsewhere. Resolve the paths via `g++ -print-file-name`
# so the script works on any distro whose mingw matches Qt's
# toolchain version.
resolve_mingw_dll_dir() {
    local dll=$1
    local path
    path=$(x86_64-w64-mingw32-g++ -print-file-name="$dll" 2>/dev/null || true)
    if [[ -z "$path" || "$path" == "$dll" || ! -f "$path" ]]; then
        return
    fi
    dirname "$path"
}

MINGW_DIRS=()
for dll in libstdc++-6.dll libwinpthread-1.dll libgcc_s_seh-1.dll zlib1.dll; do
    dir=$(resolve_mingw_dll_dir "$dll")
    if [[ -n "$dir" ]]; then
        # dedupe
        already=0
        for existing in "${MINGW_DIRS[@]}"; do
            if [[ "$existing" == "$dir" ]]; then already=1; break; fi
        done
        if [[ $already -eq 0 ]]; then MINGW_DIRS+=("$dir"); fi
    fi
done
# Ubuntu's libz-mingw-w64 drops zlib1.dll under
# /usr/x86_64-w64-mingw32/bin, which isn't always on g++'s
# -print-file-name search path. Add it as a fallback so collect_dlls
# can find zlib1.dll (which qpdf30.dll pulls in dynamically).
if [[ -d /usr/x86_64-w64-mingw32/bin ]]; then
    MINGW_DIRS+=(/usr/x86_64-w64-mingw32/bin)
fi

QT_BIN=${QT_DIR:-/opt/qt/6.10.3/mingw_64}/bin
QPDF_BIN=${QPDF_DIR:-/opt/qpdf}/bin
QT_PLUGINS_DIR=${QT_DIR:-/opt/qt/6.10.3/mingw_64}/plugins

# ONNX Runtime's onnxruntime.dll doesn't live in the mingw / Qt / qpdf
# dirs, so the import-table walk below can't source it from any of
# them. In CI/local the setup-windows-cross action assembles the NuGet
# win-x64 binaries at $ORT_WIN_DIR/lib; add that to the search list so
# collect_dlls finds onnxruntime.dll. (The Docker/release.yml path
# doesn't set ORT_WIN_DIR — there cmake/OnnxRuntime.cmake's
# FetchContent + trailer_deploy_onnxruntime POST_BUILD drops the DLL
# next to the built trailer.exe, which collect_dlls also searches via
# EXE_DIR below.) Without this, onnxruntime.dll was missing from the
# shipped dist/ tree — build/ had it via the POST_BUILD copy but the
# artifact did not, so trailer.exe wouldn't launch on a clean box.
ORT_BIN=${ORT_WIN_DIR:+${ORT_WIN_DIR}/lib}

# Directory the freshly built trailer.exe lives in, set by run_build.
# In every context the ORT deploy POST_BUILD copies onnxruntime.dll
# here, so it's the reliable fallback source when ORT_WIN_DIR is unset
# (the Docker image path).
EXE_DIR=""

collect_dlls() {
    # Recursively find every DLL trailer.exe (and the Qt platform
    # plugin) transitively loads, sourcing them from the directories
    # we know ship them. System Windows DLLs (KERNEL32, USER32, …)
    # aren't present in these dirs and are skipped — they're part of
    # Windows itself and must not be bundled.
    #
    # BFS over the import table via x86_64-w64-mingw32-objdump -p.
    local start=$1
    local dest=$2
    local -A seen=()
    local queue=("$start")
    while ((${#queue[@]})); do
        local current=${queue[0]}
        queue=("${queue[@]:1}")
        local dll
        while read -r dll; do
            if [[ -n "${seen[$dll]:-}" ]]; then continue; fi
            local candidate=""
            for src in "${MINGW_DIRS[@]}" "$QT_BIN" "$QPDF_BIN" ${ORT_BIN:+"$ORT_BIN"} ${EXE_DIR:+"$EXE_DIR"}; do
                if [[ -f "$src/$dll" ]]; then
                    candidate="$src/$dll"
                    break
                fi
            done
            if [[ -z "$candidate" ]]; then continue; fi
            seen[$dll]=1
            cp "$candidate" "$dest/"
            queue+=("$candidate")
        done < <(x86_64-w64-mingw32-objdump -p "$current" \
                     | awk '/DLL Name:/ {print $3}')
    done
}

run_build() {
    if ! command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1; then
        echo "x86_64-w64-mingw32-g++ not found — this script expects Arch's mingw-w64-gcc package." >&2
        echo "Run without --in-container to use the bundled Docker image." >&2
        exit 127
    fi
    if [[ ! -d "${QT_DIR:-/opt/qt/6.10.3/mingw_64}" ]]; then
        echo "Qt not found at ${QT_DIR:-/opt/qt/6.10.3/mingw_64}." >&2
        echo "The Dockerfile installs it via aqtinstall — rebuild with --rebuild." >&2
        exit 1
    fi

    rm -rf "$OUT_DIR"
    mkdir -p "$OUT_DIR"

    # Point CMake at Qt (aqtinstall install) and qpdf (prebuilt
    # release zip) explicitly — they aren't in the toolchain's
    # default sys-root. `cmake/toolchain-mingw-w64.cmake` pins the
    # compiler triplet. QT_HOST_PATH lets CMake run Linux-native moc
    # / rcc / uic while everything else compiles against the
    # win64_mingw Qt.
    if [[ ! -d "$BUILD_DIR" ]]; then
        cmake -S . -B "$BUILD_DIR" -G Ninja \
            -DCMAKE_TOOLCHAIN_FILE="$REPO_ROOT/cmake/toolchain-mingw-w64.cmake" \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_PREFIX_PATH="${QT_DIR:-/opt/qt/6.10.3/mingw_64};${QPDF_DIR:-/opt/qpdf}" \
            -DQT_HOST_PATH="${QT_HOST_DIR:-/opt/qt/6.10.3/gcc_64}" \
            -Dqpdf_DIR="${QPDF_DIR:-/opt/qpdf}/lib/cmake/qpdf"
    fi
    # Bare `-j` does NOT honor CMAKE_BUILD_PARALLEL_LEVEL — cmake only
    # reads that env var when `-j`/`--parallel` is absent from the
    # command line entirely; once present, cmake hands the native tool
    # (Ninja here) its own default. Invoked both by CI (which always
    # sets CMAKE_BUILD_PARALLEL_LEVEL at the workflow/job level) and by
    # a developer running this manually on their own Linux host, so
    # fall back to nproc (this script is Linux-only per its header).
    cmake --build "$BUILD_DIR" -j "${CMAKE_BUILD_PARALLEL_LEVEL:-$(nproc)}"

    local exe
    exe=$(find "$BUILD_DIR" -maxdepth 2 -name 'trailer.exe' -print -quit)
    if [[ -z "$exe" ]]; then
        echo "Could not locate trailer.exe under $BUILD_DIR" >&2
        exit 1
    fi
    # collect_dlls sources onnxruntime.dll from here when ORT_WIN_DIR
    # isn't set (the ORT deploy POST_BUILD copied it next to the exe).
    EXE_DIR=$(dirname "$exe")
    cp "$exe" "$OUT_DIR/"

    collect_dlls "$OUT_DIR/trailer.exe" "$OUT_DIR"

    # Qt discovers the platform plugin at runtime from
    # <exe_dir>/platforms/qwindows.dll. Copy it in and recurse
    # dependency collection so any Qt DLLs the plugin pulls in
    # (but the exe doesn't) are included too.
    mkdir -p "$OUT_DIR/platforms"
    cp "$QT_PLUGINS_DIR/platforms/qwindows.dll" "$OUT_DIR/platforms/"
    collect_dlls "$OUT_DIR/platforms/qwindows.dll" "$OUT_DIR"

    echo
    echo "Built: $OUT_DIR/trailer.exe"
    echo "Bundled $(find "$OUT_DIR" -name '*.dll' | wc -l) DLLs."
}

case "${1:-}" in
    --in-container)
        run_build
        ;;
    --rebuild)
        docker build "${PLATFORM_ARG[@]}" --no-cache -t "$IMAGE" -f "$DOCKERFILE" .
        exec "$0"  # run again without --rebuild
        ;;
    "")
        if ! command -v docker >/dev/null 2>&1; then
            echo "docker not found. Install Docker to use this script." >&2
            exit 127
        fi
        if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
            docker build "${PLATFORM_ARG[@]}" -t "$IMAGE" -f "$DOCKERFILE" .
        fi
        docker run "${PLATFORM_ARG[@]}" --rm -v "$PWD":/work -w /work "$IMAGE" \
            bash scripts/build-windows.sh --in-container
        ;;
    *)
        echo "Unknown argument: $1" >&2
        echo "See header of $0 for usage." >&2
        exit 2
        ;;
esac
