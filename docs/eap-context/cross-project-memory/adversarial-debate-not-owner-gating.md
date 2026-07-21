---
name: adversarial-debate-not-owner-gating
description: "Owner directive 2026-07-20: never gate on 'should I proceed'; resolve ordinary uncertainty via adversarial agent debate grounded in sources; surface only competing-interest forks as GH comments with impact context"
metadata:
  type: feedback
---

Owner directive (2026-07-20, verbatim excerpts): 'I don't want to see questions about oh this is very much the same, but it's a slightly different shape.. what to do?! If an agent is uncertain, do an adversarial agent debate grounded in our current source of truth. A should I proceed? isn't a real decision that should be gated on me. If you're not sure _how_ to proceed because there are competing interests, that's the sort of thing to surface. Use adversarial check agents as you go... My goal is to enable you to make AS MUCH PROGRESS as possible.'

**Why:** trivial/non-ambiguous questions waste his review bandwidth and stall allowed usage; the program's adversarial-verification machinery is the intended resolver, especially while GitHub Actions is degraded and CI can't be the checker.

**How to apply:** (1) 'Should I proceed?' is banned as an owner question — proceed. (2) Ordinary uncertainty (same-but-slightly-different judgment calls, ambiguous reads) → run an adversarial agent debate grounded in the current source of truth (manuals/redux/catalog/decisions), record the debate's verdict + reasoning, move on. (3) Surface to the owner ONLY genuine competing-interest forks — as GitHub comments on the relevant PR with a clear question and impact context. (4) Research mandate: pull best practices from archives, other automakers, and aluminum AIRCRAFT structure literature (longer-lived practice; note constraint differences). Related: [[owner-wants-panel-to-one-up-his-ideas]], [[manual-answerable-findings-source-and-implement]].
