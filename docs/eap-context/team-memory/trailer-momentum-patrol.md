---
name: trailer-momentum-patrol
description: Owner directive 2026-07-18 — coordinator runs a recurring patrol (adaptive send_later cadence with idle backoff) of PR conflicts, stuck agents, and unowned backlog items, dispatching clear next steps itself instead of gating on owner approval
metadata:
  type: feedback
  modified: 2026-07-18T05:25:51.127Z
---

Owner, 2026-07-18: "Set a periodic reminder to take a look at outstanding backlog items, PR conflicts, and stuck agents. This will help keep momentum, especially with directives that facilitate clear correct next steps rather than gating on owner-approval for items that don't need disambiguation."

**Why:** momentum dies between his check-ins when conflicted PRs, dead workers, and unowned backlog items sit waiting; he wants the machine self-healing, with him consulted only on genuine ambiguity.

**How to apply (coordinator):** keep a send_later self-message armed with ADAPTIVE cadence (owner refinement, same day: "hourly is probably too aggressive. Give it a backoff that takes into account that I'm a human that sleeps and naturally will have items die down if the backlog is dwindling."). Base interval 120 min when the patrol finds actionable work or the owner has been active since the last patrol; otherwise double the previous interval (240 → 480 → 720 min, cap 12h). Each patrol: (1) sweep open PRs for dirty/failing states → route rebases/fixes to owning sessions; (2) check for disconnected/stalled sessions → nudge or respawn; (3) scan docs/backlog/ for unowned items with unambiguous next steps → dispatch; (4) surface only real decisions/milestones to the owner; (5) re-arm with the computed interval. Related: [[proceed-on-clear-defaults]], [[trailer-no-proposal-only-prs]], [[post-merge-conflict-sweep]].
