# Native Windows build for Trailer (MSVC 2022 + Qt + qpdf).
#
# Companion to scripts/build-windows.sh — that script Docker-cross-
# compiles from Linux via mingw-w64. This one runs *on* Windows
# against a real MSVC toolchain. They produce binary-compatible
# artifacts (both 64-bit Windows .exes) but use different toolchains.
#
# Why two scripts? CI's release pipeline historically cross-compiles
# from Linux because that's the cheapest GitHub Actions runner. The
# native path exists so a Windows developer can iterate without
# Docker, and so a CI matrix can validate the native MSVC link
# against the prebuilt Qt + qpdf binaries Windows users actually
# install.
#
# Usage:
#   scripts/build-windows-native.ps1                            # build + test
#   scripts/build-windows-native.ps1 -BuildOnly                 # skip ctest
#   scripts/build-windows-native.ps1 -RunUat                    # also run UAT
#   scripts/build-windows-native.ps1 -BuildDir D:\trailer-build # custom build dir
#   scripts/build-windows-native.ps1 -QtDir C:\Qt\6.10.3\msvc2022_64 -QpdfDir C:\qpdf
#
# Defaults assume the layout produced by scripts/install-windows-deps.ps1:
#   $env:TRAILER_DEPS\Qt\6.10.3\msvc2022_64
#   $env:TRAILER_DEPS\qpdf
# (with TRAILER_DEPS falling back to %USERPROFILE%\trailer-deps).
#
# Prerequisites — see README.md "Windows" section for full notes:
#   - Visual Studio 2022 with "Desktop development with C++" workload
#   - CMake 3.24+ and Ninja on PATH
#   - Qt 6.10.x with the Pdf module (via aqtinstall or Qt online installer)
#   - qpdf 11+ MSVC prebuilt
#   - QT_QPA_FONTDIR is auto-set to %SystemRoot%\Fonts by the test
#     environment so QPdfWriter has access to Arial. See the comment
#     in tests/CMakeLists.txt for why.

[CmdletBinding()]
param(
    [string]$QtDir,
    [string]$QpdfDir,
    [string]$BuildDir,
    [ValidateSet('Release', 'Debug', 'RelWithDebInfo')]
    [string]$Config = 'Release',
    [switch]$BuildOnly,
    [switch]$RunUat,
    [switch]$Clean,
    [switch]$Werror
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot

# Resolve dependency locations. Order:
#   1. Explicit -QtDir / -QpdfDir
#   2. $env:TRAILER_DEPS — set by scripts/install-windows-deps.ps1
#   3. Conventional fallback: %USERPROFILE%\trailer-deps
$depsRoot = if ($env:TRAILER_DEPS) { $env:TRAILER_DEPS } else { Join-Path $env:USERPROFILE 'trailer-deps' }

if (-not $QtDir) {
    # Find newest installed 6.x under $depsRoot\Qt\<ver>\msvc2022_64
    $qtVersions = @()
    if (Test-Path (Join-Path $depsRoot 'Qt')) {
        $qtVersions = Get-ChildItem (Join-Path $depsRoot 'Qt') -Directory -ErrorAction SilentlyContinue `
            | Where-Object { Test-Path (Join-Path $_.FullName 'msvc2022_64\lib\cmake\Qt6\Qt6Config.cmake') } `
            | Sort-Object Name -Descending
    }
    if ($qtVersions) {
        $QtDir = Join-Path $qtVersions[0].FullName 'msvc2022_64'
    }
}
if (-not $QpdfDir) {
    $QpdfDir = Join-Path $depsRoot 'qpdf'
}
if (-not $BuildDir) {
    # Off-tree by default so the cross-build (build-windows/) and any
    # Linux native build don't clash with the MSVC CMakeCache pinning
    # paths.
    $BuildDir = Join-Path $depsRoot 'build-trailer'
}

# Sanity check. The null guards are explicit because $QtDir stays
# unset when nothing's installed under $depsRoot yet, and passing
# $null to Join-Path bails with the unhelpful "Cannot bind argument
# to parameter 'Path' because it is an empty string".
if (-not $QtDir) {
    Write-Error "Could not find a Qt install under '$depsRoot\Qt\<version>\msvc2022_64'. Run scripts/install-windows-deps.ps1 first, or pass -QtDir explicitly."
    exit 1
}
if (-not (Test-Path (Join-Path $QtDir 'lib\cmake\Qt6\Qt6Config.cmake'))) {
    Write-Error "Qt6Config.cmake not found at $QtDir\lib\cmake\Qt6. Pass -QtDir or run scripts/install-windows-deps.ps1."
    exit 1
}
if (-not (Test-Path (Join-Path $QpdfDir 'lib\cmake\qpdf\qpdfConfig.cmake'))) {
    Write-Error "qpdfConfig.cmake not found at $QpdfDir\lib\cmake\qpdf. Pass -QpdfDir or run scripts/install-windows-deps.ps1."
    exit 1
}

# Enter the MSVC developer environment. We do this in-process via
# Launch-VsDevShell.ps1 so the rest of the script picks up cl.exe,
# the Windows SDK, the CRT include/lib paths, etc.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) {
    Write-Error "vswhere.exe not found. Install Visual Studio 2022 with the 'Desktop development with C++' workload."
}
$vsInstall = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsInstall) {
    Write-Error "No VS 2022 install with VC.Tools.x86.x64 found. Install via Visual Studio Installer."
}
$devShell = Join-Path $vsInstall 'Common7\Tools\Launch-VsDevShell.ps1'
& $devShell -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null

Write-Host "==> Qt:      $QtDir" -ForegroundColor Cyan
Write-Host "==> qpdf:    $QpdfDir" -ForegroundColor Cyan
Write-Host "==> Build:   $BuildDir" -ForegroundColor Cyan
Write-Host "==> Config:  $Config" -ForegroundColor Cyan

Set-Location $repoRoot

if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host "==> Removing $BuildDir" -ForegroundColor Yellow
    Remove-Item $BuildDir -Recurse -Force
}

if (-not (Test-Path $BuildDir)) {
    New-Item -ItemType Directory -Path $BuildDir | Out-Null
}

$cmakeArgs = @(
    '-S', '.',
    '-B', $BuildDir,
    '-G', 'Ninja',
    "-DCMAKE_BUILD_TYPE=$Config",
    "-DCMAKE_PREFIX_PATH=$QtDir;$QpdfDir"
)
if ($Werror) {
    $cmakeArgs += '-DTRAILER_WERROR=ON'
}

Write-Host "==> cmake configure" -ForegroundColor Green
& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "==> cmake build" -ForegroundColor Green
& cmake --build $BuildDir --config $Config --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# Deploy Qt + qpdf DLLs next to trailer.exe so the binary works when
# launched directly (double-click, Explorer, shortcut) - without
# this, Windows' loader can't find Qt6Svg.dll etc. because Qt isn't
# on the system PATH. The cross-compile (scripts/build-windows.sh)
# does the equivalent via a manual objdump walk; on a native MSVC
# build Qt ships windeployqt which knows the full plugin layout
# (platforms\qwindows.dll, imageformats\, sqldrivers\, etc.) and
# does the copy correctly.
#
# Idempotent: re-running on a deployed dir is a no-op for unchanged
# DLLs, and any newly needed plugins get added without flushing.
$exe = Join-Path $BuildDir 'trailer.exe'
if (Test-Path $exe) {
    Write-Host "==> Deploying Qt runtime via windeployqt" -ForegroundColor Green
    $windeployqt = Join-Path $QtDir 'bin\windeployqt.exe'
    if (-not (Test-Path $windeployqt)) {
        Write-Error "windeployqt.exe not found at $windeployqt - Qt install is incomplete."
    }
    # --no-translations: we don't ship the Qt .qm files.
    # --no-system-d3d-compiler: not needed for our Qt feature surface.
    # --no-opengl-sw: we never request the software OpenGL fallback.
    # --release / --debug picks the right ABI variant of every DLL.
    $deployArgs = @('--release', '--no-translations',
                    '--no-system-d3d-compiler', '--no-opengl-sw', $exe)
    if ($Config -eq 'Debug') {
        $deployArgs[0] = '--debug'
    }
    & $windeployqt @deployArgs
    if ($LASTEXITCODE -ne 0) {
        Write-Error "windeployqt failed (exit $LASTEXITCODE)"
    }

    # qpdf's MSVC prebuilt ships qpdf30.dll plus the MSVC redist
    # runtimes (vcruntime140.dll, msvcp140.dll, ...). Copy the whole
    # bin/ - the MSVC runtimes are usually installed system-wide but
    # if a user is running on a fresh Windows install without VC++
    # redist, bundling them avoids a separate "install VC++ redist"
    # prompt.
    Write-Host "==> Copying qpdf + MSVC runtime DLLs" -ForegroundColor Green
    Get-ChildItem (Join-Path $QpdfDir 'bin\*.dll') -ErrorAction SilentlyContinue | ForEach-Object {
        Copy-Item $_.FullName -Destination $BuildDir -Force
    }

    $dllCount = (Get-ChildItem $BuildDir -Filter '*.dll' -ErrorAction SilentlyContinue).Count
    Write-Host "==> Deployed $dllCount DLLs alongside $exe" -ForegroundColor Green
}

if ($BuildOnly) {
    Write-Host "==> Build complete; tests skipped (-BuildOnly)." -ForegroundColor Green
    exit 0
}

# The test binaries dlopen Qt + qpdf at runtime via the standard
# Windows DLL search order, so the dependency bin dirs need to be on
# PATH. ctest --output-on-failure lets failures print their captured
# stdout/stderr.
$env:Path = "$QtDir\bin;$QpdfDir\bin;$env:Path"

# Run the test binaries against the offscreen Qt platform plugin so
# they don't try to open real windows. Over an SSH session there's
# no interactive display to attach to, and the default `windows`
# plugin will at best emit noisy QWARN lines and at worst hang
# waiting on a console session. Mirrors what Linux CI does via its
# step `env: { QT_QPA_PLATFORM: offscreen }` block.
#
# QT_QPA_FONTDIR is set per-test by tests/CMakeLists.txt /
# tests/uat/CMakeLists.txt — we don't need to export it here.
if (-not $env:QT_QPA_PLATFORM) {
    $env:QT_QPA_PLATFORM = 'offscreen'
}

Push-Location $BuildDir
try {
    Write-Host "==> Unit tests (ctest -LE uat)" -ForegroundColor Green
    & ctest -C $Config --output-on-failure --label-exclude uat
    $unitExit = $LASTEXITCODE

    $uatExit = 0
    if ($RunUat) {
        Write-Host "==> UAT (ctest -L uat)" -ForegroundColor Green
        & ctest -C $Config --output-on-failure -L uat
        $uatExit = $LASTEXITCODE
    }
} finally {
    Pop-Location
}

if ($unitExit -ne 0 -or $uatExit -ne 0) {
    Write-Host "Test failures: unit=$unitExit uat=$uatExit" -ForegroundColor Red
    exit ($unitExit -bor $uatExit)
}

Write-Host "==> All tests passed." -ForegroundColor Green
