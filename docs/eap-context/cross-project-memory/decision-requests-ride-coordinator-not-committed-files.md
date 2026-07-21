---
name: decision-requests-ride-coordinator-not-committed-files
description: Owner norm (2026-07-19) — a decision request tied to in-flight work must NOT be committed as a file (e.g. an owner-decision backlog item); route it via the coordinator channel and keep the PR draft on that stated hold until the owner rules.
metadata:
  type: feedback
---

Owner ruling 2026-07-19 on electricsim#325 (the p-model tooling PR), verbatim: "It shouldn't have created docs/backlog/BL-2026-07-19-p-model-drift-review-cadence.md at all. Instead, it should have kept the PR as a draft, and then asked you [the coordinator] to pass the request along."

**The norm.** When work produces a question that needs an OWNER DECISION (a genuine choice, not implementable work), do NOT commit it as an artifact — no `backlog.py open --decision` file, no "owner decision" item, no decision-request doc riding in the PR. Instead: (1) route the decision request through the coordinator channel (self-contained: gloss, options + implications, recommendation, default), and (2) if the decision gates the PR, keep the PR DRAFT with a stated hold-reason naming what it's waiting on (pairs with [[electricsim-draft-pr-must-state-hold-reason]]). Real, implementable work items DO get filed as normal backlog items — the distinction is decision-request vs work-item.

**Why.** A committed decision file clutters the backlog/ledger with a non-work item and puts a pending choice into the repo where the owner didn't want it; the owner reads decisions in the conversation (via the coordinator), not as files to hunt down.

**How to apply.** Before `backlog.py open` for something that's really "owner, which way?": stop — send it to the coordinator instead, and draft-hold the PR if it's gating. On #325 the fix was to `git rm` the cadence decision file, clean its refs (audit_work_item_refs --check → 0 dangling), and route the cadence ask (weekly floor + on-demand) via the coordinator. Related: [[pr-draft-ready-merge-policy]] (zero open owner-questions at ready-flip), [[manual-answerable-findings-source-and-implement]] (manual-answerable findings get implemented, not escalated — the inverse case).
