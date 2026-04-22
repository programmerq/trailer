# Trailer Windows cross-compile image
#
# Ubuntu 24.04 + Ubuntu's mingw-w64 13 toolchain + Qt's prebuilt
# Windows MinGW binaries (via aqtinstall) + qpdf's prebuilt mingw64
# release. Cross-compiles trailer.exe from a Linux host; no
# Qt-from-source build required.
#
# Not wired into GitHub Actions — run locally via
# scripts/build-windows.sh.
#
# Why aqtinstall: no Qt account required; pulls Qt's own prebuilt
# Windows/MinGW binaries (including the qtpdf module) directly. qpdf's
# GitHub releases include a prebuilt mingw64 zip with import lib and
# CMake config.
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

# qpdf: prebuilt mingw64 zip from github releases, carries
# libqpdf.a import lib + cmake config + runtime DLLs.
RUN wget -q https://github.com/qpdf/qpdf/releases/download/v${QPDF_VERSION}/qpdf-${QPDF_VERSION}-mingw64.zip \
        -O /tmp/qpdf.zip \
    && unzip -q /tmp/qpdf.zip -d /opt \
    && mv /opt/qpdf-${QPDF_VERSION}-mingw64 ${QPDF_DIR} \
    && rm /tmp/qpdf.zip

WORKDIR /work
