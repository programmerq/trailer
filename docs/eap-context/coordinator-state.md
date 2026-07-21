# Coordinator state — exported 2026-07-21 ~16:30 UTC

The coordinator is the long-lived front-door session the owner talks to; it
dispatches child sessions and aggregates their reports. Peer sessions see it as
`session_01SxjUfLd3qkWSRcM6HY6n2Q`.

## Armed timers (do not survive EAP shutdown — re-arm on revival)

- Momentum patrol: send_later trigger `trig_01CktFPheXhpRfHxMUswTpZy`, fires
  2026-07-21T18:11:00Z. Protocol: every patrol (1) worker-sweeps open PRs for
  dirty/failing states and routes fixes to owning sessions, (2) checks for
  stuck/disconnected sessions (nudge once, then respawn from pushed
  checkpoints), (3) scans docs/backlog/ for unowned unambiguous items and
  dispatches, (4) surfaces only genuine decisions to the owner, then re-arms:
  120 min if actionable work found OR owner active since last patrol, else
  double previous interval (240 -> 480 -> 720 cap).
- The Wayland session also had its own ~60-minute PR re-check timer armed.

## Session map (session IDs are claude.ai project sessions; readable ~6 months)

Active, with ownership:
- cse_01Ej287RZ8gPN3tVdKkzPVPH "Takeover 2" — merged #110 (merge commit
  b5eae1f) on owner order; finalized #111 onto main (head 68691e6, green);
  owns #112, #113 (draft, plain "Read-only" pill rework done, tip 6458662,
  stacked on #112); watches #106.
- cse_01CBPGzRUcxWD2bquNBHVfrX "ML plumbing" — #104 reworked to menu-glyph
  model (head 84b867b, green, ready), #116 ux-guidelines (ready); #103 merged.
  Flagged fast-follow needing owner word: opportunistic background precalc.
- cse_01GTaQXhmKKjsmRepvCmfLec "OCR cluster" — #108 (head f7d80f7, green after
  the stacked-PR zero-CI fix), #114 (draft, checkmark-glyph cue, green,
  awaiting owner look); #100 merged.
- cse_01A2imVZRq2vN9pyxXkm7Fbf "Wayland CI" — #117 wayland-smoke tier (green,
  advisory-vs-required note pending owner), #118 XDG-portal capture (green);
  watching both. Phase-3 CI job for the portal stack offered, not dispatched.
- cse_01C33C9h39VraTDr3SymrbCQ "async open cluster" — #107 (conflicted with
  main; rebase dispatched 16:15Z), #109 (green).
- cse_014cLEWoPnB5Aa4qRYZP9jeg "golden-path regression + CI cleanup" — #115.
- cse_01HRvFpMozLNbBafCVNzZJwv "small-UX cluster" — #105 (green).
- cse_017WuLjkHqQm2HC6omUjWYq1 "editor cluster" — password validation, crop
  drag work.
- cse_01BoGC4gfSsSv8EYajLCEprc "surface-the-ask skill / conventions" — HOLDS
  STAGED, UNPUSHED work: the AGENTS.md G2 "committed-evidence sufficiency"
  clause (incl. <img>-recommended + ~150-char proxy-defang mechanism), blocked
  on the owner saying "go" IN THAT SESSION (permission classifier does not
  accept coordinator relays for pushes).
- cse_01W46bneSn26m7Q8zKzs1DnT "merge steward" — dormant; historical lesson:
  classifier blocks merges authorized only by relay.

Disconnected and superseded (do not revive): cse_01EWarTR13zD2H4mKn5QnejS
(replaced by Takeover 2), cse_01V4sYeHzvencsNNX9ERHVoh (superseded by the
Wayland CI session), cse_01FQMLxU6qfLfvqDyDxyKEio (file-menu IA, superseded by
cse_01QTdykksgnZ5xU8oaRYaMp4).

## Merge queue @ 16:07Z sweep (verify live before acting)

- Ready + green, awaiting owner merge: #104 #105 #106 #108 #109 #111 #112 #115
  #117 #118. #116 docs-only (all CI jobs path-skipped — that is its normal
  signal, not missing CI).
- #107: merge-conflicted (dirty); rebase in flight with its owning session.
- Drafts by design: #113 (stacked on #112 — merge #112 first, then retarget
  and final-rebase #113), #114 (owner-interactive rework; owner to judge the
  glyph).

## Pending OWNER actions at export time

1. Delete merged branch `claude/structural-pdf-keep-fidelity` via GitHub UI
   (#110 page) — the environment's git relay 403s the delete verb and the
   GitHub MCP has no delete-ref tool.
2. Say "go" in cse_01BoGC4gfSsSv8EYajLCEprc to release the staged G2
   AGENTS.md clause.
3. Review #114's glyph placement/semantics; review/merge queue above.
4. When touching branch protection: keep #117's `wayland-smoke` check
   advisory until sway+grim are baked into the runner image.
5. Parked hardware checks: TCC recheck script, Retina 1:1 confirms, recorder
   capture, manual checklists on #74/#89/#95/#113.
6. Gate-reference-rig P1 ratification (brief available on request).

## Standing process rules (detail in team-memory/)

Merge commits not squash; owner's in-session word (not relays) clears
classifier gates; stacked PRs get zero CI until a push lands with base=main;
re-merge main before final verify on long builds; checkpoint-push per item and
verify remote SHA (ls-remote) before claiming pushed; date+slug decision
records riding their implementing PRs; no proposal-only PRs; draft->ready flip
is the agent's job; manual-testing asks as checkbox PR comments; minimal-UI
surface (glyphs/badges over dialogs); UI PR bodies need inline before/after
screenshots (short filenames, HTML <img>, SHA-pinned, re-pinned after any
force-push).
