#!/usr/bin/env bash
# Trailer VERSION-file lifecycle helper.
#
# Usage:
#   scripts/bump-version.sh release
#       Strip the -dev / -rc suffix. Use immediately before tagging.
#       Example: 0.2.0-dev → 0.2.0
#
#   scripts/bump-version.sh post-release
#       Bump the patch and append -dev. Use immediately after a tag
#       has been pushed.
#       Example: 0.2.0 → 0.2.1-dev
#
#   scripts/bump-version.sh dev-bump   (alias: dev)
#       Advance the -dev.N counter for a new dev build of the same
#       target version. Increments an existing -dev.N; promotes a bare
#       -dev to -dev.0; on a clean release version bumps the patch and
#       starts a fresh -dev.0.
#       Examples: 0.3.1-dev.0 → 0.3.1-dev.1
#                 0.3.1-dev   → 0.3.1-dev.0
#                 0.3.0       → 0.3.1-dev.0
#
#   scripts/bump-version.sh patch   (alias: rc-patch)
#       Bump the patch component, keeping -dev.
#       Example: 0.2.0-dev → 0.2.1-dev
#
#   scripts/bump-version.sh minor   (alias: rc-minor)
#       Bump the minor component, reset patch to 0, keep -dev.
#       Example: 0.2.0-dev → 0.3.0-dev
#
#   scripts/bump-version.sh major   (alias: rc-major)
#       Bump the major component, reset minor / patch to 0, keep -dev.
#       Example: 0.2.0-dev → 1.0.0-dev
#
#   scripts/bump-version.sh set X.Y.Z[-suffix]
#       Set VERSION explicitly. Last-resort escape hatch.
#
# Trailer follows SemVer (with 0.x caveats — see PHILOSOPHY.md "What
# 1.0 means" and RELEASING.md "Version numbering"). This script does
# not pick which bump to apply — that's a release-time human
# decision. It only enforces correctness once you've decided.
#
# The companion `release.yml` precheck skips heavy build jobs when
# VERSION carries a -dev / -rc suffix, so this script's -dev outputs
# are the right state for in-flight work.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

VERSION_FILE="VERSION"

if [[ ! -f "$VERSION_FILE" ]]; then
    echo "error: VERSION file missing at repo root" >&2
    exit 1
fi

CURRENT=$(tr -d '[:space:]' < "$VERSION_FILE")
if [[ -z "$CURRENT" ]]; then
    echo "error: VERSION file is empty" >&2
    exit 1
fi

# Parse CURRENT into MAJOR.MINOR.PATCH[-SUFFIX].
#
# Accepted shapes (validated below): X.Y.Z, X.Y.Z-dev, X.Y.Z-dev.N,
# X.Y.Z-rcN. Anything else fails loudly rather than silently mangling
# state.
parse_version() {
    local v="$1"
    if ! [[ "$v" =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)(-(dev(\.[0-9]+)?|rc[0-9]*))?$ ]]; then
        echo "error: VERSION='$v' is not in MAJOR.MINOR.PATCH[-dev[.N]|-rcN] form" >&2
        exit 1
    fi
    PARSED_MAJOR="${BASH_REMATCH[1]}"
    PARSED_MINOR="${BASH_REMATCH[2]}"
    PARSED_PATCH="${BASH_REMATCH[3]}"
    # BASH_REMATCH[5] is the suffix without its leading dash, e.g.
    # 'dev', 'dev.2', 'rc1' (empty for a clean X.Y.Z).
    PARSED_SUFFIX="${BASH_REMATCH[5]:-}"
}

write_version() {
    local new="$1"
    # Validate the result before writing — defence against logic
    # errors in this script.
    parse_version "$new"
    echo "$new" > "$VERSION_FILE"
    echo "VERSION: $CURRENT → $new"
    echo
    echo "Next steps:"
    case "$new" in
        *-dev|*-rc*)
            echo "  - Build locally to refresh TrailerVersion.h:"
            echo "      cmake -S . -B build && cmake --build build"
            ;;
        *)
            echo "  - Build locally and smoke-test:"
            echo "      make release"
            echo "  - Then open the release PR (see RELEASING.md)."
            ;;
    esac
}

parse_version "$CURRENT"

ACTION="${1:-}"

case "$ACTION" in
    release)
        if [[ -z "$PARSED_SUFFIX" ]]; then
            echo "error: VERSION='$CURRENT' has no -dev/-rc suffix to strip." >&2
            echo "       'release' is for stripping a development suffix before tagging." >&2
            echo "       If you meant to bump, use 'patch' / 'minor' / 'major'." >&2
            exit 1
        fi
        write_version "${PARSED_MAJOR}.${PARSED_MINOR}.${PARSED_PATCH}"
        ;;

    post-release)
        if [[ -n "$PARSED_SUFFIX" ]]; then
            echo "error: VERSION='$CURRENT' still carries a development suffix." >&2
            echo "       'post-release' is for bumping AFTER a tag has been pushed," >&2
            echo "       when VERSION should be a clean X.Y.Z." >&2
            exit 1
        fi
        NEW_PATCH=$((PARSED_PATCH + 1))
        write_version "${PARSED_MAJOR}.${PARSED_MINOR}.${NEW_PATCH}-dev"
        ;;

    dev-bump|dev)
        case "$PARSED_SUFFIX" in
            dev.*)
                # Already an -dev.N counter — advance N.
                DEV_N="${PARSED_SUFFIX#dev.}"
                write_version "${PARSED_MAJOR}.${PARSED_MINOR}.${PARSED_PATCH}-dev.$((DEV_N + 1))"
                ;;
            dev)
                # Bare -dev — start the counter at .0.
                write_version "${PARSED_MAJOR}.${PARSED_MINOR}.${PARSED_PATCH}-dev.0"
                ;;
            "")
                # Clean release — begin dev work on the next patch.
                NEW_PATCH=$((PARSED_PATCH + 1))
                write_version "${PARSED_MAJOR}.${PARSED_MINOR}.${NEW_PATCH}-dev.0"
                ;;
            *)
                echo "error: VERSION='$CURRENT' carries an -rc suffix; 'dev-bump' only advances -dev builds." >&2
                echo "       Use 'release' to finish the rc, or 'set' to move to a -dev version explicitly." >&2
                exit 1
                ;;
        esac
        ;;

    patch|rc-patch)
        NEW_PATCH=$((PARSED_PATCH + 1))
        write_version "${PARSED_MAJOR}.${PARSED_MINOR}.${NEW_PATCH}-dev"
        ;;

    minor|rc-minor)
        NEW_MINOR=$((PARSED_MINOR + 1))
        write_version "${PARSED_MAJOR}.${NEW_MINOR}.0-dev"
        ;;

    major|rc-major)
        NEW_MAJOR=$((PARSED_MAJOR + 1))
        write_version "${NEW_MAJOR}.0.0-dev"
        ;;

    set)
        if [[ $# -lt 2 ]]; then
            echo "error: 'set' requires an explicit version argument" >&2
            echo "Usage: scripts/bump-version.sh set X.Y.Z[-suffix]" >&2
            exit 1
        fi
        write_version "$2"
        ;;

    ""|-h|--help|help)
        sed -n '2,/^set -euo/p' "$0" | sed 's/^# \{0,1\}//' | sed '$d'
        exit 0
        ;;

    *)
        echo "error: unknown action '$ACTION'" >&2
        echo "Run 'scripts/bump-version.sh --help' for usage." >&2
        exit 1
        ;;
esac
