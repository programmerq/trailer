#!/usr/bin/env bash
# Decide whether tonight's nightly needs to build at all. Companion to
# scripts/compare-uat-baseline.sh (same "print vars for the caller to
# `eval`" shape) and scripts/parse-ctest-uat-summary.sh — see nightly.yml's
# "nightly-date" job for the call site.
#
# WHY: nightly-20260729's release body read "Draft release notes for
# nightly-20260728..HEAD (0 non-merge commits)" — the pipeline still ran
# all three lanes (build + unit + UAT), including the owner's single-
# instance self-hosted macOS VM, to rebuild and re-release byte-identical
# code. This script is the fix: a decision made ONCE, EARLY (nightly-date,
# before any lane starts), so a no-op night never spends runner time.
#
# WHAT COUNTS AS "CHANGED" — CODE, not commit count: the bug wasn't really
# about "0 commits", it was about rebuilding a tree nightly had already
# built. Counting non-merge commits (what scripts/release-notes.sh does for
# the human-readable changelog) is the WRONG signal to gate on — a run of
# merge-only commits, or a branch that reverts-then-reapplies back to the
# same tree, can read as "N commits" while the built tree is identical, and
# vice versa a single commit can change everything. So this script compares
# the actual TREE, not the commit graph: `git diff --quiet <prev> <curr>`
# over the full working tree. An identical-SHA night (the literal bug
# report) is trivially a no-op diff and falls out of the same check with no
# special case.
#
# DOCS-ONLY CHANGES ARE NOT SKIPPED (deliberate, see the PR body for the
# fuller rationale): this script does not path-filter the diff by file
# type. A change that touches only docs/markdown still counts as "code
# changed" and still builds. Rationale: (1) simplest defensible rule with
# no pathspec to keep in sync as the tree grows; (2) a hand-picked "docs"
# glob is exactly the kind of thing that quietly misclassifies a real
# change — a script, workflow, or packaging file edited alongside a docs
# tweak in the same commit would still need re-verification; (3) docs-only
# nights are rare in practice, so the wasted-runner cost this whole change
# targets is not meaningfully worse by including them. Revisit only if
# docs-only nights turn out to be common (tracked as a possible follow-up,
# not implemented here).
#
# DEGRADE GRACEFULLY, ALWAYS (never a false skip): no previous nightly tag,
# a tag whose target commit can't be resolved, or any git error while
# diffing all collapse to "do not skip" (BUILD). Skipping is only ever a
# fast-path optimization on a *positively confirmed* identical tree, never
# the default when evidence is missing.
#
# FORCE (workflow_dispatch): a human explicitly asking for a nightly always
# gets one — the caller passes FORCE=true straight through to a `skip=false`
# verdict before any of the above is even evaluated.
#
# Usage:
#   resolve-nightly-skip.sh <force> <current-commit> <prev-tag> <prev-commit>
#
#   <force>            "true"/"false" — already resolved by the caller from
#                       (event == workflow_dispatch AND the `force` input is
#                       "true"); the schedule trigger always passes "false".
#   <current-commit>   Full SHA of the commit that would be built tonight
#                       (required — caller has already checked out the
#                       resolved `ref`).
#   <prev-tag>         Previous `nightly-*` tag name, or "" if none exists.
#   <prev-commit>      That tag's target commit SHA, or "" if it couldn't be
#                       resolved.
#
# Must run inside a git working tree with both <current-commit> and
# <prev-commit> present locally (i.e. after a fetch-depth: 0 checkout).
#
# Prints, for `eval` by the caller:
#   SKIP=true|false
#   REASON_CODE=<forced|no_previous_nightly|unresolvable_previous_commit|
#                 diff_error|code_changed|code_unchanged|
#                 missing_current_commit>
#   MATCHED_COMMIT=<the previous commit SKIP=true matched, or empty>
set -uo pipefail

FORCE="${1:-false}"
CURRENT_COMMIT="${2:-}"
PREV_TAG="${3:-}"
PREV_COMMIT="${4:-}"

emit() {
  echo "SKIP=$1"
  echo "REASON_CODE=$2"
  echo "MATCHED_COMMIT=$3"
}

if [ -z "$CURRENT_COMMIT" ]; then
  echo "::error::resolve-nightly-skip.sh: current commit is required" >&2
  emit "false" "missing_current_commit" ""
  exit 0
fi

if [ "$FORCE" = "true" ]; then
  emit "false" "forced" ""
  exit 0
fi

if [ -z "$PREV_TAG" ]; then
  # First-ever nightly, or every previous nightly-* tag is gone. Nothing to
  # compare against — build.
  emit "false" "no_previous_nightly" ""
  exit 0
fi

if [ -z "$PREV_COMMIT" ] || ! git cat-file -e "${PREV_COMMIT}^{commit}" 2>/dev/null; then
  # Tag exists but its target commit is missing/unresolvable locally
  # (shallow fetch, corrupt ref, force-pushed history, etc.) — degrade to
  # BUILD rather than guess.
  emit "false" "unresolvable_previous_commit" ""
  exit 0
fi

if ! git cat-file -e "${CURRENT_COMMIT}^{commit}" 2>/dev/null; then
  emit "false" "missing_current_commit" ""
  exit 0
fi

git diff --quiet "$PREV_COMMIT" "$CURRENT_COMMIT" -- .
rc=$?
if [ "$rc" -eq 0 ]; then
  emit "true" "code_unchanged" "$PREV_COMMIT"
elif [ "$rc" -eq 1 ]; then
  emit "false" "code_changed" ""
else
  # `git diff` itself errored (rc >= 2) — e.g. an ambiguous/invalid rev
  # slipped through the resolvability checks above. Never skip on an
  # inconclusive comparison.
  emit "false" "diff_error" ""
fi
