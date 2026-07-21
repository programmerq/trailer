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
# gcc/g++, ccache, the mesa/OpenGL + xcb dev libs, libqpdf-dev + qpdf, the
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
# .github/actions/setup-linux-build/action.yml). Verified 2026-07-11:
# the actions-runner image (release v2.335.1, which `:latest` currently
# points at) is built FROM mcr.microsoft.com/dotnet/runtime-deps:8.0-noble
# (Ubuntu 24.04, ImageOS=ubuntu24) per images/Dockerfile at that tag
# (github.com/actions/runner). Noble main ships qpdf 11.9.x, so the
# Linux link is satisfied by the base — no PPA needed.
#
# The base FLOATS to the moving `:latest` tag (no version + digest pin) so
# every build-runner-image rebuild pulls the newest actions-runner release
# automatically, with no manual tag/digest bump step. The tradeoff is
# accepted deliberately: we would rather the image break loudly on an
# upstream change than sit silently stuck on an old base. If a future
# actions-runner base regressed — e.g. a hypothetical move back to an older
# Ubuntu with qpdf < 11 — the build (or the runtime guard) surfaces it
# instead of a stale pin hiding it. Belt-and-suspenders,
# setup-linux-build.action.yml version-guards libqpdf-dev at runtime and
# refuses to skip a < 11 install.
FROM ghcr.io/actions/actions-runner:latest

# The actions-runner base runs as the non-root `runner` user; switch to
# root for the system installs and restore `runner` (+ its workdir) at
# the end so the runner process starts exactly as the base image
# intends.
USER root

ARG DEBIAN_FRONTEND=noninteractive

# Everything the two setup composite actions apt-install, in one layer:
#   * Native build toolchain: build-essential (gcc/g++, C++20-capable),
#     cmake, ninja-build, mold, pkg-config, plus ccache (the compiler
#     launcher CMAKE_CXX_COMPILER_LAUNCHER points at; the CI ccache
#     action detects it on PATH and skips its own apt install).
#   * Qt's Linux runtime/dev deps: mesa/OpenGL dev libs, the xcb libs
#     Qt's xcb platform plugin loads, xkbcommon, cups.
#   * PDF backend: libqpdf-dev (Trailer links the libqpdf C++ API) +
#     the qpdf CLI (kept as a small convenience for manual PDF
#     inspection/debugging on the runner; the test suite drives the
#     libqpdf C++ API directly, not the CLI).
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
        ccache \
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

# General GitHub-Actions tooling, in a layer of its own so adding to it
# doesn't invalidate the (large, slow) toolchain layer above. Kept
# deliberately frugal — image size is a running cost, so this is only
# what the repo's own workflows drive plus the two near-universal Actions
# shell utilities:
#   * gh — the GitHub CLI. The release automation calls it directly:
#     `gh run list` / `gh workflow run` / `gh release view`
#     (.github/workflows/release.yml, release-publish.yml,
#     release-autotag.yml). Installed from GitHub's OFFICIAL apt repo per
#     https://cli.github.com, using the pinned-keyring method (fetch the
#     archive keyring to /usr/share/keyrings, add a signed-by deb line).
#     wget + ca-certificates come from the toolchain layer above.
#   * jq — command-line JSON. Not invoked by the current workflows (they
#     rely on gh's built-in `--jq` flag), but a common, forward-looking CI
#     utility for slicing JSON in shell steps. Tiny, and effectively
#     expected on any CI runner.
#   * zip — the release job builds a Windows .zip and today hand-rolls it
#     with `7z a -tzip` (release.yml) only because `zip` wasn't present;
#     ship the conventional tool so those steps can use it.
#   * shellcheck — the repo's release automation is heavily shell-driven,
#     and shellcheck is the common lint tool for shell in projects like
#     this. Small Ubuntu-main package.
RUN out=/usr/share/keyrings/githubcli-archive-keyring.gpg \
    && wget -nv -O "$out" https://cli.github.com/packages/githubcli-archive-keyring.gpg \
    && chmod go+r "$out" \
    && echo "deb [arch=$(dpkg --print-architecture) signed-by=$out] https://cli.github.com/packages stable main" \
        > /etc/apt/sources.list.d/github-cli.list \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
        gh \
        jq \
        zip \
        shellcheck \
    && rm -rf /var/lib/apt/lists/*

# ux-walkthrough Tier-1 drive harness (tools/ux-walkthrough/): drives the real
# built binary through the golden paths under a REAL X server + window manager
# and captures per-step screenshots for the ux-walkthrough judge persona. This
# is an AGENT-INVOKED pre-PR aid, NOT a blocking CI gate — but the tools must be
# present on the runner for an agent to invoke it there. In its own layer so it
# doesn't invalidate the (large, slow) toolchain layer above.
#   * xvfb            — headless X server (xvfb-run)
#   * openbox         — lightweight WM; REQUIRED so xdotool can activate/focus
#                       windows (_NET_ACTIVE_WINDOW) and so modal stacking is
#                       real — the whole point of driving on xcb vs offscreen
#   * x11-apps        — xwd (screenshot capture fallback)
#   * xdotool         — real X input events + window queries
#   * imagemagick     — import (screenshot) + convert (fixtures)
#   * xclip           — puts an image on the X clipboard (golden path 1)
#   * libxcb-cursor0  — REQUIRED: Qt 6's xcb platform plugin fails to load at
#                       runtime without it (not otherwise pulled by the
#                       toolchain layer above)
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        xvfb \
        openbox \
        x11-apps \
        xdotool \
        imagemagick \
        xclip \
        libxcb-cursor0 \
    && rm -rf /var/lib/apt/lists/*

# wayland-smoke CI tier (scripts/wayland-smoke.sh): launch the real built
# binary on a headless Wayland compositor and grim-capture the window, to
# prove Trailer loads the *wayland* platform plugin and paints real UI —
# something offscreen unit tests and the Wine tier can never prove. In its
# own layer so it doesn't invalidate the (large, slow) toolchain layer above.
#   * sway   — wlroots compositor; runs headless (WLR_BACKENDS=headless) with
#              the pixman software renderer (no GPU in the k8s pods). Chosen
#              over weston because weston's headless backend can't be
#              screenshotted (grim needs wlroots' wlr-screencopy). See
#              docs/ci/wayland-tier.md.
#   * grim   — wlroots screenshot client; captures the compositor output.
# The Qt wayland platform plugin itself needs NO package here — the aqt Qt
# 6.11 linux_gcc_64 bundle already ships libqwayland.so + the xdg-shell
# integration (verified in Phase 1), so setup-linux-build pulls it in with Qt.
#
# NOTE (ride-along, like PR #87): until this image is rebuilt + the ARC
# runner-set repointed at the new tag, sway+grim are NOT yet baked in. The
# wayland-smoke job in .github/workflows/ci.yml apt-installs them per-run
# (mirroring the Wine tier's install pattern) so it goes green WITHOUT waiting
# for an image rebuild; this layer just removes that per-run cost later.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        sway \
        grim \
    && rm -rf /var/lib/apt/lists/*

# Restore the base image's non-root runner identity and home so the
# ARC-launched runner process behaves identically to the stock image.
USER runner
WORKDIR /home/runner
