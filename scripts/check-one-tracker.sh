#!/usr/bin/env bash
# One tracker: TODO.md holds all open work (AGENTS.md, "The tracker is
# TODO.md, and nothing else"). This fails when the repo grows another
# tracking file, when two TODO.md entries share an id, or when a file cites
# an id that TODO.md doesn't have.
#
#   scripts/check-one-tracker.sh
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

status=0
fail() {
    echo "one-tracker: $*" >&2
    status=1
}

# 1. No other tracking files or folders.
while IFS= read -r path; do
    [ "$path" = "TODO.md" ] && continue
    lower=$(printf '%s' "$path" | tr '[:upper:]' '[:lower:]')
    case "/$lower" in
    */backlog/* | */decision-records/* | */decisions/* | */adr/* | */adrs/* | */audits/*)
        fail "$path: tracking folder; open work goes in TODO.md"
        continue
        ;;
    esac
    case "${lower##*/}" in
    todo*.md | *-todo.md | *_todo.md | backlog*.md | roadmap*.md | audit-*.md)
        fail "$path: tracking file; open work goes in TODO.md"
        ;;
    esac
done < <(git ls-files)

# 2. Entry ids are unique.
dups=$(grep -oE '^- \*\*T-[0-9]+ ' TODO.md | grep -oE 'T-[0-9]+' | sort | uniq -d | tr '\n' ' ')
[ -z "$dups" ] || fail "TODO.md: duplicate ids: $dups"

# 3. Every id cited outside TODO.md is open: the "T-NN: <reason>" markers
#    in known-bug QEXPECT_FAILs and the constancy sweep's KnownDefect
#    entries, and every "T-NN" in a comment or doc. The PR that closes an
#    entry must update what cites it. CHANGELOG.md is history and may name
#    closed ids.
cited=$(git grep -h -o -I -E '(^|[^A-Za-z0-9_])T-[0-9]+' -- . ':!TODO.md' ':!CHANGELOG.md' |
    grep -oE 'T-[0-9]+' | sort -u || true)
for id in $cited; do
    grep -qE "^- \*\*$id " TODO.md && continue
    files=$(git grep -l -I -E "(^|[^A-Za-z0-9_])$id([^0-9]|\$)" -- . ':!TODO.md' ':!CHANGELOG.md' |
        tr '\n' ' ')
    fail "${files% }: cites $id, which isn't in TODO.md (fixed? update or drop the reference)"
done

if [ "$status" -eq 0 ]; then
    echo "one-tracker: OK"
fi
exit "$status"
