#!/usr/bin/env bash
# Print the CHANGELOG.md section for a given version, without the
# version heading itself.
#
# Used by:
#   - The release-publish.yml workflow, to splice release notes into
#     the GitHub Release body.
#   - Maintainers, to sanity-check what users will see at tag time.
#
# Usage:
#   scripts/extract-changelog.sh 0.2.0
#       Print the body of `## [0.2.0] - ...`.
#
#   scripts/extract-changelog.sh v0.2.0
#       Leading `v` is stripped — CHANGELOG headings use bare versions.
#
# Exit codes:
#   0   matching section found and printed (may be empty)
#   1   CHANGELOG.md missing
#   2   no section found for the given version
#
# The output is whatever sits between `## [VERSION] - DATE` and the
# next `## ` heading, with leading / trailing blank lines trimmed.
# Heading-level subsections (`### Added`, etc.) pass through intact.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

CHANGELOG="CHANGELOG.md"

if [[ ! -f "$CHANGELOG" ]]; then
    echo "error: $CHANGELOG not found at repo root" >&2
    exit 1
fi

if [[ $# -lt 1 ]]; then
    echo "Usage: scripts/extract-changelog.sh <version>" >&2
    echo "Example: scripts/extract-changelog.sh 0.2.0" >&2
    exit 1
fi

VERSION="${1#v}"

# Match `## [VERSION]` at the start of a line, optionally followed by
# ` - <date>`. The closing `]` is part of the literal.
#
# awk does the heavy lifting: turn on capture at the matching heading,
# turn it off at the next `## ` heading OR at the first link-reference
# definition (`[name]: url` at col 1 — Keep-a-Changelog puts these at
# the end of the file, and they should never leak into the section
# body when the target is the bottom-most section).
section=$(awk -v v="$VERSION" '
    BEGIN {
        in_section = 0
        target = "^## \\[" v "\\]"
    }
    {
        if ($0 ~ target) {
            in_section = 1
            next
        }
        if (in_section && (/^## / || /^\[[^]]+\]: /)) {
            in_section = 0
        }
        if (in_section) {
            print
        }
    }
' "$CHANGELOG")

if [[ -z "$section" ]]; then
    echo "error: no '## [$VERSION] - ...' section found in $CHANGELOG" >&2
    exit 2
fi

# Trim leading + trailing blank lines so the spliced output sits
# cleanly inside a parent template.
printf '%s\n' "$section" | awk '
    BEGIN { started = 0 }
    {
        if (!started && $0 ~ /^[[:space:]]*$/) next
        started = 1
        lines[NR] = $0
        if ($0 !~ /^[[:space:]]*$/) last_nonblank = NR
    }
    END {
        for (i = 1; i <= last_nonblank; i++) print lines[i]
    }
'
