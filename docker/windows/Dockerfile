# Trailer Windows cross-compile image
#
# Arch Linux + Arch's mingw-w64-gcc toolchain + Qt's prebuilt Windows
# MinGW binaries (via aqtinstall) + qpdf's prebuilt mingw64 release.
# Cross-compiles trailer.exe from a Linux host; no Qt-from-source
# build required.
#
# Not wired into GitHub Actions — run locally via
# scripts/build-windows.sh.
#
# Why aqtinstall and not ownstuff: the standard Arch mingw-w64 Qt
# repo (ownstuff) does not package qt6-pdf, and no major distro
# packages mingw-w64-qpdf. aqtinstall pulls Qt's own prebuilt
# Windows/MinGW binaries (including the qtpdf module) without
# requiring a Qt account, and qpdf's GitHub releases include a
# prebuilt mingw64 zip.
#
# ABI note: Qt's win64_mingw binaries are built with a specific
# MinGW version (13.x for Qt 6.10.x). Arch's mingw-w64-gcc may be
# newer (14+). C++ ABI between MinGW GCC 13 and 14 is stable for
# common Qt/qpdf usage — we link Trailer against Qt's prebuilt .a
# files and ship whichever libstdc++-6.dll/libgcc_s_seh-1.dll
# comes with Arch's mingw.
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

FROM archlinux:latest

# Pinned versions. Bump together when Qt releases a new point
# release; qpdf evolves independently.
#
# Qt: matches the Linux/macOS/Windows CI matrix in ci.yml. Aqt only
# knows about win64_mingw from Qt 6.10.x onward in a complete way,
# so we pin to 6.10.3 here even if CI installs something slightly
# newer on Linux — the produced .exe uses the pinned Qt libs, so
# this is self-consistent.
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

# pacman 6.1+ runs downloads in a seccomp-filtered sandbox user
# ('alpm'), which fails under QEMU user-mode emulation (e.g. ARM
# hosts running this amd64 image). DisableSandbox is harmless on
# native x86_64 hosts — we're installing build-deps into an ephemeral
# image.
RUN sed -i 's/^#DisableSandbox/DisableSandbox/' /etc/pacman.conf \
    && grep -q '^DisableSandbox' /etc/pacman.conf \
        || echo 'DisableSandbox' >> /etc/pacman.conf

# Only mingw-w64-gcc (+ binutils, crt, headers, winpthreads as
# transitive deps) is in Arch's official extra repo. No
# `mingw-w64-cmake` wrapper exists upstream, so the build uses the
# toolchain file at cmake/toolchain-mingw-w64.cmake directly.
RUN pacman -Sy --noconfirm archlinux-keyring \
    && pacman -Syu --noconfirm --needed \
        base-devel \
        cmake \
        ninja \
        git \
        wget \
        unzip \
        p7zip \
        python \
        python-pip \
        ca-certificates \
        mingw-w64-gcc \
    && pacman -Scc --noconfirm

# aqtinstall: no Qt account required; pulls Qt's prebuilt Windows
# MinGW binaries (libs + headers + qtpdf module) into /opt/qt.
RUN pip install --break-system-packages --no-cache-dir aqtinstall

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
