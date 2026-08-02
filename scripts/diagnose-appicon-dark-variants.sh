#!/usr/bin/env bash
# Settle one question, on a Mac, in one run:
#
#   Does actool actually compile the `luminosity: dark` bitmaps in
#   resources/macos/Assets.xcassets/AppIcon.appiconset into Assets.car,
#   or does it silently drop them?
#
# WHY THIS EXISTS. The nightly macOS lane's informational check greps
# `assetutil --info` output for a dark/luminosity marker and reports it
# missing (see docs/backlog/2026-07-17-adaptive-dock-icon-option-b.md).
# That grep can only ever be *suggestive*: it is equally consistent with
# "the dark bitmaps were dropped" and with "the dark bitmaps are present
# but assetutil does not print an appearance axis for app-icon entries."
# Those two have completely different fixes, and the expensive decision
# downstream (author an Icon Composer .icon for ADR 0009 Option B) is
# only correct under the first.
#
# THE TEST. Compile the catalog twice with the SAME actool invocation the
# real build uses (CMakeLists.txt's TRAILER_ADAPTIVE_ICON block):
#
#   A. as committed (light + dark entries)
#   B. with every `_dark` image entry stripped from Contents.json, and
#      the dark PNGs deleted
#
# then compare the two Assets.car files byte-for-byte.
#
#   IDENTICAL  -> the dark bitmaps contribute NOTHING to the compiled
#                 catalog. They are being dropped. assetutil was telling
#                 the truth; Option A is a dead end; go to Option B.
#   DIFFERENT  -> the dark bitmaps ARE compiled in and assetutil simply
#                 does not surface them. The Dock-swap failure is
#                 somewhere else and Option B may not fix it — re-open
#                 the root-cause question BEFORE authoring a .icon.
#
# This is deliberately a differential test, not another grep: it does not
# depend on knowing what assetutil's output is *supposed* to look like,
# which is the exact unknown that made the existing check inconclusive.
#
# Requires: full Xcode (actool ships only with Xcode, not the Command
# Line Tools). Read-only with respect to the repo — everything happens in
# a temp dir. Not wired into any pipeline; run it by hand.
#
# Usage: scripts/diagnose-appicon-dark-variants.sh
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC_XCASSETS="$REPO_ROOT/resources/macos/Assets.xcassets"

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "ERROR: this diagnostic only runs on macOS (needs xcrun actool)." >&2
    exit 2
fi
if ! xcrun --find actool >/dev/null 2>&1; then
    echo "ERROR: actool not found — needs FULL Xcode, not just the Command Line Tools." >&2
    echo "       sudo xcode-select -s /Applications/Xcode.app/Contents/Developer" >&2
    exit 2
fi
if [[ ! -d "$SRC_XCASSETS" ]]; then
    echo "ERROR: $SRC_XCASSETS not found." >&2
    exit 2
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

echo "== Environment =="
xcodebuild -version 2>/dev/null || true
echo "actool: $(xcrun --find actool)"
echo

# Same flags as CMakeLists.txt's POST_BUILD step. Kept in lockstep by
# hand — if that invocation changes, change this one, or the diagnostic
# stops describing the build it is meant to explain.
compile_catalog() {
    local xcassets="$1" outdir="$2" label="$3"
    mkdir -p "$outdir"
    echo "-- compiling $label"
    xcrun actool \
        --app-icon AppIcon \
        --output-partial-info-plist "$outdir/partial.plist" \
        --minimum-deployment-target 14.0 \
        --platform macosx \
        --target-device mac \
        --compile "$outdir" \
        "$xcassets" > "$outdir/actool-stdout.txt" 2> "$outdir/actool-stderr.txt" || {
            echo "ERROR: actool failed for $label — output follows:" >&2
            cat "$outdir/actool-stdout.txt" "$outdir/actool-stderr.txt" >&2
            exit 1
        }
}

# --- A: the catalog exactly as committed --------------------------------
cp -R "$SRC_XCASSETS" "$WORK/with-dark.xcassets"
compile_catalog "$WORK/with-dark.xcassets" "$WORK/out-with-dark" "catalog WITH dark variants"

# --- B: same catalog, dark entries removed ------------------------------
cp -R "$SRC_XCASSETS" "$WORK/light-only.xcassets"
LIGHT_JSON="$WORK/light-only.xcassets/AppIcon.appiconset/Contents.json"
python3 - "$LIGHT_JSON" <<'PY'
import json, sys
path = sys.argv[1]
with open(path) as fh:
    doc = json.load(fh)
before = len(doc["images"])
doc["images"] = [img for img in doc["images"] if "appearances" not in img]
after = len(doc["images"])
with open(path, "w") as fh:
    json.dump(doc, fh, indent=2)
print(f"-- stripped {before - after} appearance-qualified entries ({before} -> {after})")
PY
rm -f "$WORK/light-only.xcassets/AppIcon.appiconset/"*_dark.png
compile_catalog "$WORK/light-only.xcassets" "$WORK/out-light-only" "catalog WITHOUT dark variants"

# --- compare ------------------------------------------------------------
A="$WORK/out-with-dark/Assets.car"
B="$WORK/out-light-only/Assets.car"
for f in "$A" "$B"; do
    [[ -f "$f" ]] || { echo "ERROR: expected $f was not produced." >&2; exit 1; }
done

SIZE_A=$(stat -f%z "$A")
SIZE_B=$(stat -f%z "$B")
HASH_A=$(shasum -a 256 "$A" | cut -d' ' -f1)
HASH_B=$(shasum -a 256 "$B" | cut -d' ' -f1)

echo
echo "== Result =="
printf '  with dark variants : %8s bytes  sha256=%s\n' "$SIZE_A" "$HASH_A"
printf '  light only         : %8s bytes  sha256=%s\n' "$SIZE_B" "$HASH_B"
echo

if [[ "$HASH_A" == "$HASH_B" ]]; then
    cat <<'MSG'
VERDICT: IDENTICAL — the dark bitmaps are DROPPED.

Removing every `luminosity: dark` entry and its PNG changes nothing about
what actool produces, so those bitmaps contribute nothing to Assets.car.
The nightly lane's assetutil check was reporting the truth, and no actool
flag or Contents.json key recovers them: a macOS .appiconset has no
supported light/dark axis.

  -> ADR 0009 Option A is a dead end. Proceed to Option B (Icon Composer
     .icon source, same build wiring) and record the outcome as an
     accepted update to ADR 0009.
MSG
    exit 0
else
    cat <<'MSG'
VERDICT: DIFFERENT — the dark bitmaps ARE compiled into Assets.car.

This CONTRADICTS the nightly lane's assetutil-based check, which means
that check is a false negative and the Dock-swap failure has a different
root cause. Do NOT spend Icon Composer authoring effort on the strength
of the Option-B recommendation in
docs/backlog/2026-07-17-adaptive-dock-icon-option-b.md — reopen the
root-cause question first, and record this result there.
MSG
    exit 0
fi
