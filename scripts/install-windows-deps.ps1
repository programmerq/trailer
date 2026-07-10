# Install Trailer's native Windows build dependencies (Qt + qpdf)
# without going through the Qt online installer (which wants a free
# Qt Account) or the vcpkg Qt-from-source build (which is slow).
#
# Method:
#   - Qt: pip + aqtinstall fetches Qt's prebuilt msvc2022_64 binaries
#         straight from Qt's CDN.
#   - qpdf: download the official MSVC 64-bit prebuilt .zip from
#         github.com/qpdf/qpdf/releases.
#
# Output layout (under $InstallRoot, default %USERPROFILE%\trailer-deps):
#   <root>\Qt\<version>\msvc2022_64\
#   <root>\qpdf\
#
# scripts/build-windows-native.ps1 picks these up automatically when
# either TRAILER_DEPS is set or the default %USERPROFILE%\trailer-deps
# layout is used.
#
# Usage:
#   scripts/install-windows-deps.ps1                          # default
#   scripts/install-windows-deps.ps1 -InstallRoot D:\deps     # custom dir
#   scripts/install-windows-deps.ps1 -QtVersion 6.10.3        # pin Qt
#   scripts/install-windows-deps.ps1 -QpdfVersion 12.3.2      # pin qpdf
#   scripts/install-windows-deps.ps1 -SkipQt                  # qpdf only
#   scripts/install-windows-deps.ps1 -SkipQpdf                # Qt only
#
# Prereqs not handled here (we don't auto-install system tooling):
#   - Visual Studio 2022 with "Desktop development with C++" workload
#   - Python 3.x on PATH (used to install aqtinstall)

[CmdletBinding()]
param(
    [string]$InstallRoot,
    # aqtinstall 3.3.0 doesn't understand the new directory layout Qt
    # 6.11+ uses on download.qt.io (toolchain-suffixed instead of
    # nested), so we default to 6.10.3. Move this up when aqtinstall
    # gains 6.11+ support. CMake requires >=6.6.
    [string]$QtVersion = '6.10.3',
    [string]$QtArch = 'win64_msvc2022_64',
    [string]$QtModules = 'qtpdf',
    # qpdf 12+ adds PDF 2.0 fixes Trailer doesn't depend on; 11.x
    # works too. We pin to a known-good release with prebuilt MSVC
    # 64-bit binaries.
    [string]$QpdfVersion = '12.3.2',
    [switch]$SkipQt,
    [switch]$SkipQpdf,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

if (-not $InstallRoot) {
    $InstallRoot = if ($env:TRAILER_DEPS) { $env:TRAILER_DEPS } else { Join-Path $env:USERPROFILE 'trailer-deps' }
}

if (-not (Test-Path $InstallRoot)) {
    New-Item -ItemType Directory -Path $InstallRoot | Out-Null
}

Write-Host "==> Install root: $InstallRoot" -ForegroundColor Cyan

# --- Qt ----------------------------------------------------------------
$qtTarget = Join-Path $InstallRoot 'Qt'
$qtVerified = Join-Path $qtTarget "$QtVersion\msvc2022_64\lib\cmake\Qt6\Qt6Config.cmake"

if ($SkipQt) {
    Write-Host "==> Qt: skipped (-SkipQt)" -ForegroundColor Yellow
} elseif ((Test-Path $qtVerified) -and (-not $Force)) {
    # ${var} braces because PowerShell otherwise parses "$QtVersion:"
    # as a scope reference (like $global:foo) and bails with a parse
    # error. Same in the Qt-installed message below.
    Write-Host "==> Qt ${QtVersion}: already installed at ${qtTarget}\${QtVersion} (pass -Force to reinstall)" -ForegroundColor Green
} else {
    Write-Host "==> Installing Qt $QtVersion ($QtArch) via aqtinstall …" -ForegroundColor Green

    # aqtinstall is the recommended account-free path for Windows Qt
    # installs. We don't pin a python version — anything 3.9+ on PATH
    # is fine.
    $python = (Get-Command python -ErrorAction SilentlyContinue)
    if (-not $python) { $python = Get-Command py -ErrorAction SilentlyContinue }
    if (-not $python) { Write-Error "Python 3.x not found. Install from https://www.python.org/." }

    # Force a fresh aqtinstall — older versions miss Qt's recent
    # CDN layout changes.
    & $python.Source -m pip install --upgrade --quiet aqtinstall
    if ($LASTEXITCODE -ne 0) { Write-Error "pip install aqtinstall failed." }

    & $python.Source -m aqt install-qt windows desktop $QtVersion $QtArch -O $qtTarget -m $QtModules.Split(' ')
    if ($LASTEXITCODE -ne 0) { Write-Error "aqt install-qt failed." }

    if (-not (Test-Path $qtVerified)) {
        Write-Error "Qt install completed but Qt6Config.cmake is missing at $qtVerified."
    }
    Write-Host "==> Qt ${QtVersion} installed to ${qtTarget}\${QtVersion}" -ForegroundColor Green
}

# --- qpdf --------------------------------------------------------------
$qpdfTarget = Join-Path $InstallRoot 'qpdf'
$qpdfVerified = Join-Path $qpdfTarget 'lib\cmake\qpdf\qpdfConfig.cmake'

if ($SkipQpdf) {
    Write-Host "==> qpdf: skipped (-SkipQpdf)" -ForegroundColor Yellow
} elseif ((Test-Path $qpdfVerified) -and (-not $Force)) {
    Write-Host "==> qpdf ${QpdfVersion}: already installed at ${qpdfTarget} (pass -Force to reinstall)" -ForegroundColor Green
} else {
    Write-Host "==> Installing qpdf ${QpdfVersion} (MSVC 64-bit prebuilt) …" -ForegroundColor Green

    $zipName = "qpdf-$QpdfVersion-msvc64.zip"
    $zipUrl = "https://github.com/qpdf/qpdf/releases/download/v$QpdfVersion/$zipName"
    $zipPath = Join-Path $InstallRoot $zipName
    Invoke-WebRequest -Uri $zipUrl -OutFile $zipPath -UseBasicParsing

    $extractDir = Join-Path $InstallRoot 'qpdf-extract-tmp'
    if (Test-Path $extractDir) { Remove-Item $extractDir -Recurse -Force }
    Expand-Archive -Path $zipPath -DestinationPath $extractDir -Force

    # Archive layout: qpdf-<ver>-msvc64\<bin|lib|include|share>
    $innerDir = Get-ChildItem $extractDir -Directory | Select-Object -First 1
    if (-not $innerDir) { Write-Error "qpdf zip missing top-level directory." }

    if (Test-Path $qpdfTarget) {
        Remove-Item $qpdfTarget -Recurse -Force
    }
    Move-Item $innerDir.FullName $qpdfTarget
    Remove-Item $extractDir -Recurse -Force
    Remove-Item $zipPath -Force

    if (-not (Test-Path $qpdfVerified)) {
        Write-Error "qpdf install completed but qpdfConfig.cmake is missing at $qpdfVerified."
    }
    Write-Host "==> qpdf $QpdfVersion installed to $qpdfTarget" -ForegroundColor Green
}

Write-Host ""
Write-Host "Done. To build Trailer:" -ForegroundColor Cyan
Write-Host "  `$env:TRAILER_DEPS = '$InstallRoot'" -ForegroundColor Cyan
Write-Host "  scripts/build-windows-native.ps1" -ForegroundColor Cyan
