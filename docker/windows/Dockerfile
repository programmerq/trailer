# Trailer Windows cross-compile image
#
# Ubuntu 24.04 + Ubuntu's mingw-w64 13 toolchain + Qt's prebuilt
# Windows MinGW binaries (via aqtinstall) + qpdf built from source.
# Cross-compiles trailer.exe from a Linux host; no Qt-from-source
# build required.
#
# Not wired into GitHub Actions — run locally via
# scripts/build-windows.sh.
#
# Why aqtinstall: no Qt account required; pulls Qt's own prebuilt
# Windows/MinGW binaries (including the qtpdf module) directly.
#
# Why build qpdf from source (rather than use the prebuilt mingw64
# zip from qpdf's releases): the upstream prebuilt is built via
# MSYS2's rolling mingw, whose libstdc++ ABI doesn't match
# Ubuntu's mingw 13 — same class of Entry-Point-Not-Found failure
# we hit with Qt (see the Qt note below). Building in-image
# against the same toolchain Qt was built with keeps the whole
# C++ ABI chain consistent.
#
# Why Ubuntu 24.04 (and not Arch, Debian 12, or Debian 13):
# Qt's win64_mingw binaries for Qt 6.10.x are built with MinGW-w64
# GCC 13.1. The libstdc++ ABI — specifically the mangled names of
# template methods like `basic_streambuf::seekpos` — drifts between
# GCC major versions, so linking against Qt's import libs with a
# mismatched mingw produces an .exe whose import table references
# symbols Qt6Pdf.dll doesn't export. Ubuntu 24.04 ships
# gcc-mingw-w64-x86-64-posix at 13.2, the closest apt-available
# match. Arch (15.2), Debian 13 (14), and Debian 12 (12) all break
# this way with a runtime "Entry Point Not Found" on `seekpos`.
#
# Build once:
#   docker build -t trailer-windows -f docker/windows/Dockerfile .
#
# Run (usually via scripts/build-windows.sh):
#   docker run --rm -v "$PWD":/work -w /work trailer-windows \
#       bash scripts/build-windows.sh --in-container
#
# On Apple Silicon / ARM Linux hosts, pull the image with
# --platform=linux/amd64 (Docker emulates x86_64 via QEMU).

FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive

# Pinned versions. Bump Qt when the target Qt release moves; qpdf
# evolves independently of Qt.
#
# IMPORTANT: if you bump QT_VERSION, verify Qt's release notes for
# the matching MinGW toolchain version and confirm Ubuntu's
# gcc-mingw-w64 still matches. A mismatched pair produces runtime
# "Entry Point Not Found" failures on Windows (see the ABI note at
# the top of this file).
ARG QT_VERSION=6.10.3
ARG QT_TARGET_ARCH=win64_mingw
ARG QT_HOST_ARCH=linux_gcc_64
# Qt install tree is nested under <QT_ROOT>/<version>/<arch>/...
ENV QT_DIR=/opt/qt/${QT_VERSION}/mingw_64
# QT_HOST_PATH points CMake at the Linux-native Qt host tools
# (moc, rcc, uic). Required when cross-compiling because the
# win64_mingw install's tools are .exe files we can't execute.
ENV QT_HOST_DIR=/opt/qt/${QT_VERSION}/gcc_64
ARG QPDF_VERSION=12.3.2
ENV QPDF_DIR=/opt/qpdf

# Build deps + mingw-w64 13.2 (posix threading — required for
# std::thread support, which Qt uses).
#
# libglib2.0-0t64 is a runtime dependency of Qt's Linux host CLI
# tools (moc / rcc / uic from the aqtinstall linux_gcc_64 package).
# Without it, rcc fails with "cannot open shared object file:
# libglib-2.0.so.0" during CMake autorcc.
#
# AllowInsecureRepositories / --allow-unauthenticated: under QEMU
# user-mode emulation on Apple Silicon, apt's gpgv signature
# verification of Ubuntu's InRelease files fails reproducibly
# ("At least one invalid signature was encountered") because of a
# syscall-translation quirk in emulated reads. Bypassing signature
# checks is safe here: the image is a local dev-only build sandbox,
# it doesn't ship artifacts, and the repos we pull from are still
# the official Ubuntu mirrors over HTTPS. On native x86_64 Linux
# hosts the flag is a no-op — apt only consults it when a signature
# check would otherwise abort.
RUN apt-get -o Acquire::AllowInsecureRepositories=true update \
    && apt-get install -y --allow-unauthenticated --no-install-recommends \
        build-essential \
        cmake \
        ninja-build \
        git \
        wget \
        unzip \
        p7zip-full \
        python3 \
        python3-venv \
        python3-pip \
        ca-certificates \
        libglib2.0-0t64 \
        gcc-mingw-w64-x86-64-posix \
        g++-mingw-w64-x86-64-posix \
        binutils-mingw-w64-x86-64 \
        libz-mingw-w64-dev \
        libz-mingw-w64 \
    && rm -rf /var/lib/apt/lists/*

# Ubuntu ships both -posix and -win32 variants and defaults to
# -win32. Select posix as the default for the unversioned mingw
# toolchain names CMake's toolchain file uses.
RUN update-alternatives --set x86_64-w64-mingw32-gcc /usr/bin/x86_64-w64-mingw32-gcc-posix \
    && update-alternatives --set x86_64-w64-mingw32-g++ /usr/bin/x86_64-w64-mingw32-g++-posix

# aqtinstall in a venv (Ubuntu 24.04's system pip refuses
# --break-system-packages installs without it).
RUN python3 -m venv /opt/aqt-venv \
    && /opt/aqt-venv/bin/pip install --no-cache-dir aqtinstall
ENV PATH=/opt/aqt-venv/bin:$PATH

# Target: Windows MinGW Qt (what trailer.exe actually links against).
RUN aqt install-qt windows desktop ${QT_VERSION} ${QT_TARGET_ARCH} -m qtpdf -O /opt/qt

# Host: Linux-native Qt of the same version. CMake picks up moc /
# rcc / uic / qmltyperegistrar from here via QT_HOST_PATH at
# configure time — the target Qt's equivalents are .exe files we
# can't run on Linux.
RUN aqt install-qt linux desktop ${QT_VERSION} ${QT_HOST_ARCH} -O /opt/qt

# Inline CMake toolchain file for cross-compiling dependencies
# from source inside this image. Kept separate from the repo's
# cmake/toolchain-mingw-w64.cmake because the host toolchain file
# is mounted at build time from /work; these in-image deps build
# before the workspace is mounted.
RUN printf '%s\n' \
        'set(CMAKE_SYSTEM_NAME Windows)' \
        'set(CMAKE_SYSTEM_PROCESSOR x86_64)' \
        'set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)' \
        'set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)' \
        'set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)' \
        'set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32 /opt/mingw-prefix)' \
        'set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)' \
        'set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)' \
        'set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)' \
        'set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)' \
        > /opt/mingw-toolchain.cmake \
    && mkdir -p /opt/mingw-prefix

# libjpeg-turbo from source — there's no mingw-w64 apt package for
# it, and qpdf needs it for JPEG handling in PDFs. Built static so
# the result gets folded into qpdf30.dll at link time and we don't
# ship another runtime DLL.
ARG LIBJPEG_VERSION=3.0.3
RUN wget -q https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/${LIBJPEG_VERSION}/libjpeg-turbo-${LIBJPEG_VERSION}.tar.gz \
        -O /tmp/jpeg.tar.gz \
    && tar -xf /tmp/jpeg.tar.gz -C /tmp \
    && cmake -S /tmp/libjpeg-turbo-${LIBJPEG_VERSION} -B /tmp/jpeg-build -G Ninja \
         -DCMAKE_TOOLCHAIN_FILE=/opt/mingw-toolchain.cmake \
         -DCMAKE_INSTALL_PREFIX=/opt/mingw-prefix \
         -DCMAKE_BUILD_TYPE=Release \
         -DENABLE_SHARED=OFF \
         -DENABLE_STATIC=ON \
         -DWITH_TURBOJPEG=OFF \
    && cmake --build /tmp/jpeg-build -j \
    && cmake --install /tmp/jpeg-build \
    && rm -rf /tmp/libjpeg-turbo-${LIBJPEG_VERSION} /tmp/jpeg.tar.gz /tmp/jpeg-build

# qpdf from source, replacing the upstream prebuilt mingw64 zip.
# The prebuilt is built via MSYS2's rolling mingw, whose libstdc++
# ABI doesn't match Ubuntu's mingw 13 — at runtime on Windows,
# trailer.exe's import table references mangled symbols qpdf30.dll
# (as prebuilt) doesn't export, producing the same
# "Entry Point Not Found" class of error we saw with Qt. Building
# qpdf from source against our toolchain keeps all three C++
# artefacts (Qt, qpdf, trailer) on the same ABI.
#
# Skip external crypto backends: qpdf's built-in "implicit" crypto
# (USE_IMPLICIT_CRYPTO=1, its default) is sufficient for Trailer's
# PDF operations, and we avoid pulling openssl/gnutls into the
# image. REQUIRE_* stay off so CMake doesn't error when those
# libraries aren't available.
#
# CRT_glob.o hack: qpdf/qpdf/CMakeLists.txt references CRT_glob.o
# as a bare filename to enable POSIX-style argv globbing in the
# qpdf.exe CLI on Windows. The linker resolves that relative to
# the build dir, where it doesn't exist — the .o lives under
# mingw's sysroot. Copy it in after configure so the link step
# finds it. (Upstream qpdf builds work around this with MSYS2's
# lib layout, which does put CRT_glob.o in a default search path.)
#
# MINGW-block patch: qpdf/qpdf/CMakeLists.txt has an `if(MINGW)`
# block that runs pwsh.exe + copy_dlls.ps1 to bundle runtime DLLs
# into extra-dlls/, which is then installed alongside qpdf.exe.
# pwsh isn't available in this image and the bundling is redundant
# — scripts/build-windows.sh walks the import table itself. Sed the
# condition to FALSE to skip the whole block (pwsh invocation,
# post-build copy, and the extra-dlls install directive that would
# otherwise fail on its missing output).
RUN wget -q https://github.com/qpdf/qpdf/releases/download/v${QPDF_VERSION}/qpdf-${QPDF_VERSION}.tar.gz \
        -O /tmp/qpdf.tar.gz \
    && tar -xf /tmp/qpdf.tar.gz -C /tmp \
    && sed -i 's/^if(MINGW)$/if(FALSE AND MINGW) # trailer: skip pwsh-based extra-dlls packaging; we bundle in build-windows.sh/' \
         /tmp/qpdf-${QPDF_VERSION}/qpdf/CMakeLists.txt \
    && cmake -S /tmp/qpdf-${QPDF_VERSION} -B /tmp/qpdf-build -G Ninja \
         -DCMAKE_TOOLCHAIN_FILE=/opt/mingw-toolchain.cmake \
         -DCMAKE_INSTALL_PREFIX=${QPDF_DIR} \
         -DCMAKE_PREFIX_PATH=/opt/mingw-prefix \
         -DCMAKE_BUILD_TYPE=Release \
         -DBUILD_STATIC_LIBS=OFF \
         -DREQUIRE_CRYPTO_GNUTLS=0 \
         -DREQUIRE_CRYPTO_OPENSSL=0 \
         -DUSE_IMPLICIT_CRYPTO=1 \
         -DBUILD_DOC=0 \
    && cp /usr/x86_64-w64-mingw32/lib/CRT_glob.o /tmp/qpdf-build/CRT_glob.o \
    && cmake --build /tmp/qpdf-build -j \
    && cmake --install /tmp/qpdf-build \
    && rm -rf /tmp/qpdf-${QPDF_VERSION} /tmp/qpdf.tar.gz /tmp/qpdf-build

WORKDIR /work
