---
id: 2026-08-02-uat-fontdir-broken-on-cross-build
title: QT_QPA_FONTDIR resolves to /Fonts on the Windows cross-build (SystemRoot read on the Linux host)
priority: P2
status: open
source: Wine UAT triage pass, 2026-08-02 (docs/backlog/2026-07-24-wine-uat-failures-triage.md)
created: 2026-08-02
---

## Threshold

On the Wine lane, the test environment's `QT_QPA_FONTDIR` points at a
directory that **exists and contains fonts** inside the Wine prefix, and
a PDF written by `QPdfWriter` under that lane contains real PDF font
resources (searchable text), not filled vector paths. Checkable from the
CI log by echoing the resolved value in the UAT step, plus the
searchable-text UATs (UAT-VWR-061..066) no longer failing for "nothing to
find".

## Context

`tests/CMakeLists.txt:25` and `tests/uat/CMakeLists.txt:19` both set:

```cmake
list(APPEND _trailer_uat_env "QT_QPA_FONTDIR=$ENV{SystemRoot}/Fonts")
```

guarded by `if(WIN32)`. `WIN32` is true for the mingw cross-build, so the
branch is taken — but `$ENV{...}` reads CMake's **configure-time host**
environment, and the Wine lane configures on Linux, where `SystemRoot` is
unset. Nothing under `.github/`, `scripts/`, or `docker/` sets it.
Verified by evaluating the identical expression under CMake on Linux:

```
-- SystemRoot=[] -> [QT_QPA_FONTDIR=/Fonts]
```

So every Wine test — unit and UAT — runs with `QT_QPA_FONTDIR=/Fonts`, a
nonexistent path.

That block's own comment explains why this matters: under the `offscreen`
platform on Windows, Qt does not enumerate fonts through GDI, so without
a valid font dir it "falls back to drawing text as filled vector paths…
searchable-text UATs (UAT-VWR-061..066) all fail because there is nothing
to find." Three of the 21 Wine UAT failures are exactly those tests.

**The native Windows path is unaffected.** `scripts/build-windows-native.ps1`
runs on a real Windows host where `SystemRoot` is set at configure time,
so the value resolves correctly there — this is specifically a
cross-compile defect, which is why it went unnoticed.

**The Wine unit lane is currently green with the broken value**, so this
is latent there rather than visible. That is worth knowing before
assuming a fix will change unit results.

## Fix direction (not applied)

Resolve the font directory at **test run** time rather than CMake
configure time. Either:

- point the workflow step's env at the Wine prefix's font dir
  (`$WINEPREFIX/drive_c/windows/Fonts`), overriding the CMake-baked
  value; or
- export `SystemRoot` before `cmake` configures on the cross lane; or
- make the CMake branch condition on being a *native* Windows build
  (`CMAKE_HOST_WIN32`) and let the runner supply the value otherwise.

Whichever is chosen, echo the resolved value in the UAT step so the log
shows what the tests actually ran with — the whole reason this survived
is that no one could see it.
