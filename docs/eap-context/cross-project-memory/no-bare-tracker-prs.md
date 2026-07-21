---
name: no-bare-tracker-prs
description: Owner ruling that PRs whose only content is a backlog/tracker item must not be opened — implement the work or fold the tracker into real-work commits.
metadata:
  type: feedback
  modified: 2026-07-21T05:12:52.713Z
---

Owner ruling 2026-07-21 (~05:10Z, on electricsim #363 — a PR whose sole content was opening a backlog-item file for the BPM AMC1300B refdes-collision deferral): bare-tracker PRs shouldn't exist. He flipped it to draft and asked for the deferral actually implemented, or the PR closed and the ask batched elsewhere.

**Why:** a PR that only opens a tracking item adds review overhead without work product — the tracker belongs in docs/TODO.md via a commit that rides real work, or the underlying task just gets done. This overruled the coordinator's own earlier "keep it as a verify-and-likely-close tracker" call — verification-debt is not a PR.

**How to apply:** never open a PR whose only content is a backlog/tracker item. Either (a) implement the tracked work in the same PR with tests + green gates and a body explaining the real ask, or (b) close/never-open the PR and record the item by folding the backlog entry into an adjacent real-work PR or batching the question to the owner. When an investigation ends "can't confirm any remaining work," the honest terminal state is close-with-per-layer-evidence, not an open tracker PR. Related: [[pr-draft-ready-merge-policy]], [[owner-max-progress-adversarial-gate]].
