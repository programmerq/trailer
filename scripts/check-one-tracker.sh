#!/usr/bin/env bash
# One tracker: todo/ holds all open work, one file per entry (todo/README.md).
# This fails when the repo grows another tracking file, when an entry's file
# name or shape is wrong, or when a file cites an entry that doesn't exist.
#
#   scripts/check-one-tracker.sh
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

status=0
fail() {
    echo "one-tracker: $*" >&2
    status=1
}

slug_re='[0-9]{4}-[0-9]{2}-[0-9]{2}(-[a-z0-9]+)+'

# 1. No other tracking files or folders.
while IFS= read -r path; do
    case "$path" in todo/*) continue ;; esac
    lower=$(printf '%s' "$path" | tr '[:upper:]' '[:lower:]')
    case "/$lower" in
    */backlog/* | */decision-records/* | */decisions/* | */adr/* | */adrs/* | */audits/*)
        fail "$path: tracking folder; open work goes in todo/"
        continue
        ;;
    esac
    case "${lower##*/}" in
    todo*.md | *-todo.md | *_todo.md | backlog*.md | roadmap*.md | audit-*.md)
        fail "$path: tracking file; open work goes in todo/"
        ;;
    esac
done < <(git ls-files)

# 2. Every entry is todo/<date>-<slug>.md and has a title, a priority and a
#    Done when. Nothing else lives in todo/ but the README.
while IFS= read -r path; do
    [ "$path" = "todo/README.md" ] && continue
    name=${path#todo/}
    if ! printf '%s' "$name" | grep -qxE "${slug_re}\.md"; then
        fail "$path: entries are named todo/YYYY-MM-DD-short-slug.md (lower case)"
        continue
    fi
    head -1 "$path" | grep -qE '^# .' || fail "$path: the first line is the title, as '# Title'"
    grep -qE '^Priority: P[0-3]\.' "$path" || fail "$path: needs a 'Priority: P0.' line (P0 to P3)"
    grep -qE '^- Done when\b' "$path" || fail "$path: needs a '- Done when' line"
done < <(git ls-files -co --exclude-standard todo/)

# 3. Every entry cited anywhere exists: "todo/<slug>" in code, tests and
#    known-bug markers, "todo/<slug>.md" in Markdown links, and "(<slug>.md)"
#    links between entries. The PR that closes an entry must update what
#    cites it. CHANGELOG.md is history and may name closed entries.
cited=$( {
    git grep -h -o -I -E "todo/${slug_re}" -- . ':!CHANGELOG.md' | sed 's#^todo/##' || true
    git grep -h -o -I -E "\]\(${slug_re}\.md\)" -- 'todo/*.md' | sed -E 's#^\]\(##; s#\.md\)$##' || true
} | sort -u)
for slug in $cited; do
    [ -f "todo/$slug.md" ] && continue
    files=$(git grep -l -I -F "$slug" -- . ':!CHANGELOG.md' | tr '\n' ' ')
    fail "${files% }: cites $slug, which isn't in todo/ (fixed? update or drop the reference)"
done

if [ "$status" -eq 0 ]; then
    echo "one-tracker: OK"
fi
exit "$status"
