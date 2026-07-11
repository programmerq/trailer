# Trailer custom ARC (Actions Runner Controller) runner image.
#
# Purpose: bake in the FULL toolchain that both the Linux and the
# Windows-cross CI jobs currently apt-install on every run, so builds
# on the self-hosted trailer-k8s pods skip the slow per-run apt step.
# The setup actions (.github/actions/setup-linux-build and
# setup-windows-cross) auto-detect each dependency and SKIP the apt
# install when it's already present, so this image is a pure speedup:
# the same actions still work unchanged on the stock
# ghcr.io/actions/actions-runner image (they just apt every run there).
#
# What is baked in here: toolchain packages ONLY — cmake/ninja/mold,
# gcc/g++, the mesa/OpenGL + xcb dev libs, libqpdf-dev + qpdf, the
# mingw-w64 (-posix) cross toolchain, Wine + i386 multiarch, and the
# python/pip needed for aqtinstall. NOT baked in: the Qt, ONNX and
# cross-built qpdf/libjpeg prefixes — those stay cache-keyed by the
# setup actions (a Qt/ONNX bump shouldn't force an image rebuild).
#
# To switch the ARC runner-set onto this image, change the one line in
# the runner-set values (see docs/ci/custom-runner-image.md):
#   image: ghcr.io/programmerq/trailer-runner:latest
#
# Built + published by .github/workflows/build-runner-image.yml (which
# runs on a GitHub-hosted runner — building an image needs a Docker
# daemon the k8s pods lack).
#
# NOTE on qpdf version: Trailer's Linux link needs qpdf >= 11 (jammy's
# qpdf 10.x breaks with `undefined reference to
# QPDFFormFieldObjectHelper::isChecked()` — see
# .github/actions/setup-linux-build/action.yml). The stock
# actions-runner base is Ubuntu-based; if a future base tag ships
# qpdf < 11, either move the base to a noble (24.04)-based runner image
# or add the qpdf PPA before `libqpdf-dev` here. Verify after a build
# with `docker run --rm <image> qpdf --version`.
FROM ghcr.io/actions/actions-runner:latest

# The actions-runner base runs as the non-root `runner` user; switch to
# root for the system installs and restore `runner` (+ its workdir) at
# the end so the runner process starts exactly as the base image
# intends.
USER root

ARG DEBIAN_FRONTEND=noninteractive

# Everything the two setup composite actions apt-install, in one layer:
#   * Native build toolchain: build-essential (gcc/g++, C++20-capable),
#     cmake, ninja-build, mold, pkg-config.
#   * Qt's Linux runtime/dev deps: mesa/OpenGL dev libs, the xcb libs
#     Qt's xcb platform plugin loads, xkbcommon, cups.
#   * PDF backend: libqpdf-dev (link) + qpdf (CLI, used by tests).
#   * Windows cross toolchain: mingw-w64 (-posix threading) g++/gcc +
#     binutils + mingw zlib; Wine + i386 multiarch for the test tier.
#   * aqtinstall needs python3 + venv + pip; git/curl/wget/unzip/
#     p7zip-full/ca-certificates are used by the setup actions' download
#     and extract steps.
# i386 is enabled first because Wine pulls 32-bit dependencies.
RUN dpkg --add-architecture i386 \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        ninja-build \
        mold \
        pkg-config \
        libgl1-mesa-dev \
        libglu1-mesa-dev \
        libegl1-mesa-dev \
        libxkbcommon-dev \
        libx11-xcb-dev \
        libxcb-icccm4 \
        libxcb-image0 \
        libxcb-keysyms1 \
        libxcb-randr0 \
        libxcb-render-util0 \
        libxcb-shape0 \
        libxcb-sync1 \
        libxcb-xfixes0 \
        libxcb-xinerama0 \
        libxcb-xkb1 \
        libxkbcommon-x11-0 \
        libcups2-dev \
        libqpdf-dev \
        qpdf \
        gcc-mingw-w64-x86-64-posix \
        g++-mingw-w64-x86-64-posix \
        binutils-mingw-w64-x86-64 \
        libz-mingw-w64-dev \
        libz-mingw-w64 \
        wine64 \
        wine \
        python3 \
        python3-venv \
        python3-pip \
        git \
        curl \
        wget \
        unzip \
        p7zip-full \
        ca-certificates \
    && update-alternatives --set x86_64-w64-mingw32-gcc /usr/bin/x86_64-w64-mingw32-gcc-posix \
    && update-alternatives --set x86_64-w64-mingw32-g++ /usr/bin/x86_64-w64-mingw32-g++-posix \
    && rm -rf /var/lib/apt/lists/*

# Restore the base image's non-root runner identity and home so the
# ARC-launched runner process behaves identically to the stock image.
USER runner
WORKDIR /home/runner
