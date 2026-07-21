# Team memory index

Consolidated 2026-07-10 (memory-hygiene pass). Canonical names use the trailer-* prefix; `pre-push-local-review-policy` was merged into [[trailer-review-before-push-policy]] and deleted.

## Requirements & owner rulings

- [[trailer-requirements-summary]] — confirmed design-criteria decisions from the 2026-07-09 interview: philosophy, gates, decision machinery, milestones, push/PR policy.
- [[trailer-review-before-push-policy]] — CANONICAL push/PR policy: tests written first → 1-2 local review agents (variant personas) → address findings → push branch and open per-item PR ready-for-review (normal practice since self-hosted CI; batching retired 2026-07-14); merges to main stay owner-gated.
- [[trailer-perf-measurement-ruling]] — perf budgets measured by implementing agents + review rounds; NO CI wall-time gate, only deterministic proxies.
- [[trailer-ux-evidence-ruling]] — offscreen grab() screenshots satisfy UX-Done for most changes; real-display/real-Mac passes batched at milestones (hybrid).
- [[proceed-on-clear-defaults]] — don't round-trip obvious decisions; pick the sensible default, state it in one line, proceed.
- [[trailer-no-narration-dialogs]] — owner taste rule: no dialogs that narrate the user's own action or a no-op; cancel is silent, non-image paste noops.
- [[trailer-minimal-ui-surface]] — minimal UI surface; subtle in-context status glyphs over dialogs/popups/progress bars; document stays the focus (owner, 2026-07-21, PR #104).
- [[trailer-dr-naming-date-slug]] — decision records use YYYY-MM-DD-<slug>.md filenames (no sequential numbers); 0002–0013 + #69's 0014 grandfathered.
- [[trailer-no-proposal-only-prs]] — DRs merge with their implementing PR; proposals are asks/👍-requests, not docs-only PRs; proposal PRs stay draft.
- [[trailer-manual-test-checklist-norm]] — manual-testing asks = `- [ ]` checklist comments on the PR (owner checks off as he goes).

## Work state & follow-ups

- [[trailer-v030-shipped]] — v0.3.0 RELEASED 2026-07-13: tag v0.3.0 @ f48f99c (PR #50), 3 artifacts (linux tar.gz / macos dmg / windows zip); first release under the criteria machinery; msi/deb/rpm + k8s 11-min pod-kill backlogged.
- [[trailer-followup-docket]] — consolidated ranked follow-ups from the four archived work sessions (P0 silent-discard-on-close now RESOLVED).
- [[trailer-dirty-close-fix-merged]] — P0 UAT-FND-014 dirty-close silent-discard FIXED + MERGED via PR #47 @ f0f76ad (2026-07-12); veto-signal + confirmCloseDirtyDoc, 6 regression cases, ADR 0004 accepted.
- [[trailer-integration-batch-pr40]] — 7-branch criteria batch PR #40 MERGED to main 2026-07-11 (45/45 green, authorship rewritten).
- [[trailer-undo-cap-desync]] — >64-edit undo desync + ImageDocument unification FIXED on fix/undo-stabilization @ 877f9fb; in PR #40 batch.

## Environment & process facts

- [[trailer-remote-build-recipe]] — canonical build+test recipe (Qt 6.11 via aqtinstall, ONNX via NuGet); automated by session-setup hook @ 03ac5c9.
- [[trailer-ci-on-k8s-runners]] — CI on self-hosted trailer-k8s runners, validated green; ci.yml fires only on push/PR to main.
- [[trailer-cross-container-persistence]] — only registered .md memory files replicate across containers; loose files (patches) do NOT.
- [[trailer-inflight-work-persistence]] — persist in-flight work by pushing an origin branch; fetch fresh before declaring work lost.
- [[trailer-verify-remote-after-push]] — pushed-claims must cite the remote SHA; blocked pushes mark reports UNPUSHED (two near-misses 2026-07-12).
- [[trailer-momentum-patrol]] — adaptive coordinator patrol (conflicts, stuck agents, unowned backlog): 120-min base, idle backoff to 12h; self-dispatched fixes, owner consulted only on real ambiguity.
