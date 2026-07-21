---
name: review-bot-ignore-ci-visibility
description: Owner ruling (ESCALATED 2026-07-20, SUPERSEDES the 2026-07-19 "reply on the PR to nudge it") — STOP posting review-bot instruction comments entirely; ignore silently, no comments. Do NOT post any comment replying to or instructing the claude[bot] reviewer; IGNORE its CI-visibility (and similar) caveats silently. Minimize ALL PR comment chatter (comment only to substantively address a real review finding). Do NOT surface reviewer/CI process topics to the owner unless one actually blocks a merge. The durable remedy is the workflow-prompt fix (electricsim #343 + redux #57).
metadata:
  type: feedback
---

# Ignore the claude[bot] reviewer's "can't see CI" caveats SILENTLY — post nothing

**Current owner ruling (2026-07-19, verbatim), which SUPERSEDES the earlier "tell it to review, reply on the PR" instruction:** "now you're just taking the place of the persistent comments. Isn't there a way to nudge it without posting a comment everywhere? I'm tired of hearing about the automated reviewer and CI at all."

**What changed (ESCALATED 2026-07-20).** The 2026-07-19 fix was to reply on each PR telling the bot to review on merits and not gate on CI visibility. That reply itself BECAME the new noise the owner was tired of. The owner has now ESCALATED (2026-07-20): sessions must **STOP posting review-bot instruction comments entirely** and IGNORE the bot's CI-visibility (and similar) caveats **SILENTLY** — the owner is tired of the comment noise. So the correct behavior is: **post NOTHING** in response to the bot's caveats ("Resource not accessible by integration" / "could not confirm green CI" and the like). Ignore them silently — the bot approves anyway; the caveat is not a blocker; CI gating is enforced by the repo's required checks at merge time. The durable remedy is the **workflow-prompt fix (electricsim #343 + redux #57)** — that is the real fix; the caveats disappear once it lands.

**How to apply (standing):**
1. Do NOT post ANY comment replying to or instructing the claude[bot] reviewer. Ignore its CI-visibility (and any similar) caveats SILENTLY — no stopgap comment, no instruction comment, nothing on the PR.
2. Minimize ALL PR comment chatter — comment on a PR ONLY when substantively addressing a real review finding (a code issue you fixed or must explain). Never post process/meta comments.
3. Do NOT surface reviewer-bot or CI-process topics to the owner in chat UNLESS one actually BLOCKS a merge. He does not want to hear about the automated reviewer or CI otherwise.
4. The durable workflow-prompt fix (electricsim #343 + redux #57) is the real remedy — the bot's caveats stop once it lands.
5. Two stopgap comments already exist (electricsim #341 and #347) from before this ruling — leave them; do not churn by deleting (deletion is just more activity). Simply stop posting more.

Related: [[electricsim-ci-only-shellcheck-and-checks-api]], [[no-op-checks-get-no-pr-comment]].
