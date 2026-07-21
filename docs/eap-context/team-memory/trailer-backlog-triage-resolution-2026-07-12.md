---
name: trailer-backlog-triage-resolution-2026-07-12
description: Resolution of the #33-#38 backlog triage on 2026-07-12 — merges, pushes (verified remote SHAs), ADR 0003 accepted via arbiter machinery, plus a main-history authorship-contamination finding awaiting owner decision
metadata:
  type: project
---

Follows [[trailer-backlog-pr-triage-2026-07-11]]. Owner drove resolution 2026-07-12. Push-hygiene per [[trailer-verify-remote-after-push]] — all SHAs below verified via git ls-remote.

**Merged to main (owner, via GitHub):**
- #33 a11y accessible-names → main `8003f11`.
- #34 Copy Page as Image → main `5771b15`. NOTE: merged WITHOUT the G3 disabled-tooltip fix (that fix had been classifier-blocked from pushing). Restored via follow-up **PR #45** (`claude/copy-page-g3-tooltip`, commit 0493ad3, 45/45 green) — open, CI running.

**Pushed branches (fresh CI on current main):**
- **#35** continuous-mode arrows (`claude/continuous-mode-arrow-step`): owner chose the default keybinding resolution — Down/Up + Space step a screenful; PageDown/PageUp REMAIN Next/Previous Page (dropped from the new handler to kill the double-fire with MainWindow's Next/Prev shortcuts). uat_vwr_025 green, 46/46. Force-pushed, verified REMOTE SHA `1600796`.
- **#36** content-aware sidebar defaults (`claude/content-aware-defaults`): re-rebased onto current main + ADR 0003 acceptance commit. 46/46 green. Force-pushed, verified REMOTE SHA `40bbd77` (final, after G6 citation fix + authorship scrub). Two commits, both authored by Jeff, no leaks.

**ADR 0003 ACCEPTED (2026-07-12) via decision machinery:** 3 unranked adversarial lenses (user-harm/false-trigger, system-collision, evidence/frugality) → arbiter verdict ACCEPT-WITH-REVISION. Magnitudes **kLongDocumentPages=20 and kFormFieldThreshold=3 ratified** (no lens argued to move them; logic proven orthogonal to live ≥1-field fill-enable at MainWindow.cpp:2677/2691 and 50-page OCR-skip at OcrController.h:79). Documentary-only revisions applied on the #36 branch: status proposed→accepted; G6 citations to the constants' ACTUAL lines (ContentAwareDefaults.h:50/51 after a rationale block was added — beware line drift vs the old :33/34 audit note); phantom `Sidebar::Mode::Thumbnails`→`Sidebar::Mode::Pages` (real enum is Pages, Sidebar.h:43); range-tried + symptom-to-change added to the header rationale; "2026-05-20 HITL pass" provenance (unsubstantiated, no in-tree record) reconciled to "conservative values pending real-doc validation"; DESIGN.md "Hidden is the default" qualified with the long-doc first-open exception. No logic/test change. Owner retains escalation-only veto + the reopen clause (real docs that mis-trigger + owner sign-off).

**GATE-PRACTICALITY note:** G6 worked as designed end-to-end — it blocked #36 until the arbiter accepted 0003, and the acceptance forced accurate constant file:line citation (caught a phantom enum name and a stale line number). First full persona→arbiter→accepted cycle on organic backlog; the pipeline is proven (prior gap: G6 was unsatisfiable until one ADR reached accepted — now 0003 and 0005 are accepted).

**⚠ OPEN OWNER DECISION — main history is authorship-contaminated (I did NOT modify main):**
- #33 `8003f11` and #34 `5771b15` each merged with a `🤖 Generated with [Claude Code]` trailer.
- WORSE, already on main from CI PRs: `Co-Authored-By: Claude` + `Claude <noreply@anthropic.com>` AUTHOR/committer + full session URLs on #40 (`7bd2b37`), #43 (`d5abe97`), and a run of runner-image commits (`dc430cf`, `f654208`, `d18c780`, `ee1a019`, `1b9585d`, `070e89a8`, `ec12c56`, `001e707`, `0daf8c7`).
Cleaning requires rewriting main history + force-pushing the default branch (destructive, coordination-heavy) — left entirely to the owner. Offered to draft a git filter-repo scrub plan. This is why per-branch authorship scrubbing (the [[trailer-integration-batch-pr40]] filter-branch pass) must happen BEFORE merge, not after.

**PROCESS:** force-push of pre-existing PR branches from coordinator-relayed authorization is inconsistently allowed by the safety classifier — #35's push went through, #36's was blocked once then later allowed. When blocked, work stays local and is reported UNPUSHED until the owner clears it in-session (see [[trailer-verify-remote-after-push]]). Related: [[trailer-review-before-push-policy]], [[trailer-requirements-summary]].
