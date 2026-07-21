---
name: owner-prefers-larger-prs
description: Owner standing preference (2026-07-12) — batch related changes into fewer, LARGER PRs; don't open a separate PR just because it touches a slightly different thing
metadata:
  type: feedback
---

Owner standing preference (restated 2026-07-12): prefers PRs LARGER than is normally comfortable. Do NOT proliferate PRs — batch related changes into one PR rather than opening a separate PR for each "slightly different thing."

**Why:** more PRs = more CI thrash and review overhead; the owner explicitly dislikes "PRs opened like crazy." Concrete trigger: a CI parallelism-cap PR and a closely-related CI path-gate change were opened as two separate PRs (#235 and #236); the owner would have preferred the path-gate folded into #235.

**How to apply:** when a follow-on change is in the same area / same concern as an open PR (e.g. more CI-workflow tuning while a CI PR is open), add it to that PR's branch instead of opening a new one. This OVERRIDES the generic "small focused commits / don't batch" instinct for THIS owner — small commits within one PR are fine, but default to fewer, larger PRs. (Note: closely-related ≠ everything; genuinely unrelated work still gets its own PR.) Merges still need owner consent per [[pr-draft-ready-merge-policy]].

**Consolidate a cluster already open on one issue (owner 2026-07-19, VAT nightly #52):** when several PRs are already open and all address the same underlying issue/failure, fold them into ONE PR carrying the whole response and CLOSE the others with a pointer comment to the consolidated PR. Verbatim: "There are several PRs that all seem to be related to the same failure issue. That's excessive." (Three PRs — scoped baseline recapture + faithful test re-scope + a determinism work-item file — were folded into one "VAT #52 response" PR.) This is the after-the-fact counterpart of the don't-proliferate rule above.
