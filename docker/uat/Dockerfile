# Trailer UAT sandbox
#
# Ubuntu 24.04 + Qt 6.8 (aqtinstall) + qpdf (apt). Runs the UAT
# label of our CTest suite under QT_QPA_PLATFORM=offscreen, so no
# X server, Xvfb, or GPU is needed. The image bakes in build-deps
# only; source is mounted at /work at run time so iteration doesn't
# require rebuilding the image.
#
# Why aqtinstall (not apt's qt6-base-dev): Ubuntu 24.04 (noble)
# ships Qt 6.4.2 in main, but Trailer's CMakeLists.txt requires
# Qt 6.5+ for the qtpdf APIs and signal/slot patterns we rely on.
# aqtinstall pulls Qt's prebuilt Linux binaries from the Qt CDN —
# the same source GitHub Actions' install-qt-action uses on the
# host runners, so PR CI and UAT CI link against identical Qt.
#
# Build once:
#   docker build -t trailer-uat -f docker/uat/Dockerfile .
#
# Run (usually via scripts/run-uat.sh):
#   docker run --rm -v "$PWD":/work -w /work trailer-uat \
#       bash scripts/run-uat.sh --in-container
#
# Switching to Xvfb later (to enable real screenshots / true window
# compositing) is a drop-in: install xvfb + use xvfb-run in the entry
# command, and flip QT_QPA_PLATFORM off.

FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# Keep this in lockstep with .github/actions/setup-linux-build's
# Qt version so PR CI and UAT CI link against identical Qt.
ARG QT_VERSION=6.8.0

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        cmake \
        ninja-build \
        git \
        pkg-config \
        python3 \
        python3-venv \
        libqpdf-dev \
        libgl1-mesa-dev \
        libglu1-mesa-dev \
        libxkbcommon-dev \
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
        libfontconfig1 \
        libdbus-1-3 \
        libcups2-dev \
    && rm -rf /var/lib/apt/lists/*

# aqtinstall in a venv (Ubuntu 24.04's system pip refuses
# --break-system-packages installs without it).
RUN python3 -m venv /opt/aqt-venv \
    && /opt/aqt-venv/bin/pip install --no-cache-dir aqtinstall
ENV PATH=/opt/aqt-venv/bin:$PATH

RUN aqt install-qt linux desktop ${QT_VERSION} linux_gcc_64 \
        -m qtpdf \
        -O /opt/qt

ENV QT_DIR=/opt/qt/${QT_VERSION}/gcc_64
ENV CMAKE_PREFIX_PATH=${QT_DIR}
ENV PATH=${QT_DIR}/bin:$PATH
ENV LD_LIBRARY_PATH=${QT_DIR}/lib

WORKDIR /work
