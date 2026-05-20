#!/usr/bin/env bash
# Print a CHANGELOG draft from git commits in a range.
#
# Usage:
#   scripts/release-notes.sh                       # since last v* tag .. HEAD
#   scripts/release-notes.sh v0.1.0..HEAD          # explicit range
#   scripts/release-notes.sh v0.1.0..v0.2.0        # historic range
#
# Output is roughly Keep-a-Changelog shaped — Added / Changed /
# Fixed / Infrastructure subsections, populated from Conventional-
# Commit-style prefixes (`feat:` → Added, `fix:` → Fixed,
# `ci:` / `build:` / `test:` / `docs:` / `chore:` → Infrastructure,
# everything else → Changed). The output is a STARTING POINT, not
# the final CHANGELOG entry — review, rewrite, drop noise, group by
# user-facing area.
#
# Pipe into your editor:
#   scripts/release-notes.sh > /tmp/notes.md && $EDITOR /tmp/notes.md
#
# Then paste the cleaned-up result into CHANGELOG.md under a new
# `## [X.Y.Z] - YYYY-MM-DD` heading.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

if [[ $# -ge 1 ]]; then
    RANGE="$1"
else
    LAST_TAG=$(git describe --tags --abbrev=0 --match='v*' 2>/dev/null || true)
    if [[ -z "$LAST_TAG" ]]; then
        echo "error: no v* tag found and no range argument supplied" >&2
        echo "Usage: scripts/release-notes.sh [<git-range>]" >&2
        exit 1
    fi
    RANGE="${LAST_TAG}..HEAD"
fi

if ! git rev-parse "$RANGE" >/dev/null 2>&1; then
    echo "error: '$RANGE' is not a valid git range" >&2
    exit 1
fi

# Collect commits as "subject" lines. Merge commits are excluded —
# their subjects are noise ("Merge pull request #N from …") and the
# actual work shows up on the merged commits themselves.
COMMITS=$(git log --no-merges --pretty=format:'%s' "$RANGE")
TOTAL=$(printf '%s\n' "$COMMITS" | grep -c . || true)

added=()
changed=()
fixed=()
infra=()

while IFS= read -r line; do
    [[ -z "$line" ]] && continue
    # Strip optional Conventional-Commit scope: "feat(ui): foo" → "feat: foo".
    # Lowercase the type prefix for matching.
    type_part="${line%%:*}"
    rest="${line#*:}"
    # If no colon, treat the whole line as the message and bucket as Changed.
    if [[ "$type_part" == "$line" ]]; then
        changed+=("$line")
        continue
    fi
    # Strip the optional Conventional-Commit scope (`feat(ui)` → `feat`)
    # via bash parameter expansion. The previous `sed 's/(.*)//'` was a
    # basic-regex literal — it matched the four characters `(.*)`, not
    # `( anything )`, so `feat(ui)` survived as `feat(ui)` and missed
    # the case branches below.
    type_lower=$(printf '%s' "${type_part%%(*}" | tr '[:upper:]' '[:lower:]')
    # Trim leading whitespace from rest.
    rest="${rest# }"
    case "$type_lower" in
        feat)
            added+=("$rest")
            ;;
        fix)
            fixed+=("$rest")
            ;;
        ci|build|test|docs|chore|deps)
            infra+=("$rest")
            ;;
        *)
            # Unknown / mixed-case prefix or no Conventional-Commit
            # discipline — keep the whole line so the human reviewer
            # sees the context they need to rebucket.
            changed+=("$line")
            ;;
    esac
done <<< "$COMMITS"

print_section() {
    local heading="$1"
    shift
    local items=("$@")
    if [[ ${#items[@]} -eq 0 ]]; then
        return
    fi
    printf '\n### %s\n\n' "$heading"
    local item
    for item in "${items[@]}"; do
        printf -- '- %s\n' "$item"
    done
}

PREV_REF="${RANGE%%..*}"
TO_REF="${RANGE##*..}"

cat <<EOF
<!--
Draft release notes for $RANGE
($TOTAL non-merge commits).

This is a STARTING POINT. Before pasting into CHANGELOG.md:
  - Drop entries that aren't user-visible.
  - Group related entries (e.g. "Sidebar mode X + Y + Z" → one bullet).
  - Move misfiled entries (e.g. \`refactor:\` that exposes new behaviour
    should be in Added, not Changed).
  - Tighten wording — commit subjects optimise for git history, not
    for users browsing release notes.

Replace this comment block + the heading below with a real
\`## [X.Y.Z] - YYYY-MM-DD\` section.
-->

## [Unreleased] — draft from \`$PREV_REF..$TO_REF\`
EOF

print_section "Added" "${added[@]+"${added[@]}"}"
print_section "Changed" "${changed[@]+"${changed[@]}"}"
print_section "Fixed" "${fixed[@]+"${fixed[@]}"}"
print_section "Infrastructure" "${infra[@]+"${infra[@]}"}"
