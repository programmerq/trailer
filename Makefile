# Convenience wrappers around the release-pipeline build scripts.
#
# These targets mirror exactly what CI runs in .github/workflows/
# release.yml, so a green local `make release` is a strong signal
# that CI will succeed. The scripts/build-*.sh files are the source
# of truth; the workflow YAML invokes them too. No build logic
# lives in Makefile rules — they're thin wrappers.
#
# Run `make help` for a summary.
#
# Windows: GNU Make works fine on Windows when invoked from a
# Developer Command Prompt or Git Bash (both ship with chocolatey
# `make`). On Windows, `uname` reports MINGW*/MSYS* — we detect that
# and route `make release` / `make test` to the PowerShell wrappers.

.PHONY: help release release-macos release-windows release-windows-native \
        release-uat test test-uat install-windows-deps clean-release \
        screenshots-windows \
        bump-release bump-post-release bump-dev bump-patch bump-minor bump-major \
        release-notes show-changelog

# `uname` exists on Linux, macOS, Git Bash, and MSYS. PowerShell-only
# environments don't have it; in that case `release` falls through to
# the "Unsupported host" branch. Users on bare PowerShell should call
# scripts/build-windows-native.ps1 directly.
HOST_UNAME := $(shell uname 2>/dev/null || echo Unknown)

help:
	@echo "Trailer build + release targets:"
	@echo ""
	@echo "  make release                   release artifact for the host platform"
	@echo "                                   - macOS host    → scripts/build-macos.sh"
	@echo "                                   - Linux host    → cmake + ctest"
	@echo "                                   - Windows host  → scripts/build-windows-native.ps1"
	@echo "  make test                      build + run unit tests on the host"
	@echo "  make test-uat                  build + run UAT suite on the host"
	@echo "  make release-macos             build the arm64 .app DMG (macOS host only)"
	@echo "  make release-windows           Windows cross-build via Docker (any host)"
	@echo "  make release-windows-native    native MSVC build (Windows host only)"
	@echo "  make release-uat               run the UAT suite via Docker"
	@echo "  make install-windows-deps      install Qt + qpdf (Windows host only)"
	@echo "  make clean-release             rm -rf build-macos/, build-macos-deps/, dist/"
	@echo ""
	@echo "VERSION-file lifecycle (see RELEASING.md, scripts/bump-version.sh):"
	@echo ""
	@echo "  make bump-release              strip -dev/-rc suffix (use before tagging)"
	@echo "  make bump-post-release         bump patch + add -dev (use after tagging)"
	@echo "  make bump-dev                  (deprecated) dev versions are now derived from git automatically; no manual bump"
	@echo "  make bump-patch                bump patch, keep -dev"
	@echo "  make bump-minor                bump minor (reset patch), keep -dev"
	@echo "  make bump-major                bump major (reset minor + patch), keep -dev"
	@echo ""
	@echo "Release-notes helpers:"
	@echo ""
	@echo "  make release-notes             draft CHANGELOG entries since the last v* tag"
	@echo "  make show-changelog VERSION=X.Y.Z"
	@echo "                                 print the CHANGELOG.md section for VERSION"
	@echo "                                 (what the GitHub Release body will splice in)"
	@echo ""
	@echo "All targets honour the VERSION file as the canonical version"
	@echo "string. Don't hand-edit VERSION; use the bump-* targets."

# ---------------------------------------------------------------- release
# Host-platform dispatch. Linux/macOS go straight to their build
# pipelines; Windows (detected by uname's MINGW/MSYS prefix when called
# from Git Bash) re-enters the PowerShell wrapper. The Windows branch
# also handles the case where someone runs `make` from chocolatey-make
# on plain cmd.exe — uname returns "Windows" there.
ifeq ($(HOST_UNAME),Darwin)
release: release-macos
test: release-macos
test-uat:
	scripts/run-uat.sh --host
else ifeq ($(HOST_UNAME),Linux)
release: test
test:
	cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
	# Bare `--parallel` (no number) does NOT honor CMAKE_BUILD_PARALLEL_LEVEL
	# — cmake only reads that env var when `--parallel` is absent entirely;
	# once present, cmake hands the native tool (GNU Make here) its OWN
	# default, i.e. an unbounded bare `-j`. This target runs on a
	# developer's own Linux box, not a memory-capped CI pod, so honor an
	# explicit CMAKE_BUILD_PARALLEL_LEVEL if set, else default to nproc.
	cmake --build build --parallel "$${CMAKE_BUILD_PARALLEL_LEVEL:-$$(nproc)}"
	cd build && QT_QPA_PLATFORM=offscreen ctest --output-on-failure --label-exclude 'uat|advisory'
test-uat:
	scripts/run-uat.sh --host
else ifneq (,$(findstring MINGW,$(HOST_UNAME))$(findstring MSYS,$(HOST_UNAME))$(findstring Windows,$(HOST_UNAME)))
release: release-windows-native
test:
	powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-windows-native.ps1
test-uat:
	powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-windows-native.ps1 -RunUat
else
release:
	@echo "Unsupported host: $(HOST_UNAME)"; exit 1
test: release
test-uat: release
endif

release-macos:
	scripts/build-macos.sh

release-windows:
	scripts/build-windows.sh

# Native MSVC build (Windows host only). The PowerShell wrapper exits
# non-zero if it's run on a non-Windows host or VS 2022 isn't installed,
# so this rule is safe to expose as a make target. -Deploy runs
# windeployqt + copies qpdf+MSVC runtime DLLs after tests pass, so
# the build dir is a shippable self-contained directory (you can zip
# build-trailer/ and run trailer.exe on a fresh Windows box).
release-windows-native:
	powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-windows-native.ps1 -RunUat -Deploy

install-windows-deps:
	powershell -NoProfile -ExecutionPolicy Bypass -File scripts/install-windows-deps.ps1

# Regenerate the Windows reference screenshots under docs/screenshots/
# windows. Builds + invokes tools/grab_screenshots, which drives
# Application + MainWindow under the offscreen plugin (works over
# SSH / on CI / on a headless box). Re-run after any UI change that
# meaningfully affects the toolbar or viewer layout.
screenshots-windows:
	powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-windows-native.ps1 -BuildOnly
	powershell -NoProfile -ExecutionPolicy Bypass -Command "$$repo=(Get-Location).Path; $$env:TRAILER_DEPS=if ($$env:TRAILER_DEPS) { $$env:TRAILER_DEPS } else { Join-Path $$env:USERPROFILE 'trailer-deps' }; $$env:Path = (Join-Path $$env:TRAILER_DEPS 'Qt\6.10.3\msvc2022_64\bin') + ';' + (Join-Path $$env:TRAILER_DEPS 'qpdf\bin') + ';' + $$env:Path; $$env:QT_QPA_PLATFORM='offscreen'; $$env:QT_QPA_FONTDIR='C:\Windows\Fonts'; & (Join-Path $$env:TRAILER_DEPS 'build-trailer\grab_screenshots.exe') (Join-Path $$repo 'docs\screenshots\windows')"

release-uat:
	scripts/run-uat.sh

clean-release:
	rm -rf build-macos build-macos-deps dist

# ---------------------------------------------------------------- version + changelog
# VERSION lifecycle. See scripts/bump-version.sh + RELEASING.md.
# These targets do NOT decide which bump kind to apply — that's a
# release-time human decision per project policy.

bump-release:
	scripts/bump-version.sh release

bump-post-release:
	scripts/bump-version.sh post-release

# Retained for muscle memory: dev versions are git-derived at configure
# time, so this is a no-op that just prints guidance (see bump-version.sh).
bump-dev:
	scripts/bump-version.sh dev-bump

bump-patch:
	scripts/bump-version.sh patch

bump-minor:
	scripts/bump-version.sh minor

bump-major:
	scripts/bump-version.sh major

release-notes:
	@scripts/release-notes.sh

show-changelog:
	@if [ -z "$(VERSION)" ]; then \
		echo "Usage: make show-changelog VERSION=X.Y.Z" >&2; exit 2; \
	fi
	@scripts/extract-changelog.sh "$(VERSION)"
