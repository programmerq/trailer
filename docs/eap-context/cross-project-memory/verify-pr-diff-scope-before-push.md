---
name: verify-pr-diff-scope-before-push
description: An agent opening a PR from a partial/single-file worktree can capture a commit whose TREE is just that one file — merging it would DELETE the rest of the repo. Always verify `git diff origin/<base> --stat` shows the intended scope before pushing/opening a cross-repo or worktree PR.
metadata:
  type: feedback
  modified: 2026-07-18T05:35:45.164Z
---

**REAL near-miss (2026-07-18).** In `ev1-manual-redux`, PR #46 — a one-line
Micro-Pack 100 catalog `notes` fix — was committed from a worktree that had
**ONLY that one file checked out**, so the commit's tree contained just
`harness/ev1_connector_catalog.yaml`. Its diff vs `main` was:

```
15632 files changed, 1 insertion(+), 1596373 deletions(-)
```

Merging it would have **deleted 15,631 files** — the entire ev1-manual-redux
repo minus that one YAML. CI never even ran (the branch was pushed with a token,
which doesn't trigger workflows), so **nothing automated flagged it**. It was
caught only by a manual no-idle-drafts draft audit.

**Why it happened.** The branch/worktree was created with a **sparse or
single-file checkout**, and `git commit -a` / `git add .` captured a snapshot of
that **partial tree** rather than `main` + the edit. The intended CONTENT (the
one-line fix) was correct; the ARTIFACT (the tree) was catastrophically wrong.
The diff, not the edit, is what merges.

**How to apply — MANDATORY pre-push check.** Before pushing a branch or opening a
PR — **ESPECIALLY from a worktree or a sibling/cross-repo checkout** — run:

```bash
git diff origin/<base-branch> --stat   # or --numstat
```

and CONFIRM the file count + insertions/deletions match the intended change. A
diff showing **thousands of deletions** or files you didn't touch means the
branch base/tree is wrong — **STOP** and rebuild the branch on `origin/<base>`
before pushing. Put this check into any agent prompt that opens PRs.

**Repair recipe.**

```bash
git fetch origin <base>
git checkout <branch>
git reset --hard origin/<base>         # rebase the branch onto the real base tree
# re-apply the single intended edit
git diff origin/<base> --stat          # confirm it's ~the right size (1 file, few lines)
# run the gate green
git push --force-with-lease
```

**Related memory:** [[electricsim-worktree-fleet-regen-needs-top-level]] — the
other worktree-path hazard (nested `.claude/worktrees/` breaks redux symlink
resolution); same "a worktree is not the top-level checkout" family of failures.

---

Commit-trailer scrub (2026-07-20): the harness auto-appends `Co-Authored-By: Claude <model name>` trailers carrying a model identifier — which must not appear in pushed artifacts. Workers must scrub the trailer to plain 'Claude' (or drop it) before pushing. Discovered on #345's recovery; earlier merged history may carry the trailer — no history rewrite unless the owner asks.
