---
name: proceed-on-clear-defaults
description: Owner feedback (2026-07-10) — don't bounce low-ambiguity decisions back; pick the sensible default, state it in one line, and proceed
metadata:
  type: feedback
---

# Proceed on clear defaults; don't round-trip obvious decisions

Owner (programmerq) feedback, 2026-07-10, after a session paused to ask "(a) leave branch for batching, (b) prep ci.yml paths-ignore, or (c) open PR anyway?" when (a)+(b) was already the session's own recommendation: "These aren't questions that I'd expect have ambiguity and shouldn't come back."

**Why:** the owner works async; a question that has an obvious best answer stalls the session for hours for no information gain. The owner explicitly invites pushback only when ambiguity is genuine.

**How to apply:** when the options have a clear best default (especially one you yourself recommended), take it, state the choice in one line ("Going with X; say the word for Y"), and keep working. Reserve real questions for genuinely destructive/irreversible forks or true 50/50 judgment calls. Related: [[trailer-review-before-push-policy]], [[trailer-inflight-work-persistence]].
