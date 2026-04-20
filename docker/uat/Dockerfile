# Trailer UAT sandbox
#
# Ubuntu 24.04 + Qt6 (apt) + qpdf. Runs the UAT label of our CTest
# suite under QT_QPA_PLATFORM=offscreen, so no X server, Xvfb, or GPU
# is needed. The image bakes in build-deps only; source is mounted at
# /work at run time so iteration doesn't require rebuilding the image.
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

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        cmake \
        ninja-build \
        git \
        pkg-config \
        qt6-base-dev \
        qt6-base-dev-tools \
        qt6-tools-dev \
        qt6-pdf-dev \
        libqt6pdf6 \
        libqt6pdfwidgets6 \
        libqt6test6 \
        libqpdf-dev \
        libgl1-mesa-dev \
        libxkbcommon-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work
