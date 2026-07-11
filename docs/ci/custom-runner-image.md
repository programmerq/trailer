# Custom runner image for the `trailer-k8s` self-hosted runners

The `trailer-k8s` runners are ephemeral pods managed by
[Actions Runner Controller (ARC)][arc]. By default they boot the stock
`ghcr.io/actions/actions-runner` image, which ships no build toolchain,
so every CI job apt-installs cmake/ninja/mingw/Wine/… from scratch on
each run. A custom image bakes that toolchain in so builds start warm.

## What the image bakes in

`docker/runner/Dockerfile` (`FROM ghcr.io/actions/actions-runner:latest`)
installs the **toolchain packages** both the Linux and the Windows-cross
jobs apt-install every run:

- Native build: `build-essential` (gcc/g++, C++20), `cmake`,
  `ninja-build`, `mold`, `pkg-config`.
- Qt's Linux runtime/dev deps: the mesa/OpenGL dev libs
  (`libgl1-mesa-dev`, `libglu1-mesa-dev`, `libegl1-mesa-dev`,
  `libxkbcommon-dev`, `libx11-xcb-dev`), the xcb libraries Qt's xcb
  platform plugin loads, and `libcups2-dev`.
- PDF backend: `libqpdf-dev` (link) + `qpdf` (CLI).
- Windows cross toolchain: mingw-w64 `-posix` gcc/g++ + binutils + mingw
  zlib (with the `-posix` alternative selected), plus `wine64`/`wine` and
  i386 multiarch for the Wine test tier.
- `python3` + `venv` + `pip` for aqtinstall, and
  git/curl/wget/unzip/p7zip-full/ca-certificates for the download steps.

It deliberately does **not** bake in the Qt, ONNX, or cross-built
qpdf/libjpeg prefixes. Those stay cache-keyed by the setup actions so a
Qt/ONNX version bump doesn't force an image rebuild.

## Auto-detect: the same actions work on stock and custom images

`.github/actions/setup-linux-build` and
`.github/actions/setup-windows-cross` probe each dependency
(`command -v cmake`, `command -v x86_64-w64-mingw32-g++`,
`dpkg -s libqpdf-dev` — version-aware, must be >= 11 —,
`command -v wine`, …) **before** installing it,
and skip the apt step when it's already present. The action log prints
`preinstalled (skipped)` or `will install via apt` for each dep.

Consequences:

- On the **stock** runner image nothing is preinstalled, so the actions
  apt-install everything exactly as before — no behaviour change.
- On the **custom** image every toolchain dep is already present, so the
  actions skip apt entirely and the job starts building immediately.
- `install-wine` now defaults to `auto`: Wine is installed only when
  neither `wine64` nor `wine` is on `PATH` (so the custom image skips it;
  explicit `true`/`false` still force/disable the install).

The same green build path runs either way; the custom image is purely a
speedup.

## Building + publishing the image

`.github/workflows/build-runner-image.yml` builds and pushes the image to
`ghcr.io/programmerq/trailer-runner`, tagged `latest` and a `YYYYMMDD`
date tag. It triggers on push to `main` touching the Dockerfile or the
workflow, on `workflow_dispatch`, and weekly (Mondays 06:00 UTC).

That workflow runs on **`ubuntu-latest` (GitHub-hosted)**, not
`trailer-k8s` — building a Docker image needs a Docker daemon, which the
k8s pods deliberately lack.

## The one-line ARC change

Point the ARC runner-set (the `AutoscalingRunnerSet` / Helm values that
define the `trailer-k8s` runners) at the custom image by changing the
runner container image:

```yaml
# ARC runner-set values (gha-runner-scale-set)
template:
  spec:
    containers:
      - name: runner
        image: ghcr.io/programmerq/trailer-runner:latest   # was ghcr.io/actions/actions-runner:latest
```

Apply with the usual `helm upgrade` for the runner-set release. New pods
pick up the image on their next scale-up; nothing in the workflows or
setup actions needs to change.

> **qpdf version note:** Trailer's Linux link needs qpdf >= 11 (Ubuntu
> jammy's qpdf 10.x fails with `undefined reference to
> QPDFFormFieldObjectHelper::isChecked()`). Verified 2026-07-11: the
> `actions-runner` base (release `v2.335.1`, which `:latest` points at)
> is `FROM mcr.microsoft.com/dotnet/runtime-deps:8.0-noble` (Ubuntu
> 24.04, `ImageOS=ubuntu24`), and noble ships qpdf 11.9 — so the base is
> fine. To keep it that way the Dockerfile PINS a specific released tag +
> digest instead of the moving `:latest`; when bumping, confirm the new
> tag's `images/Dockerfile` still uses an `*-noble` (or newer) base. As a
> second line of defence `setup-linux-build` is version-aware: it refuses
> to skip a libqpdf-dev < 11 install and warns loudly. Verify a built
> image with `docker run --rm <image> qpdf --version`.

[arc]: https://github.com/actions/actions-runner-controller
