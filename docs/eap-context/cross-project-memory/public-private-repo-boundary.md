---
name: public-private-repo-boundary
description: ev1sim is PUBLIC, electricsim is PRIVATE — never reference private-repo identifiers in anything pushed to public ev1sim (owner rule 2026-07-12)
metadata:
  type: feedback
---

Owner rule (2026-07-12): `programmerq/ev1sim` is a PUBLIC repo; `programmerq/electricsim` (and its CI) is PRIVATE. Do NOT expose private-repo identifiers in anything that lands on the PUBLIC ev1sim repo — commit messages, PR titles/bodies, PR/review comments, and code comments.

**Leaky identifiers to keep OUT of public ev1sim:** the name `electricsim`; runner labels `electricsim-mighty` / `electricsim-k8s`; `VAT` / "vehicle-acceptance-test" / `vat-nightly` / `vat/baselines`; private-repo PR/issue numbers; private file paths and backlog IDs. Generic phrasing is fine ("a downstream nightly build", "constrained-RAM CI runners", "7Gi").

**Direction matters:** private→public references are the hazard. Referencing the PUBLIC ev1sim (e.g. "ev1sim#26") from inside the PRIVATE electricsim repo is fine.

**Why:** the owner flagged a near-miss — ev1sim#26's PR *body* named `electricsim` and `VAT` in two sentences (commit messages + code comment stayed generic). He said it was "closer than I like" and required no rework of past activity, but the rule stands for all future ev1sim work.

**How to apply:** before pushing/opening anything on ev1sim, scan the text for the identifiers above and genericize. When drafting cross-repo PRs, describe the private consumer generically. Related: [[pr-draft-ready-merge-policy]].
