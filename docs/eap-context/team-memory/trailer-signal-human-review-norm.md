---
name: trailer-signal-human-review-norm
description: Owner norm (2026-07-15) — when a PR/decision needs a human, explicitly surface the ask + context + impact + the decision set; if the answer is unambiguous, act without waiting (e.g. undraft a ready PR); use socratic questions to resolve genuine ambiguity. Now codified as the surface-the-ask skill, PR #83 (merged 2026-07-19)
metadata:
  type: feedback
---

# Signal human-review-needed explicitly; don't park unambiguous work as a draft

Owner (programmerq) directive, on PR #68 (dev-build license-parity), verbatim:

> "What is the specific ask here to allow ready-to-merge? This should be a skill to help agents signal that you need a human to review. Surface the ask, give context, and share impact for whatever decision set you suspect. If there's an unambiguous answer, don't wait for a human. If you need a human, use socratic questions to resolve ambiguity!"

The trigger: a reviewed, CI-green, byte-verbatim 2-line mirror of already-merged steps had been left sitting as a DRAFT "waiting for a human" when there was no actual human decision to make — only the owner-gated merge click remained.

**Why:** the owner works async. Leaving an unambiguous change parked as a draft stalls it for no information gain; conversely, when a real decision IS needed, silence-in-draft gives the owner nothing to decide on. Both failure modes waste a round-trip. This governs the AFTER-review signal (how you hand a reviewed change back), complementing the before-push review gate in [[trailer-review-before-push-policy]].

**How to apply:**
1. Don't leave a reviewed, CI-safe, unambiguous change parked as a draft waiting for a human — mark it ready-for-review and state plainly that only the owner-gated merge click remains.
2. When a genuine human decision IS needed, surface it explicitly: the specific ask, the context, the impact, and the suspected decision set / options — don't just sit silent in draft.
3. Use socratic questions to resolve ambiguity rather than guessing.
4. DONE (no longer a proposal): the skill was built at `.claude/skills/surface-the-ask/SKILL.md` (sibling to decision-brief) and MERGED to main via PR #83 (https://github.com/programmerq/trailer/pull/83) on 2026-07-19. It is now the canonical in-repo reference at `.claude/skills/surface-the-ask/SKILL.md` — future sessions should read that skill directly rather than relying on this memory's summary. It encodes: triage-first; surface WHAT / CONTEXT / IMPACT; a socratic default-marked table; the "Ready-to-merge ask" PR-body convention (Case A/B); the reviewable-deliverable gate (proposal/DR-only ≠ ready-for-review; a DR merges with its impl; pre-impl direction = inline question or 👍-request); and the manual-testing `- [ ]` checklist-comment norm (precedent #59, standardized #72). Do NOT re-propose it as a follow-up.

Related: [[trailer-review-before-push-policy]] (before-push review gate; this is its after-review complement), [[proceed-on-clear-defaults]] (don't round-trip obvious decisions).
