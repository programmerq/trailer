---
name: trailer-backlog-pr-triage-2026-07-11
description: Triage of the six pre-overhaul open PRs (#33-#38) against the new criteria gates on 2026-07-11 — verdicts, gate-practicality feedback, and the pending owner decisions
metadata:
  type: project
---

First real exercise of the criteria machinery (G1-G9 + ADRs) on organic backlog. All six rebased onto main `f7090f9` (post-#40+#44, baseline 45/45 green) in isolated worktrees, rebuilt, re-tested. See [[trailer-requirements-summary]], [[trailer-review-before-push-policy]], [[trailer-remote-build-recipe]].

**Verdicts (2026-07-11):**
- **#33** a11y accessible-names (`claude/a11y-accessible-names`) — MERGE-READY. Clean rebase (ref 0aea265), 45/45 + new sweep guard 11/11. Fix still needed: main never added the Search-button accessibleName. G8 dormant → lands under per-PR no-a11y-regression rule.
- **#34** Copy Page as Image (`claude/copy-page-as-image`) — MERGE-READY after a G3 fix I applied (ref 9a6ffac). Clean rebase, 45/45. Adds NO shortcut → clear of ADR 0001. Original PR had a G3 gap (disabled menu item lacked explanatory tooltip; Edit menu didn't `setToolTipsVisible(true)`); fixed to match repo's Share/Two-Pages pattern + extended uat_fnd_070. NOTE: amend set committer to Claude (author preserved Jeff) — normalize at batch-merge.
- **#35** continuous-mode arrow=screenful (`claude/continuous-mode-arrow-step`) — NEEDS-WORK. Rebase clean (only union-mergeable CMake add/add; single-page path untouched, ref 09dcf8f), but its own uat_vwr_025 FAILS: continuous-mode PageDown/PageUp double-fire with MainWindow's pre-existing Next/Previous-Page QAction shortcuts (delta 2× pageStep). Pre-existing, not rebase-induced. Down/Up arrow part is a sound bugfix (no ADR needed); PageUp/PageDown remap is a keybinding-semantics decision needing owner input (drop them, keep arrows+Space, OR write an ADR).
- **#36** content-aware sidebar defaults (`claude/content-aware-defaults`) — ADR-BLOCKED by G6. Clean rebase + 46/46 (ref 3888bc3). New ≥3-field/≥20-page logic (kFormFieldThreshold=3, kLongDocumentPages=20 in src/ui/ContentAwareDefaults.h:33-34) is orthogonal to main's live ≥1-field fill-enable (MainWindow.cpp:2650) and 50-page OCR-skip (OcrController.h:79) — no double-apply. Blocked because ADR 0003 is still `proposed`; needs owner sign-off on the numbers 20 and 3. Merge-ready the instant 0003 accepted.
- **#37** thumbnail row-height diagnosis (`claude/todo-thumb-rowheight-diagnosis`) — MERGE-READY. Docs-only, clean rebase (ref 34ea7af). Diagnosis re-verified accurate vs live Sidebar.cpp (fixed 108px sizeHint vs KeepAspectRatio → ~8px portrait / ~52px landscape gaps). Both PNGs valid.
- **#38** actions/checkout 6→7 (dependabot) — MERGE-READY pending fresh CI. Already on main HEAD. Today's Windows-cross failure was the PRE-#44 ccache apt-index bug (`E: Unable to locate package ccache`), NOT checkout; #44 already fixes it. checkout v7 is node24-compatible (v6 was already node24). Not ours to push.

**Gate-practicality feedback (for owner):**
- G2 offscreen `grab()` works for menu items (QMenu popup()+grab() offscreen produced real pixmaps). Edge: disabled-because-unsupported-doc vs disabled-because-no-doc couldn't be synthesized offscreen (needs animated-GIF fixture Qt can't write) but is pixel-identical — acceptable.
- G6 worked exactly as designed: cleanly blocked #36 on proposed ADR 0003, catching the precise ≥20/≥3 numbers 0003 flagged for sign-off.

**PROCESS FINDING:** force-pushing rebased branches to REFRESH pre-existing PRs (to re-run CI on current main) is auto-blocked by the safety classifier when the push instruction originates from coordinator routing context rather than a direct user message. Owner must approve the push directly (or the instruction must come in a user turn). As of 2026-07-11 the #33/#34/#37 rebased refs sit LOCAL-ONLY awaiting owner "push them".

**Pending owner decisions:** (1) approve push of #33/#34/#37; (2) ADR 0003 numbers → unblocks #36; (3) #35 PageUp/PageDown keybinding decision. Related: [[trailer-integration-batch-pr40]], [[trailer-followup-docket]].
