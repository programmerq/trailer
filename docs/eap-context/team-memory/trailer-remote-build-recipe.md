---
name: trailer-remote-build-recipe
description: Verified build+test recipe for Trailer in the remote env (Qt 6.11 via aqtinstall, ONNX Runtime 1.25.0 via NuGet, apt OpenGL/qpdf deps)
metadata:
  type: reference
---

# Trailer — verified GREEN build & test recipe (remote env)

Verified 2026-07-09 at HEAD de62300 (C++20 / Qt6, version 0.2.0). Result: build OK, 38/38 ctest PASS, app launches offscreen. No source files modified.

## 1. System packages (apt)

```bash
apt-get update
apt-get install -y libqpdf-dev qpdf \
  libgl1-mesa-dev libglu1-mesa-dev libegl1-mesa-dev libxkbcommon-dev libx11-xcb-dev
```

- `libqpdf-dev` — CMakeLists finds libqpdf via pkg-config (`pkg_check_modules(QPDF REQUIRED IMPORTED_TARGET libqpdf)`).
- The mesa `-dev` packages are REQUIRED: Qt6Gui's CMake config hard-fails with "dependency WrapOpenGL could not be found" without OpenGL dev headers/libs.

## 2. Qt 6.11.0 via aqtinstall

apt Qt (6.4.2) is too old; CMake needs >= 6.5.

```bash
pip3 install aqtinstall
cd /opt
python3 -m aqt install-qt linux desktop 6.11.0 linux_gcc_64 -m qtpdf -O /opt/Qt
```

Installs to `/opt/Qt/6.11.0/gcc_64` (includes Core Concurrent Gui Widgets Network Svg Test Pdf PdfWidgets PrintSupport — all components CMakeLists requires).

## 3. ONNX Runtime 1.25.0 (NuGet route)

`cmake/OnnxRuntime.cmake` FetchContent-downloads the official tarball from GitHub, which returns HTTP 403 through the agent proxy. Bypass by fetching the identical binaries from NuGet and pre-seeding the FetchContent source dir.

```bash
cd /tmp
curl -sSL -o ort.nupkg "https://www.nuget.org/api/v2/package/Microsoft.ML.OnnxRuntime/1.25.0"
mkdir ortx && cd ortx && unzip -oq ../ort.nupkg
# Assemble the tarball-shaped layout the module expects (include/ + lib/libonnxruntime.so)
mkdir -p /opt/onnxruntime-1.25.0/include /opt/onnxruntime-1.25.0/lib
cp build/native/include/*.h                                     /opt/onnxruntime-1.25.0/include/
cp runtimes/linux-x64/native/libonnxruntime.so                 /opt/onnxruntime-1.25.0/lib/
cp runtimes/linux-x64/native/libonnxruntime_providers_shared.so /opt/onnxruntime-1.25.0/lib/
ln -sf libonnxruntime.so /opt/onnxruntime-1.25.0/lib/libonnxruntime.so.1   # match SONAME
```

## 4. Configure & build

```bash
cd /home/user/trailer
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/opt/Qt/6.11.0/gcc_64 \
  -DFETCHCONTENT_SOURCE_DIR_ONNXRUNTIME_PREBUILT=/opt/onnxruntime-1.25.0
cmake --build build --parallel
```

Key trick: `-DFETCHCONTENT_SOURCE_DIR_ONNXRUNTIME_PREBUILT=<dir>` makes FetchContent use the pre-extracted ORT SDK instead of downloading (declare name `onnxruntime_prebuilt` → var `FETCHCONTENT_SOURCE_DIR_ONNXRUNTIME_PREBUILT`).

`-DTRAILER_ORT_ROOT=` alone does NOT work — it only feeds `find_package(onnxruntime CONFIG)`, and neither the tarball nor the NuGet payload ships a CMake config, so it falls through to the download path.

## 5. Test

```bash
cd /home/user/trailer/build
QT_QPA_PLATFORM=offscreen ctest --output-on-failure
```

No `LD_LIBRARY_PATH` needed — the linker baked RUNPATH `/opt/Qt/6.11.0/gcc_64/lib:/opt/onnxruntime-1.25.0/lib` into the binaries.

ctest result:
```
100% tests passed, 0 tests failed out of 38
Total Test time (real) = 38.56 sec
```

## 6. App launch smoke test (offscreen)

```bash
cd /home/user/trailer/build
QT_QPA_PLATFORM=offscreen ./trailer --help      # exit 0, prints usage
QT_QPA_PLATFORM=offscreen ./trailer --version   # -> "Trailer 0.2.0", exit 0
```

## Paths

- Build directory: `/home/user/trailer/build`
- App binary:      `/home/user/trailer/build/trailer`
- Test binaries:   `/home/user/trailer/build/tests/` and `/home/user/trailer/build/tests/uat/`
- Qt:              `/opt/Qt/6.11.0/gcc_64`
- ONNX Runtime:    `/opt/onnxruntime-1.25.0`

## Gotchas

- GitHub release assets 403 via proxy — MUST use the NuGet route for ORT above.
- Qt6Gui CMake config fails without OpenGL dev libs (libgl1-mesa-dev etc.).
- `TRAILER_ORT_ROOT` does not help; use `FETCHCONTENT_SOURCE_DIR_ONNXRUNTIME_PREBUILT`.
- Harmless build warning from qpdf headers: "POINTERHOLDER_TRANSITION is not defined" (`-Wcpp`) — TRAILER_WERROR is OFF by default so it does not break the build.
- Locale warning at startup ("Detected locale C ... switched to C.UTF-8") is benign.
- `test_uat_search_and_markup` is the slow one (~34s); everything else is sub-second.

## Related

- [[trailer-undo-cap-desync]] — confirmed >64-edit undo desync at de62300 + ImageDocument not unified.

## Update 2026-07-10 — recipe now automated

This recipe is now automated by `scripts/session-setup.sh` + a `.claude/settings.json` SessionStart hook on branch `chore/session-setup-hook` @ `03ac5c9` (idempotent, sha256-pinned ORT download, self-heals partial Qt installs). Pushed to origin 2026-07-10; awaiting batched-PR assembly — see [[trailer-undo-cap-desync]].
