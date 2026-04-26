#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="$(awk '/^project\(/{found=1} found && /VERSION [0-9]/{print $2; exit}' "$REPO_ROOT/CMakeLists.txt")"
DMG_NAME="Trailer-${VERSION}-macOS.dmg"

# ── 1. Detect Qt ──────────────────────────────────────────────────────────────
if [[ -n "${QTDIR:-}" ]]; then
    echo "Using QTDIR from environment: $QTDIR"
else
    # Search common installation locations (use glob, not ls, to avoid word splitting)
    for CANDIDATE in "$HOME"/Qt/6.*/macos; do
        if [[ -x "$CANDIDATE/bin/macdeployqt" ]]; then
            QTDIR="$CANDIDATE"
            echo "Found Qt at: $QTDIR"
            break
        fi
    done

    if [[ -z "${QTDIR:-}" ]]; then
        # Fall back to PATH
        MACDEPLOYQT_PATH="$(command -v macdeployqt 2>/dev/null || true)"
        if [[ -n "$MACDEPLOYQT_PATH" ]]; then
            QTDIR="$(cd "$(dirname "$MACDEPLOYQT_PATH")/.." && pwd)"
            echo "Found Qt via PATH at: $QTDIR"
        fi
    fi
fi

if [[ -z "${QTDIR:-}" ]]; then
    echo "ERROR: Qt 6 not found." >&2
    echo "  Set the QTDIR environment variable to your Qt 6 macOS kit, e.g.:" >&2
    echo "    export QTDIR=\$HOME/Qt/6.8.0/macos" >&2
    echo "  Or install Qt 6.8+ with the qtpdf module from https://qt.io/download" >&2
    exit 1
fi

MACDEPLOYQT="$QTDIR/bin/macdeployqt"
if [[ ! -x "$MACDEPLOYQT" ]]; then
    echo "ERROR: macdeployqt not found at $MACDEPLOYQT" >&2
    exit 1
fi

# ── 2. Check required tools ───────────────────────────────────────────────────
for TOOL in cmake hdiutil; do
    if ! command -v "$TOOL" &>/dev/null; then
        echo "ERROR: Required tool '$TOOL' not found." >&2
        case "$TOOL" in
            cmake)
                echo "  Install CMake: brew install cmake  or  https://cmake.org/download/" >&2
                ;;
            hdiutil)
                echo "  hdiutil is part of macOS and should always be present." >&2
                ;;
        esac
        exit 1
    fi
done

# ── 3. Configure ──────────────────────────────────────────────────────────────
echo "Configuring..."
cmake \
    -B "$REPO_ROOT/build-macos" \
    -S "$REPO_ROOT" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$QTDIR"

# ── 4. Build ──────────────────────────────────────────────────────────────────
echo "Building..."
cmake --build "$REPO_ROOT/build-macos" -j"$(sysctl -n hw.logicalcpu)"

# ── 5. Deploy Qt frameworks ───────────────────────────────────────────────────
APP_PATH="$REPO_ROOT/build-macos/Trailer.app"
echo "Deploying Qt frameworks..."
"$MACDEPLOYQT" "$APP_PATH" -always-overwrite

# ── 6. Create DMG ────────────────────────────────────────────────────────────
# Note: the .icns and Info.plist are baked into the bundle by CMake
# (MACOSX_BUNDLE_INFO_PLIST + the TRAILER_MACOS_ICON target_sources block
# in CMakeLists.txt), so no extra copy step is needed.
DIST_DIR="$REPO_ROOT/dist"
mkdir -p "$DIST_DIR"

STAGING_DIR="$(mktemp -d)"
trap 'rm -rf "$STAGING_DIR"' EXIT
echo "Staging app for DMG..."
cp -R "$APP_PATH" "$STAGING_DIR/Trailer.app"

DMG_PATH="$DIST_DIR/$DMG_NAME"
echo "Creating DMG..."
hdiutil create \
    -volname "Trailer" \
    -srcfolder "$STAGING_DIR" \
    -ov \
    -format UDZO \
    -o "$DMG_PATH"

# ── 7. Done ───────────────────────────────────────────────────────────────────
echo ""
echo "Success! DMG created at:"
echo "  $DMG_PATH"

# ── Code signing and notarization (not yet configured) ───────────────────────
# TODO: Add Apple Developer Team ID and signing identity to use the section below.
#
# TEAM_ID="XXXXXXXXXX"
# SIGNING_IDENTITY="Developer ID Application: Your Name ($TEAM_ID)"
#
# echo "Signing app bundle..."
# codesign \
#     --deep \
#     --force \
#     --options runtime \
#     --entitlements "$REPO_ROOT/platform/macos/entitlements.plist" \
#     --sign "$SIGNING_IDENTITY" \
#     "$APP_PATH"
#
# echo "Signing DMG..."
# codesign --sign "$SIGNING_IDENTITY" "$DMG_PATH"
#
# echo "Submitting for notarization..."
# xcrun notarytool submit "$DMG_PATH" \
#     --team-id "$TEAM_ID" \
#     --wait
#
# echo "Stapling notarization ticket..."
# xcrun stapler staple "$DMG_PATH"
