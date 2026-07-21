---
name: trailer-manual-test-checklist-norm
description: Owner norm 2026-07-17 (PR #72 comment) — any request for owner manual/on-device testing must be posted as a `- [ ]` checklist comment on the PR that he checks off as he goes
metadata:
  type: feedback
---

Owner on PR #72, 2026-07-17: "Comment here with a `[ ]` checklist for manual testing with my local dev build. I'll check it off as I go. This is a good thing to do for any manual testing requests going forward."

**Why:** he batches hands-on verification at his Mac; a checkable list on the PR makes the ask precise, trackable, and self-documenting (precedent: PR #59's 12-box Manual Testing checklist).

**How to apply:** when a PR needs owner hardware verification (TCC prompts, Retina/dpr rendering, on-device compile, OS shortcuts), post a PR comment with `- [ ]` items listing exact copy-paste steps and the expected observation per step. Keep items atomic and orderable. Encoded in the surface-the-ask skill. Related: [[trailer-ux-evidence-ruling]], [[trailer-no-proposal-only-prs]].
