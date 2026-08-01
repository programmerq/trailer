#!/usr/bin/env bash
# Assert that a COMPILED Assets.car (the actool output — see CMakeLists.txt's
# TRAILER_ADAPTIVE_ICON block and ADR 0009) actually contains a dark /
# luminosity appearance marker for the AppIcon set.
#
# IMPORTANT — what this proves and what it does NOT:
#   - PROVES: the artifact actool produced from
#     resources/macos/Assets.xcassets contains SOME machine-readable trace
#     of a dark-appearance variant. This is the "built green" half of ADR
#     0009's threshold.
#   - Does NOT prove: that macOS actually RENDERS that variant in the Dock
#     when the system is in dark mode. That is the "icon didn't change"
#     half — native Dock/Finder chrome, unobservable from any script, real
#     or CI — and is exactly the gap that let ADR 0009 Option A ship
#     looking correct while failing on the owner's real Tahoe dogfood (see
#     docs/backlog/2026-07-17-adaptive-dock-icon-option-b.md). A green
#     result from this script is evidence toward, never a substitute for,
#     the real-Mac visual check that backlog item's Threshold requires.
#
# scripts/build-macos.sh's own post-build verification keeps a SEPARATE,
# non-fatal, informational version of this same grep (never fails a real
# release build on an unconfirmed pattern). THIS script is the hard-
# asserting counterpart, meant to be invoked only from an explicit,
# opt-in verification workflow — not from the release/nightly/dev-build
# pipelines — precisely because the exact assetutil key/value shape was
# never empirically confirmed against real Tahoe output before this
# script was written (see the backlog item). Once a real run confirms (or
# corrects) the pattern below, that confirmation belongs in a commit
# message / PR comment, not silently baked in as if it had always been
# verified.
#
# Usage: scripts/assert-adaptive-icon-variant.sh /path/to/Assets.car
# Exit 0 + prints the match; exit 1 + prints the FULL raw assetutil output
# (so a failing run is immediately diagnosable from the CI log, not just
# a bare "not found").

set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 /path/to/Assets.car" >&2
    exit 2
fi
ASSETS_CAR="$1"

if [[ ! -f "$ASSETS_CAR" ]]; then
    echo "ERROR: $ASSETS_CAR does not exist." >&2
    exit 1
fi

RAW_OUTPUT="$(assetutil --info "$ASSETS_CAR" 2>&1)"

echo "== Full assetutil --info output (ground truth for this run) =="
echo "$RAW_OUTPUT"
echo "== End assetutil output =="

if echo "$RAW_OUTPUT" | grep -qi "AppIcon"; then
    echo "OK: AppIcon set present in compiled catalog."
else
    echo "ERROR: AppIcon not found in $ASSETS_CAR — actool ran but the compiled catalog carries no AppIcon set." >&2
    exit 1
fi

if echo "$RAW_OUTPUT" | grep -qi "dark\|luminosity"; then
    echo "OK: a dark/luminosity marker is present in the compiled catalog (pattern: case-insensitive 'dark' or 'luminosity')."
    exit 0
else
    echo "ERROR: no dark/luminosity marker found in the compiled Assets.car." >&2
    echo "       This means the ARTIFACT itself does not visibly carry the dark" >&2
    echo "       variant after actool compilation — even before asking whether" >&2
    echo "       macOS renders it. See the full output above for what actool" >&2
    echo "       actually emitted, and docs/backlog/2026-07-17-adaptive-dock-" >&2
    echo "       icon-option-b.md for the research context." >&2
    exit 1
fi
