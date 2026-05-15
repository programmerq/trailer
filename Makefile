# Convenience wrappers around the release-pipeline build scripts.
#
# These targets mirror exactly what CI runs in .github/workflows/
# release.yml, so a green local `make release` is a strong signal
# that CI will succeed. The scripts/build-*.sh files are the source
# of truth; the workflow YAML invokes them too. No build logic
# lives in Makefile rules — they're thin wrappers.
#
# Run `make help` for a summary.

.PHONY: help release release-macos release-windows release-uat clean-release

HOST_UNAME := $(shell uname)

help:
	@echo "Trailer release-artifact targets (mirror CI's release.yml):"
	@echo ""
	@echo "  make release          build the release artifact for the host platform"
	@echo "                          - macOS host  → scripts/build-macos.sh"
	@echo "                          - Linux host  → cmake + ctest (no script wrapper)"
	@echo "  make release-macos    build the universal .app DMG (macOS host only)"
	@echo "  make release-windows  Windows cross-build via Docker (any host)"
	@echo "  make release-uat      run the UAT suite via Docker"
	@echo "  make clean-release    rm -rf build-macos/, build-macos-deps/, dist/"
	@echo ""
	@echo "All targets honour the VERSION file as the canonical version"
	@echo "string. Bump VERSION (and reconfigure cmake) before tagging."

ifeq ($(HOST_UNAME),Darwin)
release: release-macos
else ifeq ($(HOST_UNAME),Linux)
release:
	cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DTRAILER_WERROR=ON
	cmake --build build --parallel
	cd build && QT_QPA_PLATFORM=offscreen ctest --output-on-failure --label-exclude uat
else
release:
	@echo "Unsupported host: $(HOST_UNAME)"; exit 1
endif

release-macos:
	scripts/build-macos.sh

release-windows:
	scripts/build-windows.sh

release-uat:
	scripts/run-uat.sh

clean-release:
	rm -rf build-macos build-macos-deps dist
